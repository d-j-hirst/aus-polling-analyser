"""Prepare immutable publication packages while forecasts and uploads continue.

The C++ process alone owns count acceptance and forecasting. This worker consumes
only completed numerical handoffs. Every publication has a deadline measured from
calculation completion, including time spent waiting for this worker to restart.
Late commentary is saved for inspection but cannot modify a released package.
"""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from functools import lru_cache
from pathlib import Path
import time

from . import activity, commentary, comparisons, reporting_days, rule_commentary, seat_alerts, tcp_reporting
from .storage import atomic, fingerprint, read
from .run_settings import check_trial_options
from .vote_type_names import VoteTypeNames


def utc(seconds=None):
    return datetime.fromtimestamp(time.time() if seconds is None else seconds, timezone.utc).isoformat()


def ready_forecasts(folder):
    return sorted((folder/'packages').glob('*/ready-numerical.json'),
                  key=lambda path: (path.parent.name != 'origin', path.parent.name))


def public_run(config):
    # Use an allow-list: local folders, credentials and API settings never cross
    # the website boundary, even if later versions add them to local settings.
    return {k: config[k] for k in ('version', 'run_id', 'election', 'kind',
            'display_name', 'visibility', 'election_date', 'timezone')}


class Processor:
    def __init__(self, folder, settings=None, client=None):
        self.folder = Path(folder)
        self.config = read(self.folder / 'run.json')
        check_trial_options(self.config, commentary=settings)
        self.local_trial = self.config.get('local_trial', False)
        self.vote_type_names = VoteTypeNames(self.config['election'])
        reporting_days.schedule(self.config)  # Reject invalid reporting hours before any API call.
        self.settings = dict(settings or {'enabled': False})
        # This switch affects only the frozen website text. Toggling it must
        # not change an AI request identity, reserve credit or resend a call.
        self.compare_writers = self.settings.pop('compare_writers', False)
        if not isinstance(self.compare_writers, bool):
            raise ValueError('compare_writers must be true or false')
        self.rules_only = self.settings.pop('rules_only', False)
        if not isinstance(self.rules_only, bool) or (self.rules_only and (self.compare_writers or self.settings.get('enabled'))):
            raise ValueError('rules_only must be a boolean and cannot be combined with AI or compare_writers')
        self.minimum_count_change = self.settings.get('minimum_count_change', 10)
        if not isinstance(self.minimum_count_change, int) or self.minimum_count_change < 1:
            raise ValueError('minimum_count_change must be a positive number of votes')
        self.prompt_version = self.settings.get('prompt_version', commentary.PROMPT_VERSION)
        if self.prompt_version not in commentary.SUPPORTED_VERSIONS:
            raise ValueError('Unsupported commentary prompt version')
        self.client = client
        self.executor = ThreadPoolExecutor(max_workers=max(1, min(8, self.settings.get('concurrency', 4))))
        self.pending = {}
        self.received = set()
        self.awaiting_timing = {}
        self.budget = None
        if self.settings.get('enabled'):
            self.budget = commentary.Budget(self.folder/'commentary-budget.sqlite', self.settings['spending_limit_usd'])
        self.last_status = None
        self.status_written_at = 0
        self.status_write_error = None
        self.closing = False
        self.summary_index = {}
        self.released = set()
        self.acknowledged_digests = None
        self.load_summary = lru_cache(maxsize=16)(lambda identity: comparisons.comparison_summary(self.folder/'packages'/identity))

    def close(self):
        # Running HTTP calls have finite timeouts; preserve their eventual local
        # results when stopping cleanly. Pending work checks its deadline itself.
        self.closing = True
        self.executor.shutdown(wait=True)
        self.collect()

    def collect(self):
        for key, (future, path) in list(self.pending.items()):
            if future.done():
                atomic(path, future.result())
                del self.pending[key]
        self.log_finished_commentary()

    def log_finished_commentary(self):
        # Most forecasts have all their replies by publication. If a request
        # is still in flight at the deadline, report its actual end when it
        # returns. Only the console and local receipts use these timings.
        for package, (keys, deadline) in list(self.awaiting_timing.items()):
            if not keys.isdisjoint(self.pending):
                continue
            receipts = []
            for key in keys:
                path = package/'commentary'/f'{key}.json'
                if path.exists():
                    receipts.append(read(path))
            spans = [r.get('request_timing', {}) for r in receipts]
            starts = [s['first_sent_at'] for s in spans if 'first_sent_at' in s]
            ends = [s['last_received_at'] for s in spans if 'last_received_at' in s]
            complete = sum(r['status'] == 'complete' for r in receipts)
            identity = f"{self.config['run_id']}/{package.name}"
            message = f"Commentary for {identity}: {complete}/{len(keys)} distinct comparisons completed."
            # Older saved replies lack exchange timestamps. A timeout without
            # a reply likewise cannot supply a last-response time.
            if any('request_timing' not in r for r in receipts):
                message += ' Full request timing unavailable.'
            elif starts and ends and max(ends) >= min(starts):
                seconds = max(ends) - min(starts)
                message += f' First request to last response: {seconds:.1f}s.'
                if max(ends) > deadline:
                    message += ' Last response arrived after the publication deadline.'
            elif ends:
                message += ' Clock readings do not give a valid elapsed time.'
            elif starts:
                message += ' No API response received.'
            else:
                message += ' No recorded API request.'
            print(message, flush=True)
            del self.awaiting_timing[package]

    def request(self, packet, deadline, result_path, cache_overview):
        started_path = result_path.with_suffix('.started.json')
        if result_path.exists():
            return
        key = result_path.stem
        if key in self.pending:
            return
        # An unreceipted call may have been billed before a crash. Keep its cost
        # reservation and do not silently send the same paid request again.
        if started_path.exists():
            atomic(result_path, {'status': 'unavailable', 'reason': 'Worker interrupted during request; no automatic paid duplicate.',
                                 'finished_at': utc()})
            return
        atomic(started_path, {'queued_at': utc(), 'deadline': utc(deadline)})
        self.pending[key] = (self.executor.submit(self.perform, packet, deadline, cache_overview,
                                                  result_path.with_suffix('.input.json')), result_path)

    def perform(self, packet, deadline, cache_overview=False, input_path=None):
        errors = []
        timing = {}
        # Each distinct comparison has its own worker and may retry. Save the
        # first dispatch across attempts and the last actual reply, even if a
        # reply is subsequently rejected. UTC epoch times also span restarts.
        def time_exchange(send):
            def measured(*args, **kwargs):
                timing.setdefault('first_sent_at', time.time())
                response = send(*args, **kwargs)
                timing['last_received_at'] = time.time()
                return response
            return measured
        settings = dict(self.settings, cache_overview=cache_overview)
        client = (commentary.OpenAIClient(settings, time_exchange) if self.client is None
                  else time_exchange(self.client))
        maximum = max(1, min(3, self.settings.get('attempts', 2)))
        # requests.json holds the local working evidence. Archive the actual
        # message text and cache boundaries once, using the same payload that
        # determines the spending reservation. Preparation belongs to this
        # comparison's worker, so other timeframes can proceed independently.
        def record_input(messages):
            if input_path is not None and not input_path.exists():
                atomic(input_path, {'format': 'commentary-messages-v1', 'input': messages})
        for attempt in range(maximum):
            if time.time() >= deadline:
                break
            try:
                result = commentary.generate(packet, settings, self.budget, deadline, client, record_input)
                result.update(finished_at=utc(), attempts=attempt+1, request_timing=timing)
                return result
            except Exception as error:
                errors.append(str(error))
                # Budget/configuration failures cannot be improved by retrying.
                if isinstance(error, (ValueError, KeyError)) and ('budget' in str(error).lower() or self.client is None and not self.settings.get('model')):
                    break
                if attempt+1 < maximum:
                    time.sleep(max(0, min(1, deadline-time.time())))
        return {'status': 'unavailable', 'reason': errors[-1] if errors else 'Commentary deadline reached in queue.',
                'errors': errors, 'finished_at': utc(), 'request_timing': timing}

    def prepare(self, paths):
        summaries = []
        for path in paths:
            package = path.parent
            if package.name in self.summary_index:
                summaries.append(self.summary_index[package.name])
                continue
            if not (package/'summary.json').exists():
                numerical = read(package/'numerical.json')
                if numerical['run_id'] != self.config['run_id'] or numerical['forecast_id'] != package.name:
                    raise ValueError('Numerical handoff identity does not match its run/folder')
                if read(path).get('forecast_id') != numerical['forecast_id']:
                    raise ValueError('Numerical readiness marker has the wrong identity')
                if package.name not in self.received:
                    source = (f"source {numerical['source_time']}" if numerical.get('source_time')
                              else 'start-of-counting reference')
                    received_at = datetime.now(timezone.utc).isoformat(timespec='seconds')
                    print(f"Received forecast {self.config['run_id']}/{package.name} at {received_at} ({source}).", flush=True)
                    self.received.add(package.name)
                # Write summary last. It is also the acknowledgement that local
                # explanatory evidence has been fully materialised.
                evidence = comparisons.evidence(numerical, self.folder)
                full = comparisons.summary(numerical, self.folder)
                # Store the call's first forecast alongside the current winners.
                # Every comparison window and a restarted uploader then read the
                # same history, even if the earlier package is still uploading.
                prior_calls = summaries[-1].get('seat_calls', {}) if summaries else {}
                full['seat_calls'] = seat_alerts.active_calls(full, evidence, prior_calls)
                full['booth_warnings'] = evidence.get('booth_warnings', {})
                if summaries:
                    previous = read(self.folder/'packages'/summaries[-1]['forecast_id']/'evidence.json.gz')
                    earlier = self.load_summary(summaries[-1]['forecast_id'])
                    before, after, _ = tcp_reporting.reporting_views(previous, evidence,
                        tcp_reporting.zero_districts(earlier), tcp_reporting.zero_districts(full))
                    full['count_activity_since_previous'] = activity.count_changes(before, after)
                atomic(package/'evidence.json.gz', evidence)
                atomic(package/'summary.json', full)
            else:
                full = read(package/'summary.json')
            # Reversal detection needs every earlier winning chance, but only
            # actual comparison anchors need all percentile/candidate accounts.
            # Keep that small index in memory and bound the larger summary cache.
            compact = {k: full[k] for k in ('forecast_id', 'source_time', 'input_revision', 'origin')}
            compact['count_activity_since_previous'] = full.get('count_activity_since_previous', {})
            # Later reports can establish that the commission had previously
            # named different finalists, even when both comparison dates are
            # well after the switch. The district totals are small enough to
            # keep alongside win chances without reopening old booth archives.
            compact['published_district_tcp'] = full.get('published_district_tcp', {})
            compact['seat_calls'] = full.get('seat_calls', {})
            compact['seats'] = {s: {'win_percent': row['win_percent']} for s, row in full['seats'].items()}
            self.summary_index[package.name] = compact
            summaries.append(compact)
        return summaries

    def publish_status(self, status):
        """Keep the GUI informed without letting a busy display file hold up forecasts."""
        now = time.monotonic()
        # Report changes immediately. An idle watcher only needs an occasional
        # timestamp to show it is alive; replacing the same file twice a second
        # creates needless contention with Windows readers of a WSL replay.
        if self.last_status == status and not self.status_write_error and now - self.status_written_at < 30:
            return
        path = self.folder/'reporting-status.json'
        try:
            atomic(path, dict(status, updated_at=utc()))
        except OSError as error:
            # This is the GUI's status display. Forecasts, commentary and server
            # receipts have their own durable files. Leave the previous status
            # readable and try the latest status on the next poll. In particular,
            # a failure to write an error message must not stop the upload loop.
            message = f'Could not update {path}: {error}'
            if 'error' in status:
                message += f" (reporting error: {status['error']})"
            if message != self.status_write_error:
                print(message, flush=True)
            self.status_write_error = message
            return
        self.last_status = status
        self.status_written_at = now
        self.status_write_error = None

    def saved_acknowledgements(self):
        """A server receipt identifies the exact report that must survive restart."""
        digests = {}
        for path in (self.folder/'receipts').glob('*.json'):
            for identity, record in read(path).get('forecasts', {}).items():
                if not record.get('accepted'):
                    continue
                package = self.folder/'packages'/identity
                if not (package/'publication.json').is_file():
                    raise ValueError(f'Restore the already uploaded publication in {package} before resuming.')
                digest = record.get('receipt', {}).get('sha256')
                if not isinstance(digest, str) or not digest:
                    raise ValueError(f'The acknowledgement for {identity} in {path} has no publication digest.')
                if identity in digests and digest != digests[identity]:
                    raise ValueError(f'Destination receipts disagree about {identity}. Check the saved publications.')
                digests[identity] = digest
        return digests

    def recover_publication(self, folder, numerical):
        """Finish a release interrupted between the frozen body and its marker."""
        path = folder/'publication.json'
        if not path.exists():
            return False
        body = read(path)
        forecast = body['forecast']
        source_time = numerical['source_time']
        if source_time:
            source_time = comparisons.instant(source_time, self.config['timezone']).isoformat()
        if (body.get('frozen') is not True or body.get('schema_version') != 1 or
                body['run'] != public_run(self.config) or forecast['forecast_id'] != folder.name or
                forecast['source_time'] != source_time or forecast.get('source_fingerprint') != numerical.get('source_fingerprint') or
                any(forecast[key] != numerical[key] for key in ('input_revision', 'input_fingerprint', 'iterations', 'completed_at'))):
            raise ValueError(f'Frozen publication does not match its numerical handoff at {path}.')
        digest = fingerprint(body)
        if digest != self.acknowledged_digests.get(folder.name, digest):
            raise ValueError(f'The frozen publication at {path} differs from the acknowledged upload.')
        atomic(folder/'ready-publication.json', {'schema_version': 1, 'forecast_id': folder.name, 'sha256': digest})
        return True

    def tick(self, paths=None):
        self.collect()
        updated = read(self.folder/'run.json')
        if public_run(updated) != public_run(self.config) or updated.get('local_trial', False) != self.local_trial:
            raise ValueError('Registered run identity changed; create a new run instead')
        check_trial_options(updated)
        reporting_days.schedule(updated)
        self.config = updated
        if self.acknowledged_digests is None:
            self.acknowledged_digests = self.saved_acknowledgements()
        if paths is None:
            paths = ready_forecasts(self.folder)
        summaries = self.prepare(paths)
        packages = {}
        for path in paths:
            package = path.parent
            if package.name in self.released or (package/'ready-publication.json').exists():
                self.released.add(package.name)
                packages[package.name] = 'released'
                continue
            numerical = read(package/'numerical.json')
            if self.recover_publication(package, numerical):
                self.released.add(package.name)
                packages[package.name] = 'released'
                continue
            if package.name == 'origin':
                atomic(package/'requests.json', {})
                self.release(package, numerical, {}, {})
                self.released.add(package.name)
                packages[package.name] = 'released reference'
                continue
            complete = comparisons.instant(numerical['completed_at'], 'UTC').timestamp()
            timeout = self.config.get('commentary_timeout_seconds', 30 if self.config['election'].endswith('fed') else 90)
            if timeout < 0 or timeout > 600:
                raise ValueError('Commentary timeout must be between zero and 600 seconds')
            deadline = complete+timeout
            current = self.load_summary(package.name)
            prepared = package/'comparisons.json'
            if not prepared.exists():
                groups = comparisons.comparisons(current, summaries,
                    lambda identity: comparisons.comparison_evidence(self.folder/'packages'/identity), self.config, self.load_summary)
                requests = {}
                rule_contexts = {}
                mechanical = {}
                for window, group in groups.items():
                    # The writers still see the full group. Each seat's FP and TCP
                    # distributions are saved with both endpoint forecasts, so their
                    # individual percentile differences need no extra copy per window.
                    mechanical[window] = {k: v for k, v in group.items()
                                          if k not in ('evidence', 'seat_changes', 'ranked_tcp_groups', 'rule_regional',
                                                       'rule_regional_changes', 'rule_earlier_ordinary_count', 'rule_tpp_components')}
                    if not group['available']:
                        continue
                    if self.local_trial:
                        mechanical[window]['commentary_decision'] = 'disabled'
                        continue
                    mechanical[window]['commentary_decision'] = activity.decision(group, self.minimum_count_change)
                    if mechanical[window]['commentary_decision'] != 'request':
                        continue
                    if self.rules_only:
                        mechanical[window]['commentary_decision'] = 'rules_based'
                    packet = comparisons.prompt_packet(current, group)
                    packet['source_timezone'] = self.config['timezone']
                    key = fingerprint({'packet': packet, 'settings': self.settings, 'prompt': self.prompt_version})
                    requests[window] = {'key': key, 'packet': packet}
                    if self.compare_writers or self.rules_only:
                        # Summarise all comparable booths locally. AI requests,
                        # cache identities and public evidence retain their normal size.
                        if key not in rule_contexts:
                            rule_contexts[key] = rule_commentary.comparison_context(group, packet)
                        requests[window]['rule_context'] = rule_contexts[key]
                atomic(package/'requests.json', requests)
                atomic(prepared, mechanical)
            mechanical, requests = read(prepared), read(package/'requests.json')
            if self.local_trial:
                # Keep the normal numerical forecast and comparison tables. A
                # deliberately unrealistic trial needs no prose, even on a
                # quiet update, and no private counts in a commentary request.
                self.release(package, numerical, mechanical, {})
                self.released.add(package.name)
                packages[package.name] = 'released'
                continue
            results = {}
            for window, group in mechanical.items():
                local_text = activity.notice(group, window)
                if local_text:
                    results[window] = local_text
            # Several windows can use the same reference and therefore the
            # same request. Only distinct comparisons can reuse this overview.
            ai_requests = {} if self.rules_only else requests
            cache_overview = len({request['key'] for request in ai_requests.values()}) > 1
            preset_changed = set()
            for window, request in ai_requests.items():
                result_path = package/'commentary'/f"{request['key']}.json"
                if result_path.exists():
                    result = read(result_path)
                    result.pop('request_timing', None)  # Operator-only timing stays in the local receipt.
                    if comparisons.instant(result['finished_at'], 'UTC').timestamp() <= deadline:
                        results[window] = result
                elif not self.settings.get('enabled'):
                    results[window] = {'status': 'unavailable', 'reason': 'Commentary is disabled or not configured.'}
                elif result_path.stem not in self.pending and not self.closing and time.time() < deadline:
                    # A restarted worker may use a new preset. Only a request
                    # prepared with this configuration can start a paid call.
                    key = fingerprint({'packet': request['packet'], 'settings': self.settings,
                                       'prompt': self.prompt_version})
                    if key == request['key']:
                        self.request(request['packet'], deadline, result_path, cache_overview)
                    else:
                        results[window] = {'status': 'unavailable',
                                           'reason': 'Commentary settings changed after this forecast was prepared.'}
                        preset_changed.add(window)
            ready = all(w in results and (results[w]['status'] == 'complete' or w in preset_changed
                                         or not self.settings.get('enabled')) for w in ai_requests)
            if ready or time.time() >= deadline:
                for window in mechanical:
                    if not mechanical[window]['available']:
                        results[window] = {'status': 'unavailable', 'reason': mechanical[window]['reason']}
                    elif window not in results and not self.rules_only:
                        results[window] = {'status': 'unavailable', 'reason': 'Commentary publication deadline reached.'}
                self.release(package, numerical, mechanical, results)
                if self.settings.get('enabled'):
                    if requests:
                        self.awaiting_timing[package] = ({r['key'] for r in requests.values()}, deadline)
                    else:
                        print(f"No commentary requests needed for {self.config['run_id']}/{package.name}.", flush=True)
                self.released.add(package.name)
                packages[package.name] = 'released'
            else:
                packages[package.name] = 'waiting for commentary'
        self.log_finished_commentary()
        status = {'packages': packages, 'pending_commentary_requests': len(self.pending)}
        self.publish_status(status)
        return dict(status, updated_at=utc())

    def release(self, folder, numerical, mechanical, results):
        if (folder/'ready-publication.json').exists():
            return
        metadata = {k: numerical[k] for k in ('forecast_id', 'input_revision', 'input_fingerprint',
                    'iterations', 'delivered_at', 'completed_at', 'source_time')}
        metadata['source_fingerprint'] = numerical.get('source_fingerprint')
        metadata['source_time'] = comparisons.instant(metadata['source_time'], self.config['timezone']).isoformat() if metadata['source_time'] else None
        metadata['origin'] = numerical.get('origin', False)
        metadata['source_timezone'] = self.config['timezone']
        metadata['simulation'] = numerical.get('simulation')
        metadata['model_revision'] = numerical.get('model_revision', numerical['input_revision'])
        for group in mechanical.values():
            # A restart can resume a comparison prepared by an older worker.
            group.pop('seat_changes', None)
            if group.get('reference_source_time'):
                group['reference_source_time'] = comparisons.instant(group['reference_source_time'], self.config['timezone']).isoformat()
            flow_reference = group.get('statewide_preference_changes')
            if flow_reference and flow_reference.get('reference_source_time'):
                flow_reference['reference_source_time'] = comparisons.instant(
                    flow_reference['reference_source_time'], self.config['timezone']).isoformat()
        # Numerical units and labels are part of the public contract. The complete
        # diagnostics stay local; cited evidence is included only in compact form.
        requests = {} if self.local_trial else read(folder/'requests.json')
        # The rules writer uses the local evidence and embeds its figures in
        # the text. Only the AI edition can reference separate evidence IDs.
        cited_requests = {} if self.rules_only else requests
        selected_evidence = {r['key']: r['packet']['evidence'] for r in cited_requests.values()}
        if self.compare_writers or self.rules_only:
            review_flags = {}
            results = rule_commentary.append_editions(results, requests, numerical['forecast_id'], self.load_summary,
                                                     review_flags, rules_only=self.rules_only)
            if review_flags:
                atomic(folder/'rule-review.json', review_flags)
        results = self.vote_type_names.reports(results)
        # A call is a model decision every reader should see. Both writers
        # share this bulletin, including on quiet updates or an API timeout.
        for window, group in mechanical.items():
            if window in results and group.get('seat_call_changes'):
                results[window] = dict(results[window], seat_call_changes=group['seat_call_changes'])
        summary = self.load_summary(numerical['forecast_id'])
        report = dict(numerical['report'])
        if summary.get('seat_calls'):
            report['seatCalls'] = summary['seat_calls']
        if summary.get('booth_warnings'):
            report['possibleBoothErrors'] = summary['booth_warnings']
        payload = {'schema_version': 1, 'run': public_run(self.config), 'forecast': metadata,
                   'report': report, 'comparisons': mechanical, 'commentary': results,
                   'commentary_evidence': selected_evidence,
                   'commentary_requests': {w: r['key'] for w, r in cited_requests.items()},
                   'released_at': utc(), 'frozen': True}
        payload['reporting_schedule'] = reporting_days.schedule(self.config)
        if not numerical.get('origin'):
            payload['retained_reports'] = {'yesterday': reporting_days.yesterday(
                self.folder, numerical, list(self.summary_index.values()), self.config)}
        atomic(folder/'publication.json', payload)
        atomic(folder/'ready-publication.json', {'schema_version': 1,
               'forecast_id': numerical['forecast_id'], 'sha256': fingerprint(payload)})

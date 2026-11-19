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

from . import activity, commentary, comparisons, reporting_days
from .storage import atomic, fingerprint, read


def utc(seconds=None):
    return datetime.fromtimestamp(time.time() if seconds is None else seconds, timezone.utc).isoformat()


def public_run(config):
    # Use an allow-list: local folders, credentials and API settings never cross
    # the website boundary, even if later versions add them to local settings.
    return {k: config[k] for k in ('version', 'run_id', 'election', 'kind',
            'display_name', 'visibility', 'election_date', 'timezone')}


class Processor:
    def __init__(self, folder, settings=None, client=None):
        self.folder = Path(folder)
        self.config = read(self.folder / 'run.json')
        reporting_days.schedule(self.config)  # Reject invalid reporting hours before any API call.
        self.settings = settings or {'enabled': False}
        self.minimum_count_change = self.settings.get('minimum_count_change', 10)
        if not isinstance(self.minimum_count_change, int) or self.minimum_count_change < 1:
            raise ValueError('minimum_count_change must be a positive number of votes')
        self.prompt_version = self.settings.get('prompt_version', commentary.PROMPT_VERSION)
        if self.prompt_version not in (commentary.PROMPT_VERSION, commentary.PREVIOUS_VERSION):
            raise ValueError('Unsupported commentary prompt version')
        self.client = client
        self.executor = ThreadPoolExecutor(max_workers=max(1, min(8, self.settings.get('concurrency', 4))))
        self.pending = {}
        self.budget = None
        if self.settings.get('enabled'):
            self.budget = commentary.Budget(self.folder/'commentary-budget.sqlite', self.settings['spending_limit_usd'])
        self.status = {}
        self.closing = False
        self.summary_index = {}
        self.released = set()
        self.load_summary = lru_cache(maxsize=16)(lambda identity: read(self.folder/'packages'/identity/'summary.json'))

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
        atomic(result_path.with_suffix('.input.json'), packet)
        self.pending[key] = (self.executor.submit(self.perform, packet, deadline, cache_overview), result_path)

    def perform(self, packet, deadline, cache_overview=False):
        errors = []
        maximum = max(1, min(3, self.settings.get('attempts', 2)))
        for attempt in range(maximum):
            if time.time() >= deadline:
                break
            try:
                settings = dict(self.settings, cache_overview=cache_overview)
                result = commentary.generate(packet, settings, self.budget, deadline, self.client)
                result.update(finished_at=utc(), attempts=attempt+1)
                return result
            except Exception as error:
                errors.append(str(error))
                # Budget/configuration failures cannot be improved by retrying.
                if isinstance(error, (ValueError, KeyError)) and ('budget' in str(error).lower() or self.client is None and not self.settings.get('model')):
                    break
                if attempt+1 < maximum:
                    time.sleep(max(0, min(1, deadline-time.time())))
        return {'status': 'unavailable', 'reason': errors[-1] if errors else 'Commentary deadline reached in queue.',
                'errors': errors, 'finished_at': utc()}

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
                # Write summary last. It is also the acknowledgement that local
                # explanatory evidence has been fully materialised.
                evidence = comparisons.evidence(numerical, self.folder)
                full = comparisons.summary(numerical)
                if summaries:
                    previous = read(self.folder/'packages'/summaries[-1]['forecast_id']/'evidence.json.gz')
                    full['count_activity_since_previous'] = activity.count_changes(previous, evidence)
                atomic(package/'evidence.json.gz', evidence)
                atomic(package/'summary.json', full)
            full = self.load_summary(package.name)
            # Reversal detection needs every earlier winning chance, but only
            # actual comparison anchors need all percentile/candidate accounts.
            # Keep that small index in memory and bound the larger summary cache.
            compact = {k: full[k] for k in ('forecast_id', 'source_time', 'input_revision', 'origin')}
            compact['count_activity_since_previous'] = full.get('count_activity_since_previous', {})
            compact['seats'] = {s: {'win_percent': row['win_percent']} for s, row in full['seats'].items()}
            self.summary_index[package.name] = compact
            summaries.append(compact)
        return summaries

    def tick(self):
        self.collect()
        updated = read(self.folder/'run.json')
        if public_run(updated) != public_run(self.config):
            raise ValueError('Registered run identity changed; create a new run instead')
        reporting_days.schedule(updated)
        self.config = updated
        paths = sorted((self.folder/'packages').glob('*/ready-numerical.json'),
                       key=lambda p: (p.parent.name != 'origin', p.parent.name))
        summaries = self.prepare(paths)
        self.status = {'updated_at': utc(), 'packages': {}, 'pending_commentary_requests': len(self.pending)}
        for path in paths:
            package = path.parent
            if package.name in self.released or (package/'ready-publication.json').exists():
                self.released.add(package.name)
                self.status['packages'][package.name] = 'released'
                continue
            numerical = read(package/'numerical.json')
            if package.name == 'origin':
                atomic(package/'requests.json', {})
                self.release(package, numerical, {}, {})
                self.released.add(package.name)
                self.status['packages'][package.name] = 'released reference'
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
                    lambda identity: read(self.folder/'packages'/identity/'evidence.json.gz'), self.config, self.load_summary)
                requests = {}
                mechanical = {}
                for window, group in groups.items():
                    mechanical[window] = {k: v for k, v in group.items() if k not in ('evidence', 'seat_changes')}
                    if not group['available']:
                        continue
                    mechanical[window]['seat_changes'] = [
                        {'seat': row['seat'], 'changes': row['changes']} for row in group['seat_changes']]
                    mechanical[window]['commentary_decision'] = activity.decision(group, self.minimum_count_change)
                    if mechanical[window]['commentary_decision'] != 'request':
                        continue
                    packet = comparisons.prompt_packet(current, group)
                    packet['source_timezone'] = self.config['timezone']
                    key = fingerprint({'packet': packet, 'settings': self.settings, 'prompt': self.prompt_version})
                    requests[window] = {'key': key, 'packet': packet}
                atomic(package/'requests.json', requests)
                atomic(prepared, mechanical)
            mechanical, requests = read(prepared), read(package/'requests.json')
            results = {}
            for window, group in mechanical.items():
                local_text = activity.notice(group, window)
                if local_text:
                    results[window] = local_text
            # Several windows can use the same reference and therefore the
            # same request. Only distinct comparisons can reuse this overview.
            cache_overview = len({request['key'] for request in requests.values()}) > 1
            preset_changed = set()
            for window, request in requests.items():
                result_path = package/'commentary'/f"{request['key']}.json"
                if result_path.exists():
                    result = read(result_path)
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
                                         or not self.settings.get('enabled')) for w in requests)
            if ready or time.time() >= deadline:
                for window in mechanical:
                    if not mechanical[window]['available']:
                        results[window] = {'status': 'unavailable', 'reason': mechanical[window]['reason']}
                    elif window not in results:
                        results[window] = {'status': 'unavailable', 'reason': 'Commentary publication deadline reached.'}
                self.release(package, numerical, mechanical, results)
                self.released.add(package.name)
                self.status['packages'][package.name] = 'released'
            else:
                self.status['packages'][package.name] = 'waiting for commentary'
        atomic(self.folder/'reporting-status.json', self.status)
        return self.status

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
            if group.get('reference_source_time'):
                group['reference_source_time'] = comparisons.instant(group['reference_source_time'], self.config['timezone']).isoformat()
            flow_reference = group.get('statewide_preference_changes')
            if flow_reference and flow_reference.get('reference_source_time'):
                flow_reference['reference_source_time'] = comparisons.instant(
                    flow_reference['reference_source_time'], self.config['timezone']).isoformat()
        # Numerical units and labels are part of the public contract. The complete
        # diagnostics stay local; cited evidence is included only in compact form.
        requests = read(folder/'requests.json')
        selected_evidence = {r['key']: r['packet']['evidence'] for r in requests.values()}
        payload = {'schema_version': 1, 'run': public_run(self.config), 'forecast': metadata,
                   'report': numerical['report'], 'comparisons': mechanical, 'commentary': results,
                   'commentary_evidence': selected_evidence,
                   'commentary_requests': {w: r['key'] for w, r in requests.items()},
                   'released_at': utc(), 'frozen': True}
        payload['reporting_schedule'] = reporting_days.schedule(self.config)
        if not numerical.get('origin'):
            payload['retained_reports'] = {'yesterday': reporting_days.yesterday(
                self.folder, numerical, list(self.summary_index.values()), self.config)}
        atomic(folder/'publication.json', payload)
        atomic(folder/'ready-publication.json', {'schema_version': 1,
               'forecast_id': numerical['forecast_id'], 'sha256': fingerprint(payload)})

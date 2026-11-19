"""Translate completed forecasts into comparisons with explicit electoral units.

Win chances are percentages, and their differences are percentage points. TCP
estimates are conditional on a named pair: changing the opponent never turns
one margin into a comparable margin against another opponent. Diagnostic
changes are evidence about the model's response, not a causal decomposition
of the change in winning probability.
"""
from datetime import datetime, timedelta, timezone
import math
from pathlib import Path, PureWindowsPath
import sys
from .timezones import ZoneInfo

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'analysis'))
from lib.live_analysis_archive import load_json, analysis_path_for_snapshot
from .activity import count_changes
from . import count_context


def pairs(value):
    if isinstance(value, dict):
        return {str(k): v for k, v in value.items()}
    return {('/'.join(map(str, k)) if isinstance(k, list) else str(k)): v
            for k, v in (value or [])}


def instant(text, zone):
    result = datetime.fromisoformat(text.replace('Z', '+00:00'))
    if result.tzinfo is None:
        result = result.replace(tzinfo=ZoneInfo(zone))
    return result.astimezone(timezone.utc)


def bands(values, thresholds):
    if not values:
        return None
    if len(values) != len(thresholds):
        raise ValueError('Probability bands lack matching percentile thresholds')
    return {format(float(q), 'g'): x for q, x in zip(thresholds, values)}


def _at(values, index, default=None):
    return values[index] if index < len(values) else default


def summary(numerical):
    report = numerical['report']
    thresholds = report.get('probabilityBands', report.get('voteTotalThresholds', []))
    parties = pairs(report.get('partyAbbr', {}))
    seats = {}
    for i, name in enumerate(report['seatNames']):
        candidates = pairs(_at(report.get('seatCandidateNames', []), i, {}))
        seats[name] = {
            'holder': {'party_id': str(_at(report.get('seatIncumbents', []), i)),
                       'margin': _at(report.get('seatMargins', []), i)},
            'candidates': {p: {'party': parties.get(p, p), 'name': candidates.get(p)}
                           for p in pairs(_at(report['seatPartyWinFrequencies'], i, {}))},
            'win_percent': pairs(_at(report['seatPartyWinFrequencies'], i, {})),
            'fp_percentiles': {p: bands(v, thresholds) for p, v in
                               pairs(_at(report.get('seatFpBands', []), i, {})).items()},
            'fp_mean_percent': pairs(_at(report.get('seatMeanFp', []), i, {})),
            'tcp_percentiles': {p: bands(v, thresholds) for p, v in
                                pairs(_at(report.get('seatTcpBands', []), i, {})).items()},
            # SavedReport stores pairing frequencies as proportions, unlike
            # party winning chances. Convert here, once, for public comparisons.
            'pairing_percent': {p: v * 100 for p, v in
                                pairs(_at(report.get('seatTcpScenarios', []), i, {})).items()},
            'tpp_percentiles': bands(_at(report.get('seatTppBands', []), i), thresholds),
        }
    statewide = {k: pairs(report.get(k, {})) for k in
        ('expectedSeats', 'majorityWinPc', 'minorityWinPc', 'mostSeatsWinPc', 'overallWinPc')}
    statewide.update({k: report.get(k) for k in ('hungParliamentPc', 'coalitionExpectedSeats', 'countProgress')})
    statewide['tpp_percentiles'] = bands(report.get('tppFrequencies'), report.get('voteTotalThresholds', []))
    statewide['fp_percentiles_by_party'] = {p: bands(v, report.get('voteTotalThresholds', []))
        for p, v in pairs(report.get('fpFrequencies', {})).items()}
    if report.get('reportMode') == 'LF' and report.get('statewidePreferenceFlows'):
        medians = {}
        for row in report['statewidePreferenceFlows']:
            key = '/'.join(str(row[field]) for field in ('sourceParty', 'destinationParty', 'otherFinalist'))
            levels = bands(row['percentiles'], report['voteTotalThresholds'])
            medians[key] = levels['50']
        statewide['preference_flow_medians'] = medians
    return {'forecast_id': numerical['forecast_id'], 'source_time': numerical['source_time'],
            'input_revision': numerical['input_revision'], 'origin': numerical.get('origin', False),
            'seats': seats, 'statewide': statewide,
            'parties': parties}


def _party_values(rows):
    return {str(row['party_index']): row.get('value') for row in (rows or [])
            if isinstance(row, dict) and 'party_index' in row and 'value' in row}


def _shared_evidence(row):
    """Keep the shared estimates which can move a seat without a new local count.

    The complete diagnostic also holds matrices, candidate rosters and repeated
    zero adjustments. They remain in the original archive. Here we keep the
    estimates and their evidence strength, with transformed adjustments labelled
    explicitly so they cannot be reported as percentage-point vote changes.
    """
    node = row.get('node', {})
    result = {k: row.get(k) for k in ('name', 'projected_2pp') if k in row}
    result['transformed_share_adjustments_not_percentage_points'] = {
        'tpp': row.get('final_specific_tpp_deviation'),
        'fp': {str(p['party_index']): p['value'] for p in row.get('final_specific_fp_deviations', [])
               if p.get('abbreviation')},
    }
    result['confidence_and_completion_proportions'] = {k: node.get(k) for k in
        ('fp_completion', 'fp_confidence', 'tpp_completion', 'tpp_confidence', 'tcp_completion')}
    result['projected_fp_votes_for_named_parties'] = {str(p['party_index']): p['value']
        for p in node.get('fp_votes_projected', []) if p.get('abbreviation')}
    result['projected_tpp_votes'] = _party_values(node.get('tpp_votes_projected'))
    return result


def _category_evidence(biases):
    result = {}
    sources = [('tpp', biases.get('tpp', {}))]
    sources += [(f"fp/{row['party_index']}", row) for row in biases.get('fp', [])]
    for quantity, source in sources:
        for kind in ('booth_type', 'vote_type'):
            for row in source.get(kind, []):
                result[f"{quantity}/{kind}/{row['category']}"] = dict(row,
                    quantity=quantity, category_kind=kind,
                    units='bias/raw/std_dev are transformed vote-share adjustments, not percentage points; vote_count is votes')
    return result


def evidence(numerical, run_folder=None):
    """Retain interpretable accounts once, without forecast draws or covariance arrays.

    Counted and projected candidate votes use the same party identity. Their
    difference describes the estimated vote still to come; it is not an observed
    batch. Shared preference offsets are diagnostics on the model's log-odds
    scale, so they are labelled separately from percentage-point outcomes.
    """
    path = numerical.get('diagnostic_file')
    if not path:
        return {'booths': {}, 'seats': {}, 'preferences': {}, 'issues': [], 'statewide': {},
                'observed_counts_available': numerical.get('origin', False)}
    snapshot = Path(path)
    # The Windows GUI writes its own absolute path. A WSL worker opens
    # the same run through its mounted folder. Use the snapshot there only
    # when the handoff identifies this run as the owner of the diagnostic.
    windows_path = PureWindowsPath(path)
    if run_folder is not None and windows_path.is_absolute() and not snapshot.is_absolute():
        if windows_path.parent.name != numerical['run_id'] or windows_path.parent.parent.name != 'live_runs':
            raise ValueError('Windows diagnostic path is outside this forecast run: '+path)
        snapshot = Path(run_folder)/windows_path.name
    analysis = load_json(analysis_path_for_snapshot(snapshot))
    result = {'booths': {}, 'seats': {}, 'preferences': {}, 'issues': analysis.get('input_recovery', []),
              'observed_counts_available': 'booths' in analysis,
              'statewide': _shared_evidence(analysis.get('election', {})),
              'category_biases': _category_evidence(analysis.get('category_biases', {}))}
    result['regions'] = {r['name']: _shared_evidence(r) for r in analysis.get('regions', [])}
    result['manual_preferences'] = {r['seat']: r for r in analysis.get('manual_preference_evidence', [])}
    # These are published sample counts, whereas manual_preferences above
    # describes their use in simulation draws. Only the former can establish
    # new counting without changed FP/TCP totals (for example a Finniss 4CP).
    result['manual_count_observations'] = {}
    for row in analysis.get('preference_count_observations', []):
        result['manual_count_observations'][row['district']] = {
            'seat': row['district'],
            'transferred_votes': {p: row['sampled_preference_ballots']*share/100
                                  for p, share in row['flow_percent'].items()},
            'sampled_first_preferences': row.get('sampled_first_preferences', {}),
            'sampled_candidate_totals': row.get('sampled_candidate_totals', {})}
    district_settings = {s['name']: s for s in analysis.get('seats', [])}
    independent_parties = [p for p, abbr in pairs(numerical['report'].get('partyAbbr', {})).items()
                           if abbr == 'IND' and int(p) >= 0]
    for booth in analysis.get('booths', []):
        node = booth.get('node', {})
        key = '/'.join(str(booth.get(k, '')) for k in ('seat_name', 'booth_type', 'vote_type', 'name'))
        result['booths'][key] = {k: booth.get(k) for k in ('seat_name', 'name', 'booth_type', 'vote_type')}
        result['booths'][key].update({k: _party_values(node.get(k)) for k in
            ('fp_votes_current', 'tcp_votes_current', 'fp_votes_projected', 'tpp_votes_projected', 'tcp_votes_projected')})
        counts = result['booths'][key]
        counts['expected_remaining_fp'] = sum(counts['fp_votes_projected'].values())-sum(counts['fp_votes_current'].values())
        # Ordinary party IDs already agree. A prominent independent can instead
        # be counted under the commission's candidate ID and projected under the
        # model's Independent party. Record that explicit correspondence without
        # rewriting raw counts or combining the other independent candidates.
        district = district_settings.get(booth['seat_name'], {})
        independent = district.get('live_independent_party_index')
        if independent is not None and len(independent_parties) == 1:
            counted_id, projected_id = str(independent), independent_parties[0]
            if counted_id != projected_id and counted_id in counts['fp_votes_current'] and projected_id in counts['fp_votes_projected']:
                counts['count_to_projection_ids'] = {counted_id: projected_id}
    for seat in analysis.get('seats', []):
        result['seats'][seat['name']] = {k: seat.get(k) for k in
            ('region_name', 'estimated_tpp', 'live_preference_flow_deviation', 'fp_evidence', 'fp_integration',
             'final_specific_tpp_deviation', 'final_specific_fp_deviations',
             'fp_booth_type_sensitivity', 'tpp_booth_type_sensitivity',
             'fp_vote_type_sensitivity', 'tpp_vote_type_sensitivity')}
        result['seats'][seat['name']]['count_progress'] = {k: seat.get('node', {}).get(k) for k in
                ('fp_completion', 'tcp_completion', 'tpp_completion')}
    for row in analysis.get('preference_learning', []):
        key = f"{row['seat']}/{row['category']}/{row['pair']}"
        result['preferences'][key] = row
    return result


def numeric_changes(before, after, prefix=''):
    """Only matched numerical quantities get differences; absence is not zero."""
    if isinstance(before, (int, float)) and not isinstance(before, bool) and isinstance(after, (int, float)) and not isinstance(after, bool):
        if before != after:
            yield {'metric': prefix, 'before': before, 'after': after, 'change': after-before}
    elif isinstance(before, dict) and isinstance(after, dict):
        for key in sorted(before.keys() & after.keys()):
            yield from numeric_changes(before[key], after[key], f'{prefix}/{key}')
    elif isinstance(before, list) and isinstance(after, list) and len(before) == len(after):
        # Lists here are fixed percentile arrays, not unordered observations.
        for i, (a, b) in enumerate(zip(before, after)):
            yield from numeric_changes(a, b, f'{prefix}/{i}')


def booth_changes(events, known, minimum_revision=10):
    """Describe newly reporting units and revisions for the mechanical report.

    One unit is a polling place or declaration category. Rank by affected vote
    allocations, not net turnout: correcting 100 votes between candidates is
    still significant when the total stays fixed. FP and TCP are parallel
    accounts of ballots, so the existing activity measure avoids adding them
    twice. A first TCP count at a booth with FP already present is an update.
    """
    if not known:
        return {'available': False, 'reason': 'Observed counts are unavailable.'}
    units = []
    for event in events:
        if event['kind'] != 'booths' or not event.get('changed_votes'):
            continue
        before, after = event.get('before') or {}, event.get('after') or {}
        has_votes = lambda row: any(v > 0 for field in ('fp_votes_current', 'tcp_votes_current')
                                   for v in row.get(field, {}).values())
        kind = 'new' if not has_votes(before) and has_votes(after) else 'updated'
        if kind == 'updated' and event['changed_votes'] < minimum_revision:
            continue
        record = after or before
        units.append({'kind': kind, 'seat': event['seat'], 'name': record['name'],
                      'record': event['record'], 'evidence_id': event['id'],
                      'vote_type': record.get('vote_type'), 'booth_type': record.get('booth_type'),
                      'affected_votes': event['changed_votes'],
                      'fp_net_change': event.get('fp_vote_change', 0),
                      'tcp_net_change': event.get('tcp_vote_change', 0)})
    units.sort(key=lambda row: (-row['affected_votes'], row['seat'], row['name']))

    def group(rows, kind, limit):
        matches = [r for r in rows if r['kind'] == kind]
        shown, others = matches[:limit], max(0, len(matches)-limit)
        names = [r['name'] for r in shown]
        if others:
            names.append(f'{others} other' + ('s' if others != 1 else ''))
        text = ', '.join(names[:-1])+' and '+names[-1] if len(names) > 1 else ''.join(names)
        return {'total': len(matches), 'shown': shown, 'other_count': others, 'text': text}

    def groups(rows):
        return {'new': group(rows, 'new', 3), 'updated': group(rows, 'updated', 2)}

    # The website retains every first return and every correction affecting more
    # than twenty votes. Keep these identities before the display lists are capped.
    fields = ('kind', 'record', 'name', 'seat', 'affected_votes', 'fp_net_change', 'tcp_net_change')
    retained = [{key: unit[key] for key in fields} for unit in units
                if unit['kind'] == 'new' or unit['affected_votes'] > 20]
    return {'available': True, 'retained_events': retained,
            'minimum_revision_votes': minimum_revision, **groups(units),
            'by_seat': {s: groups([r for r in units if r['seat'] == s]) for s in sorted({r['seat'] for r in units})}}


def tcp_against_previous_forecast(previous, current):
    """Explain a changed booth TCP count using the reference forecast's estimate.

    The usual case is a new or revised count for the same two candidates that
    the comparison's reference forecast projected. A classic Labor-Coalition contest may have
    a TPP projection instead; use that only when its candidate identities match
    exactly. After realignment, do not compare different opponents or invent a
    forecast for a pairing that had no matching estimate.

    Compare cumulative shares, not raw totals: 83 of 133 votes is compared with
    the previous projected share even if that projection covered 135 votes.
    Also express the difference at the current count size, so a few votes in a
    small booth are not mistaken for a large electoral development. These are
    descriptive comparisons, not uncertainty tests or effects on seat win odds.
    Return None when the reported count has not changed; missing comparisons
    have an explicit reason, rather than a zero difference implying agreement.
    """
    previous, current = previous or {}, current or {}
    reported = current.get('tcp_votes_current') or {}
    if reported == (previous.get('tcp_votes_current') or {}):
        return None
    if len(reported) != 2 or any(not math.isfinite(v) or v < 0 for v in reported.values()) or sum(reported.values()) <= 0:
        return {'available': False, 'reason': 'No positive current TCP count with two candidate identities.'}

    # The explicit alias identifies a prominent independent in the count and
    # projection. Never merge two counted candidates merely because their party
    # label is shared: that would turn a two-candidate comparison into one party.
    aliases = dict(previous.get('count_to_projection_ids', {}))
    aliases.update(current.get('count_to_projection_ids', {}))
    measured = {aliases.get(p, p): votes for p, votes in reported.items()}
    if len(measured) != 2:
        return {'available': False, 'reason': 'The two counted candidates do not have distinct projection identities.'}

    expectation = None
    expectation_kind = None
    for field, kind in [('tcp_votes_projected', 'TCP'), ('tpp_votes_projected', 'TPP')]:
        projected = previous.get(field) or {}
        if projected.keys() != measured.keys():
            continue
        if any(not math.isfinite(v) or v < 0 for v in projected.values()) or sum(projected.values()) <= 0:
            continue
        expectation, expectation_kind = projected, kind
        break
    if expectation is None:
        return {'available': False, 'reason': 'The reference forecast has no positive booth estimate for this exact pair.'}

    counted_total = sum(measured.values())
    projected_total = sum(expectation.values())
    shares = {}
    for party in sorted(measured):
        observed_share = measured[party]/counted_total
        expected_share = expectation[party]/projected_total
        shares[party] = {'observed_percent': 100*observed_share,
                         'expected_percent': 100*expected_share,
                         'difference_pp': 100*(observed_share-expected_share),
                         'equivalent_vote_difference': measured[party]-counted_total*expected_share}
    return {'available': True, 'pair': sorted(measured), 'counted_votes': counted_total,
            'expectation_kind': expectation_kind, 'shares': shares}


def finalist_probabilities(seat):
    """Sum mutually exclusive pairings to get each candidate's finalist chance."""
    result = {}
    for pairing, chance in seat.get('pairing_percent', {}).items():
        for party in pairing.split('/'):
            result[party] = result.get(party, 0)+chance
    return result


def _preference_changes(current, reference):
    """Compare the same original party and two finalists across live reports.

    The full intervals are in each numerical report. Only the medians are
    needed to describe a change. A missing row has no earlier level.
    """
    before = reference['statewide'].get('preference_flow_medians', {})
    after = current['statewide'].get('preference_flow_medians', {})
    changes = []
    for key in before.keys() & after.keys():
        parties = key.split('/')
        if not all(p in current['parties'] and current['parties'][p] == reference['parties'].get(p) for p in parties):
            continue
        changes.append({'parties': parties, 'before': before[key], 'after': after[key],
                        'change_pp': after[key]-before[key]})
    return sorted(changes, key=lambda row: (-abs(row['change_pp']), row['parties']))


def compare(current, previous, history, current_evidence, previous_evidence, zero_count_flows=None):
    rows = []
    for seat in sorted(current['seats'].keys() & previous['seats'].keys()):
        after, before = current['seats'][seat], previous['seats'][seat]
        for party in sorted(before['win_percent'].keys() | after['win_percent'].keys()):
            old, new = before['win_percent'].get(party, 0), after['win_percent'].get(party, 0)
            points = [h['seats'].get(seat, {}).get('win_percent', {}).get(party, 0) for h in history]
            points += [old, new]
            rows.append({'seat': seat, 'party_id': party, 'identity': after['candidates'].get(party),
                         'before': old, 'after': new, 'change_pp': new-old,
                         'minimum_within_window': min(points), 'maximum_within_window': max(points),
                         'identity_changed': before['candidates'].get(party) != after['candidates'].get(party)})
    rows.sort(key=lambda r: (-abs(r['change_pp']), r['seat'], r['party_id']))
    details = []
    for seat in sorted(current['seats'].keys() & previous['seats'].keys()):
        values = list(numeric_changes(previous['seats'][seat], current['seats'][seat]))
        values += list(numeric_changes(finalist_probabilities(previous['seats'][seat]),
                                       finalist_probabilities(current['seats'][seat]), '/finalist_percent'))
        details.append({'seat': seat, 'changes': values,
                        'before': previous['seats'][seat], 'after': current['seats'][seat]})
    events = []
    for kind in ('booths', 'seats', 'preferences', 'regions', 'manual_preferences', 'manual_count_observations', 'category_biases'):
        old, new = previous_evidence.get(kind, {}), current_evidence.get(kind, {})
        for key in sorted(old.keys() | new.keys()):
            if old.get(key) == new.get(key):
                continue
            event = {'id': f'e{len(events)}', 'kind': kind, 'record': key,
                           'before': old.get(key), 'after': new.get(key),
                           'interpretation': 'Changed evidence or model estimate; not an isolated causal effect.'}
            if kind in ('booths', 'seats', 'preferences', 'manual_preferences', 'manual_count_observations'):
                record = new.get(key) or old.get(key) or {}
                event['seat'] = record.get('seat_name', record.get('seat', key if kind in ('seats', 'manual_preferences') else None))
            if kind in ('booths', 'manual_count_observations'):
                event['changed_votes'] = count_changes(
                    {'observed_counts_available': True, kind: {key: old.get(key) or {}}},
                    {'observed_counts_available': True, kind: {key: new.get(key) or {}}})['changed_votes']
            if kind == 'booths' and old.get(key) and new.get(key):
                event['fp_vote_change'] = sum(new[key]['fp_votes_current'].values())-sum(old[key]['fp_votes_current'].values())
                event['tcp_vote_change'] = sum(new[key]['tcp_votes_current'].values())-sum(old[key]['tcp_votes_current'].values())
                event['pair_changed'] = new[key]['tcp_votes_current'].keys() != old[key]['tcp_votes_current'].keys()
                # Compare the new cumulative composition with what the previous
                # forecast expected at that booth. This describes how the share
                # differs from expectations, not its effect on the seat's win chance.
                forecast = old[key]['fp_votes_projected']
                aliases = new[key].get('count_to_projection_ids', {})
                measured = {}
                for party, votes in new[key]['fp_votes_current'].items():
                    mapped = aliases.get(party, party)
                    measured[mapped] = measured.get(mapped, 0)+votes
                fp_changed = new[key]['fp_votes_current'] != old[key]['fp_votes_current']
                if fp_changed and sum(forecast.values()) > 0 and sum(measured.values()) > 0:
                    event['fp_share_minus_previous_expectation_pp'] = {
                        p: 100*(measured[p]/sum(measured.values())-forecast[p]/sum(forecast.values()))
                        for p in measured.keys() & forecast.keys()}
            elif kind == 'booths':
                # Newly introduced or removed units have no matching booth
                # expectation, but their actual net count change is still known.
                before, after = old.get(key) or {}, new.get(key) or {}
                event['fp_vote_change'] = sum(after.get('fp_votes_current', {}).values())-sum(before.get('fp_votes_current', {}).values())
                event['tcp_vote_change'] = sum(after.get('tcp_votes_current', {}).values())-sum(before.get('tcp_votes_current', {}).values())
            if kind == 'booths':
                tcp_comparison = tcp_against_previous_forecast(old.get(key), new.get(key))
                if tcp_comparison is not None:
                    event['tcp_against_previous_forecast'] = tcp_comparison
            events.append(event)
    old_issues = previous_evidence.get('issues', [])
    for issue in current_evidence.get('issues', []):
        if issue not in old_issues:
            events.append({'id': f'e{len(events)}', 'kind': 'input_recovery', 'after': issue, 'seat': issue.get('district')})
    for kind in ('statewide',):
        if previous_evidence.get(kind) != current_evidence.get(kind):
            events.append({'id': f'e{len(events)}', 'kind': kind,
                           'before': previous_evidence.get(kind), 'after': current_evidence.get(kind)})
    counts = count_changes(previous_evidence, current_evidence)
    # A correction followed by a restoration must not be labelled "no counting"
    # just because the endpoints match. Saved interval accounts cover such
    # changes without rereading all historical booth diagnostics per window.
    intervals = [s.get('count_activity_since_previous', {}) for s in history
                 if s['forecast_id'] != previous['forecast_id']]
    intervals.append(current.get('count_activity_since_previous', {}))
    if counts.get('known') and intervals and all(v.get('known') for v in intervals):
        counts['endpoint_changed_votes'] = counts['changed_votes']
        counts['changed_votes'] = max(counts['changed_votes'], sum(v['changed_votes'] for v in intervals))
    result = {'available': True, 'reference_forecast_id': previous['forecast_id'],
            'count_activity': counts,
            'reference_source_time': previous['source_time'],
            'input_revision_changed': current['input_revision'] != previous['input_revision'],
            'win_changes': rows, 'seat_changes': details, 'evidence': events,
            'category_transfers': count_context.category_transfers(events) if counts.get('known') else [],
            'close_tcp_counts': count_context.close_tcp_changes(previous_evidence, current_evidence, details),
            'booth_changes': booth_changes(events, counts.get('known', False)),
            'statewide': {side: {k:v for k,v in item['statewide'].items() if k != 'preference_flow_medians'}
                          for side, item in [('before', previous), ('after', current)]}}
    reference = zero_count_flows if previous.get('origin') and zero_count_flows is not None else previous
    changes = _preference_changes(current, reference)
    if changes:
        result['statewide_preference_changes'] = {'rows': changes, 'reference_forecast_id': reference['forecast_id'],
            'reference_source_time': reference['source_time'], 'from_first_zero_count': reference is not previous}
    return result


def comparisons(current, summaries, load_evidence, config, load_summary=None):
    zone = config['timezone']
    now = instant(current['source_time'], zone)
    earlier = [s for s in summaries if (s.get('origin') or s['forecast_id'] < current['forecast_id'])
               and s['forecast_id'] != current['forecast_id']
               and (s.get('origin') or instant(s['source_time'], zone) <= now)]
    earlier.sort(key=lambda s: (datetime.min.replace(tzinfo=timezone.utc) if s.get('origin') else instant(s['source_time'], zone), s['forecast_id']))
    origins = [s for s in earlier if s.get('origin')]
    counted = [s for s in earlier if not s.get('origin')]
    # The separate pre-election reference has no public live-flow table. For
    # this quantity alone, a genuine first capture with zero FP AND TCP can
    # establish the start. Record its identity explicitly. A first capture
    # that already has votes cannot establish the missing starting level.
    zero_count_flows = None
    if origins and counted and current['statewide'].get('preference_flow_medians'):
        first = load_summary(counted[0]['forecast_id']) if load_summary else counted[0]
        progress = first.get('statewide', {}).get('countProgress') or {}
        if progress.get('fp_votes') == 0 and progress.get('tcp_votes') == 0 and first['input_revision'] == origins[0]['input_revision']:
            zero_count_flows = first
    local_cutoff = datetime.fromisoformat(config['election_date']).replace(
        hour=config.get('night_end_hour', 6), tzinfo=ZoneInfo(zone)) + timedelta(days=1)
    from . import reporting_days
    day = reporting_days.period(now, config)
    election_night = now < local_cutoff
    windows = {'15m': 900, '1h': 3600} if election_night else ({'3h': 10800} if day['three_hour_enabled'] else {})
    anchors = {'previous': counted[-1] if counted else (origins[0] if origins else None),
               'since_start': origins[0] if origins else None}
    if not election_night:
        # Everyone checking during this reporting day sees changes against the
        # same 5 am boundary, using the latest actual forecast before it. A
        # capture stamped exactly 5 am belongs to the new day, so it cannot
        # become a different starting point for later reports that morning.
        # A missing morning capture is not interpolated or replaced by origin.
        eligible = [s for s in counted if instant(s['source_time'], zone) < day['start']]
        anchors['today'] = eligible[-1] if eligible else None
    for name, seconds in windows.items():
        eligible = [s for s in counted if instant(s['source_time'], zone) <= now-timedelta(seconds=seconds)]
        anchors[name] = eligible[-1] if eligible else None
    result, cache = {}, {}
    for name, anchor in anchors.items():
        daily_metadata = ({'report_date': day['report_date'], 'reporting_timezone': day['timezone'],
                           'requested_reference_time': day['start'].isoformat()} if name == 'today' else {})
        if anchor is None:
            result[name] = dict(available=False, reason='No forecast at or before the required reference time.', **daily_metadata)
            continue
        identity = anchor['forecast_id']
        if identity not in cache:
            history = [s for s in earlier if anchor.get('origin') or
                       not s.get('origin') and instant(s['source_time'], zone) >= instant(anchor['source_time'], zone)]
            complete_anchor = load_summary(identity) if load_summary else anchor
            cache[identity] = compare(current, complete_anchor, history,
                                      load_evidence(current['forecast_id']), load_evidence(identity), zero_count_flows)
        elapsed = None if not anchor['source_time'] else (now-instant(anchor['source_time'], zone)).total_seconds()
        result[name] = dict(cache[identity], actual_seconds=elapsed,
                            requested_seconds=windows.get(name), **daily_metadata)
    return result


def _pair_likelihood(seat, pair):
    # A pair that was plausible at either endpoint can be relevant to the story.
    return max(seat['before']['pairing_percent'].get(pair, 0), seat['after']['pairing_percent'].get(pair, 0))/100


def _tcp_interval_rows(seat):
    """Choose the district's most consequential changed TCP interval for prose.

    Each range describes the first candidate's share conditional on one exact
    pairing. Rank pairs by the larger movement of their two bounds, weighted
    by how likely that contest was. Read both bounds from the two forecasts
    so the writer also sees an unchanged bound. If historical data permit
    only a one-sided comparison, give it only when no complete range qualifies.
    """
    before = seat['before'].get('tcp_percentiles') or {}
    after = seat['after'].get('tcp_percentiles') or {}
    best, best_score = [], None
    for pair in sorted(before.keys() & after.keys()):
        # A slot such as the second independent can start representing a
        # different person. Two named opponents must still describe the
        # same contest before we compare its range across the update.
        same_contest = True
        for party in pair.split('/'):
            old_name = seat['before'].get('candidates', {}).get(party, {}).get('name')
            new_name = seat['after'].get('candidates', {}).get(party, {}).get('name')
            if old_name and new_name and old_name != new_name:
                same_contest = False
                break
        if not same_contest:
            continue
        rows = []
        for percentile in ('5', '95'):
            old = (before[pair] or {}).get(percentile)
            new = (after[pair] or {}).get(percentile)
            if old is not None and new is not None:
                rows.append({'metric': f'/tcp_percentiles/{pair}/{percentile}',
                             'before': old, 'after': new, 'change': new-old})
        movement = max((abs(r['change']) for r in rows), default=0)
        if movement:
            score = (len(rows), movement * _pair_likelihood(seat, pair))
            if best_score is None or score > best_score:
                best, best_score = rows, score
    return best


def prompt_packet(current, comparison):
    """Select by probability, close-pair margin and uncertainty, not wins alone.

    All seats remain visible in the small overview. Detailed event truncation is
    explicit, so an LLM cannot mistake a compact selection for complete causality.
    """
    scores = {}
    for row in comparison['win_changes']:
        reversal = row['maximum_within_window']-row['minimum_within_window']-abs(row['change_pp'])
        scores[row['seat']] = max(scores.get(row['seat'], 0), abs(row['change_pp']), reversal)
    for seat in comparison['seat_changes']:
        for row in seat['changes']:
            if '/tcp_percentiles/' in row['metric'] and abs(row['after']-50) < 10:
                pair = '/'.join(row['metric'].split('/')[2:4])
                likelihood = _pair_likelihood(seat, pair)
                scores[seat['seat']] = max(scores.get(seat['seat'], 0), 3*abs(row['change'])*likelihood)
            elif '/pairing_percent/' in row['metric']:
                scores[seat['seat']] = max(scores.get(seat['seat'], 0), abs(row['change']))
    # Editorial attention also includes corrections in safe seats. A large
    # candidate reallocation can leave both total votes and winning chances
    # unchanged. For this selection only, 100 affected ballots have the same
    # priority as a one-point chance movement, capped at twenty points so one
    # very large booth cannot hide every close contest. This is a configurable-
    # in-code starting editorial convention, not a forecast weight.
    for event in comparison['evidence']:
        if event.get('seat') and event.get('changed_votes', 0):
            seat = event['seat']
            scores[seat] = max(scores.get(seat, 0), min(20, event['changed_votes']/100))
    ranked_seats = sorted(scores, key=lambda s: (-scores[s], s))
    count_changes_by_seat = {}
    observed_changes = {}
    for event in comparison['evidence']:
        if event.get('seat') and event.get('changed_votes', 0):
            seat = event['seat']
            count_changes_by_seat[seat] = count_changes_by_seat.get(seat, 0)+event['changed_votes']
            totals = observed_changes.setdefault(seat, {'affected_votes': 0, 'fp_net_change': 0, 'tcp_net_change': 0})
            totals['affected_votes'] += event['changed_votes']
            for field, quantity in [('fp_votes_current', 'fp_net_change'), ('tcp_votes_current', 'tcp_net_change')]:
                before = (event.get('before') or {}).get(field, {})
                after = (event.get('after') or {}).get(field, {})
                totals[quantity] += sum(after.values())-sum(before.values())
    # Reserve some attention for the source of an update. Early counting can
    # move many uncounted marginal seats more than the safe seats that actually
    # reported; a report still needs the latter to explain the former.
    transfers = comparison.get('category_transfers', [])
    for transfer in transfers:
        # Both records appeared above. For editorial priority, count the
        # movement once, just as FP and TCP can describe the same ballots.
        count_changes_by_seat[transfer['seat']] -= max(transfer['fp_votes'], transfer['tcp_votes'])
    observed_seats = sorted(count_changes_by_seat, key=lambda s: (-count_changes_by_seat[s], s))[:4]
    selection_order = list(dict.fromkeys(ranked_seats[:6]+observed_seats+ranked_seats))
    selected = set(selection_order[:10])
    # Describe a matched relabelling once, before spending the limited booth
    # evidence allowance. The full mechanical accounts keep both entries.
    matched_ids = {identity for t in transfers for identity in t['evidence_ids']}
    events = [e for e in comparison['evidence'] if e['id'] not in matched_ids
              and (e['kind'] in ('statewide', 'category_biases', 'regions') or e.get('seat') in selected)]
    def priority(event):
        if event['kind'] == 'statewide':
            return 110000
        if event['kind'] in ('manual_preferences', 'manual_count_observations', 'statewide', 'regions'):
            return 100000
        if event.get('pair_changed'):
            return 90000
        if event.get('changed_votes', 0):
            return 80000+event['changed_votes']
        if event['kind'] == 'preferences':
            row = event.get('after') or event.get('before') or {}
            return 5000+abs(row.get('adjusted_first_votes_from_pending_fp', 0)-row.get('prior_first_votes_from_pending_fp', 0))
        if event['kind'] == 'seats':
            return 4000
        if event['kind'] == 'category_biases':
            return abs((event.get('after') or {}).get('vote_count', 0)) / 100
        if event['kind'] == 'input_recovery':
            return abs(event['after'].get('impact_votes') or 0)
        if event['kind'] == 'booths' and event.get('before') is None:
            return sum((event.get('after') or {}).get('fp_votes_current', {}).values())
        return max(event.get('changed_votes', 0),
                   abs(event.get('fp_vote_change', 0))+abs(event.get('tcp_vote_change', 0)))
    events.sort(key=priority, reverse=True)
    # Limit evidence by bytes as well as record count: a statewide diagnostic can
    # be much larger than a booth update. Preserve a visible omission count.
    import json
    included, used, per_seat, by_kind, seat_kinds, shared_bytes = [], 0, {}, {}, {}, 0
    # Interleave district accounts so a safe but numerically large electorate
    # cannot consume all the explanatory space before another selected seat.
    ordered, remaining = [], events[:]
    for round_number in range(4):
        for seat in sorted(selected, key=lambda s: (-scores[s], s)):
            event = next((e for e in remaining if e.get('seat') == seat), None)
            if event:
                ordered.append(event); remaining.remove(event)
    # Give recorded changes space before interpretations. Otherwise ten seat
    # projections can fill the evidence allowance while omitting their cause.
    observed = [e for e in ordered if e.get('changed_votes', 0)]
    events = observed+[e for e in remaining if not e.get('seat')]+ordered+remaining
    seen_ids = set()
    for event in events:
        if event['id'] in seen_ids:
            continue
        seen_ids.add(event['id'])
        seat = event.get('seat')
        if seat and per_seat.get(seat, 0) >= 4:
            continue
        if seat and event['kind'] != 'booths' and (seat, event['kind']) in seat_kinds:
            continue
        if event['kind'] == 'category_biases' and by_kind.get('category_biases', 0) >= 6:
            continue
        if event['kind'] == 'seats':
            # A seat account contains many repeated arrays. Supply the actual
            # changes in its evidence/uncertainty rather than two full copies.
            event = {k: v for k, v in event.items() if k not in ('before', 'after')}
            source = next(e for e in events if e['id'] == event['id'])
            event['diagnostic_changes'] = sorted(numeric_changes(source.get('before'), source.get('after')),
                key=lambda r: -abs(r['change']))[:8]
            event['units'] = 'Internal diagnostic units as named; these changes are not win-probability contributions.'
        size = len(json.dumps(event))
        if used+size > 18000 or not seat and shared_bytes+size > 5000:
            continue
        included.append(event); used += size
        if not seat:
            shared_bytes += size
        by_kind[event['kind']] = by_kind.get(event['kind'], 0)+1
        if seat:
            seat_kinds[seat, event['kind']] = True
        if seat:
            per_seat[seat] = per_seat.get(seat, 0)+1
    seat_changes = []
    for seat in comparison['seat_changes']:
        if seat['seat'] not in selected:
            continue
        # Keep one central estimate per quantity and a few tail movements which
        # describe uncertainty. Repeating all fifteen percentiles adds tokens,
        # not independent information. Full changes remain in the package.
        central = [r for r in seat['changes'] if '/pairing_percent/' in r['metric']
                   or '/fp_mean_percent/' in r['metric'] or r['metric'].endswith('/50')]
        # A large conditional margin change in a vanishingly unlikely pairing
        # should not displace evidence about plausible contests. This only
        # selects commentary context; it does not alter forecast probabilities.
        def importance(row):
            weight = 1
            if '/tcp_percentiles/' in row['metric']:
                pair = '/'.join(row['metric'].split('/')[2:4])
                weight = _pair_likelihood(seat, pair)
            return -abs(row['change'])*weight
        ranked = sorted(central, key=importance)[:4]
        ranked += _tcp_interval_rows(seat)
        ranked += [r for r in seat['changes'] if '/finalist_percent/' in r['metric']]
        seat_changes.append({'seat': seat['seat'], 'changes': ranked})
    def central_statewide(row):
        result = dict(row)
        result['tpp_percentiles'] = {q: v for q, v in (row.get('tpp_percentiles') or {}).items() if q in ('5', '50', '95')}
        result['fp_percentiles_by_party'] = {p: {q: v for q, v in values.items() if q in ('5', '50', '95')}
            for p, values in row.get('fp_percentiles_by_party', {}).items() if values}
        return result
    packet = {'forecast_id': current['forecast_id'], 'source_time': current['source_time'],
            'reference_forecast_id': comparison['reference_forecast_id'],
            'reference_source_time': comparison['reference_source_time'],
            'actual_seconds': comparison.get('actual_seconds'),
            'input_revision_changed': comparison['input_revision_changed'],
            'count_activity': comparison.get('count_activity'),
            'observed_count_changes_by_seat': observed_changes,
            'category_transfers': [{k:v for k,v in t.items() if k != 'evidence_ids'} for t in transfers],
            'close_tcp_counts': {s:v for s,v in comparison.get('close_tcp_counts', {}).items() if s in selected},
            'parties': current['parties'],
            'units': 'Vote shares and winning chances are percentages; changes are percentage points. TCP is conditional on its named pair.',
            'statewide': {k: central_statewide(v) for k, v in comparison['statewide'].items()},
            'all_seats': {s: {p: round(chance, 2) for p, chance in v['win_percent'].items() if chance > .005 or p in ('0', '1')}
                          for s, v in current['seats'].items()},
            'overview_precision': 'All-seat overview rounded to two decimals; selected facts retain exact exported values.',
            'candidate_identities': {s: {p: identity for p, identity in current['seats'][s]['candidates'].items()
                if current['seats'][s]['win_percent'].get(p, 0) > .1 or p in ('0', '1')} for s in sorted(selected)},
            'all_candidate_identities': {s: row['candidates'] for s, row in current['seats'].items()},
            'seat_holders': {s: row.get('holder', {}) for s, row in current['seats'].items()},
            'booth_changes': {k: v for k, v in comparison.get('booth_changes', {}).items()
                              if k != 'retained_events'},
            'win_changes': [r for r in comparison['win_changes'] if r['seat'] in selected][:24],
            'seat_changes': seat_changes,
            'evidence': included, 'omitted_evidence_records': len(comparison['evidence'])-len(included)-len(matched_ids)}
    # A conspicuous booth result can still represent a small part of its
    # district. Keep the model's current FP completion estimate for districts
    # with selected booth results, before the detailed seat diagnostics are
    # shortened. Missing progress gives no basis for calling a count small.
    booth_districts = {event['seat'] for event in included if event['kind'] == 'booths' and event.get('changed_votes')}
    progress = {}
    for event in comparison['evidence']:
        if event['kind'] == 'seats' and event.get('seat') in booth_districts:
            current_progress = (event.get('after') or {}).get('count_progress') or {}
            value = current_progress.get('fp_completion')
            if value is not None and 0 <= value <= 1:
                progress[event['seat']] = value
    if progress:
        packet['district_fp_completion'] = progress
    flow = comparison.get('statewide_preference_changes')
    if flow and not comparison['input_revision_changed']:
        # This is an editorial cutoff in percentage points, with at most three
        # quotable figures per window. Small changes still appear on the website.
        noteworthy = [row for row in flow['rows'] if abs(row['change_pp']) >= 2][:3]
        if noteworthy:
            packet['statewide_preference_changes'] = noteworthy
            if flow['from_first_zero_count']:
                packet['preference_flow_reference'] = {'forecast_id': flow['reference_forecast_id'],
                    'source_time': flow['reference_source_time'], 'basis': 'first live capture with zero FP and TCP votes'}
    return packet

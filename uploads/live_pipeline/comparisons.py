"""Translate completed forecasts into comparisons with explicit electoral units.

Win chances are percentages, and their differences are percentage points. TCP
estimates are conditional on a named pair: changing the opponent never turns
one margin into a comparable margin against another opponent. Diagnostic
changes are evidence about the model's response, not a causal decomposition
of the change in winning probability.
"""
from datetime import datetime, timedelta, timezone
from collections import defaultdict, deque
from functools import lru_cache
import math
from pathlib import Path, PureWindowsPath
import sys
from .timezones import ZoneInfo

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'analysis'))
from lib.live_analysis_archive import load_json, analysis_path_for_snapshot
from .activity import affected_ballots, count_changes, first_tcp_size
from . import booth_fp, booth_tcp, count_context, pairing_commentary, preference_context, prompt_layout, ranked_tcp, rule_evidence, seat_alerts, tcp_reporting
from .storage import read


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


def median_seats(report):
    """Read the middle simulated seat total from the same percentile bands as
    the website. A fractional average cannot recover this whole-seat result."""
    thresholds = report.get('voteTotalThresholds') or []
    if 50 not in thresholds:
        return {}
    distributions = pairs(report.get('seatCountFrequencies'))
    parties = pairs(report.get('partyAbbr'))
    coalition = report.get('coalitionSeatCountFrequencies')
    if coalition and parties.get('-4') == 'LNP':
        # The combined row has its own distribution. In some projects the
        # individual Liberal row also carries the combined party's badge.
        # Use the combined figure for that badge to keep its meaning clear.
        distributions = {p: values for p, values in distributions.items() if parties.get(p) != 'LNP'}
        distributions['-4'] = coalition
    medians = {}
    for party, values in distributions.items():
        levels = bands(values, thresholds)
        if levels and levels['50'] is not None:
            medians[party] = levels['50']
    return medians


def comparison_summary(package):
    """Recover missing comparative evidence from the original forecast and
    its own capture when an older summary is selected. Released files stay frozen."""
    saved = read(package/'summary.json')
    needs_medians = 'medianSeats' not in saved['statewide']
    needs_tcp = 'published_district_tcp' not in saved
    needs_district_pairs = 'conditional_wins' not in saved
    needs_seat_totals = 'chance_at_least_seats' not in saved
    needs_iterations = 'iterations' not in saved
    needs_simulation_context = needs_district_pairs or needs_seat_totals
    if (needs_medians or needs_tcp or needs_simulation_context or needs_iterations) and (package/'numerical.json').is_file():
        numerical = read(package/'numerical.json')
        if needs_iterations:
            saved['iterations'] = numerical.get('iterations')
        if needs_medians:
            saved['statewide']['medianSeats'] = median_seats(numerical['report'])
        if needs_tcp:
            saved['published_district_tcp'] = tcp_reporting.published_districts(package.parent.parent, numerical)
        if needs_simulation_context:
            context = simulation_context(numerical, package.parent.parent, saved['statewide']['medianSeats'])
            for key, value in context.items():
                saved.setdefault(key, value)
    return saved


def summary(numerical, run_folder=None):
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
    statewide['medianSeats'] = median_seats(report)
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
            'iterations': numerical.get('iterations'),
            'seats': seats, 'statewide': statewide,
            'published_district_tcp': tcp_reporting.published_districts(run_folder, numerical),
            'parties': parties, **simulation_context(numerical, run_folder, statewide['medianSeats'])}


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


def diagnostic_path(numerical, run_folder):
    path = numerical.get('diagnostic_file')
    if not path:
        return None
    snapshot = Path(path)
    # The Windows GUI writes its own absolute path. A WSL worker opens
    # the same run through its mounted folder. Use the snapshot there only
    # when the handoff identifies this run as the owner of the diagnostic.
    windows_path = PureWindowsPath(path)
    if run_folder is not None and windows_path.is_absolute() and not snapshot.is_absolute():
        if windows_path.parent.name != numerical['run_id'] or windows_path.parent.parent.name != 'live_runs':
            raise ValueError('Windows diagnostic path is outside this forecast run: '+path)
        snapshot = Path(run_folder)/windows_path.name
    return snapshot


def simulation_context(numerical, run_folder, medians):
    """Read simulation counts that help the local writer explain the forecast.
    District wins among draws with the same two finalists measure how likely
    each was to win that matchup. Statewide seat totals show whether a change
    in a party's median reflected a material shift in its chances. Both come
    from the forecast for this capture and stay outside the AI request."""
    result = {}
    chances = {}
    snapshot = diagnostic_path(numerical, run_folder)
    if snapshot and snapshot.is_file():
        report = read(snapshot).get('simulation_report', {})
        for seat, rows in zip(report.get('seat_name', []), report.get('seat_tcp_win_percent', [])):
            result[seat] = {str(row['party_a']['party_index'])+'/'+str(row['party_b']['party_index']):
                            row['value'] for row in rows}
        distributions = {str(row['party_index']): row['values'] for row in report.get('party_seat_win_frequency', [])}
        if numerical['report'].get('coalitionSeatCountFrequencies') and '-4' in medians:
            distributions['-4'] = report.get('coalition_seat_win_frequency') or []
        for party, median in medians.items():
            counts = distributions.get(party, [])
            simulations = sum(counts)
            if simulations:
                # If the median is five, a move between four and five is about
                # the chance of at least five. A move between five and six is
                # about the chance of at least six. Saving these two percentages
                # covers either direction without keeping the whole distribution.
                chances[party] = {str(n): 100 * sum(counts[n:]) / simulations for n in (median, median + 1)}
    return {'election': numerical['report'].get('termCode'), 'conditional_wins': result,
            'chance_at_least_seats': chances}


def preference_count_observations(analysis):
    """Keep the published split and the three-way tallies on their own denominators.
    Only changes to the observed ballots establish new counting. The simulator's
    separate draw statistics tell us whether it actually used that observation."""
    result = {}
    for row in analysis.get('preference_count_observations', []):
        result[row['district']] = {
            'seat': row['district'],
            'sample': row.get('sample'), 'available_at': row.get('available_at'),
            'sampled_preference_ballots': row['sampled_preference_ballots'],
            'flow_percent': row['flow_percent'],
            'transferred_votes': {p: row['sampled_preference_ballots']*share/100
                                  for p, share in row['flow_percent'].items()},
            'sampled_first_preferences': row.get('sampled_first_preferences', {}),
            'sampled_candidate_totals': row.get('sampled_candidate_totals', {})}
    return result


@lru_cache(maxsize=32)
def archived_regional_counts(path):
    # Saved archives are immutable. Retain only small regional totals when
    # several comparison windows return to an older capture.
    return rule_evidence.regional_counts(load_json(path))


@lru_cache(maxsize=32)
def archived_primary_accounts(path):
    # Older packages can supply the same small per-seat account from the
    # immutable sidecar. Cache only the accounts across comparison windows.
    return {r['seat']: r for r in load_json(path).get('fp_based_tpp', [])}


def booth_tcp_bounds(analysis):
    """Keep the votes the simulator still treats as fixed after a pairing
    change or primary recheck. These bounds are separate from the feed's
    reported totals and do not generate a new count event."""
    result = {}
    for booth in analysis.get('booths', []):
        retained = booth.get('retained_tpp', {})
        revised = booth.get('tcp_fp_revision', {})
        floors = [_party_values(v) for v in (retained.get('lower_bounds', retained.get('counts')),
                                             revised.get('lower_bounds')) if v]
        if floors:
            key = '/'.join(str(booth.get(k, '')) for k in ('seat_name', 'booth_type', 'vote_type', 'name'))
            result[key] = floors
    return result


@lru_cache(maxsize=32)
def archived_tcp_bounds(path):
    return booth_tcp_bounds(load_json(path))


def comparison_evidence(package):
    """Recover evidence omitted by older packages from that capture's own
    diagnostic. The frozen report and the accepted vote history stay intact."""
    saved = read(package/'evidence.json.gz')
    first = package.parent/'f00000001'
    if (saved.get('booths') and package.name >= first.name and package.name != 'origin'
            and (first/'summary.json').is_file() and (first/'evidence.json.gz').is_file()):
        revision, source_time, shapes, tcp_shapes = booth_fp.opening_geography(first)
        reference = read(package/'summary.json')
        if revision == reference['input_revision'] and source_time and source_time <= (reference['source_time'] or ''):
            saved['opening_fp_geography'] = shapes
            saved['opening_tcp_geography'] = tcp_shapes
    records = saved.get('manual_count_observations', {})
    missing = [seat for seat, row in records.items() if 'flow_percent' not in row]
    need_regions = 'matched_regional_counts' not in saved and bool(saved.get('booths'))
    need_accounts = 'fp_based_tpp' not in saved and bool(saved.get('booths'))
    need_bounds = 'booth_tcp_bounds' not in saved and bool(saved.get('booths'))
    if (missing or need_regions or need_accounts or need_bounds) and (package/'numerical.json').is_file():
        snapshot = diagnostic_path(read(package/'numerical.json'), package.parent.parent)
        path = analysis_path_for_snapshot(snapshot) if snapshot else None
        if path and path.is_file() and need_bounds:
            saved['booth_tcp_bounds'] = archived_tcp_bounds(path)
        if path and path.is_file() and need_accounts:
            saved['fp_based_tpp'] = archived_primary_accounts(path)
        if path and path.is_file() and need_regions:
            saved['matched_regional_counts'] = archived_regional_counts(path)
        if path and path.is_file() and missing:
            archived = preference_count_observations(load_json(path))
            for seat in missing:
                row = archived.get(seat)
                if row and all(records[seat].get(k, {}) == row[k] for k in
                               ('transferred_votes', 'sampled_first_preferences', 'sampled_candidate_totals')):
                    records[seat] = row
    return saved


def evidence(numerical, run_folder=None):
    """Retain measured booth counts and estimated outstanding votes separately.
    Shared preference adjustments use log odds rather than percentage points."""
    snapshot = diagnostic_path(numerical, run_folder)
    if snapshot is None:
        return {'booths': {}, 'seats': {}, 'preferences': {}, 'issues': [], 'statewide': {},
                'observed_counts_available': numerical.get('origin', False)}
    analysis = load_json(analysis_path_for_snapshot(snapshot))
    result = {'booths': {}, 'seats': {}, 'preferences': {}, 'issues': analysis.get('input_recovery', []),
              'booth_warnings': seat_alerts.booth_warnings(analysis),
              'observed_counts_available': 'booths' in analysis,
              'statewide': _shared_evidence(analysis.get('election', {})),
              'category_biases': _category_evidence(analysis.get('category_biases', {}))}
    result['regions'] = {r['name']: _shared_evidence(r) for r in analysis.get('regions', [])}
    result['optional_preferential'] = analysis.get('optional_preferential')
    result['classic_benchmark_labor_rates'] = {s['name']: s['classic_benchmark_labor_rates']
        for s in analysis.get('seats', []) if s.get('classic_benchmark_labor_rates')}
    result['fp_based_tpp'] = {r['seat']: r for r in analysis.get('fp_based_tpp', [])}
    result['booth_tcp_bounds'] = booth_tcp_bounds(analysis)
    result['matched_regional_counts'] = rule_evidence.regional_counts(analysis)
    result['manual_preferences'] = {r['seat']: r for r in analysis.get('manual_preference_evidence', [])}
    # These are published sample counts, whereas manual_preferences above
    # describes their use in simulation draws. Only the former can establish
    # new counting without changed FP/TCP totals (for example a Finniss 4CP).
    result['manual_count_observations'] = preference_count_observations(analysis)
    district_settings = {s['name']: s for s in analysis.get('seats', [])}
    independent_parties = [p for p, abbr in pairs(numerical['report'].get('partyAbbr', {})).items()
                           if abbr == 'IND' and int(p) >= 0]
    for booth in analysis.get('booths', []):
        node = booth.get('node', {})
        key = '/'.join(str(booth.get(k, '')) for k in ('seat_name', 'booth_type', 'vote_type', 'name'))
        result['booths'][key] = {k: booth.get(k) for k in ('seat_name', 'name', 'booth_type', 'vote_type')}
        if booth.get('display_name'):
            result['booths'][key]['name'] = booth['display_name']
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
        # For a non-classic contest the simulator uses matched local FP/TCP
        # booths to estimate the share of other candidates' preferences going
        # to its focus candidate. Keep the actual fitted percentage and its
        # count identities. An older archive lacking this measure can still
        # show which booths stopped supplying usable votes for the pair.
        flow = seat.get('tcp_focus_party_pref_flow')
        focus = seat.get('tcp_focus_party_index')
        raw_pair = _party_values(seat.get('node', {}).get('tcp_votes_current'))
        if flow is not None and 0 <= flow <= 100 and len(raw_pair) == 2 and str(focus) in raw_pair:
            result.setdefault('local_tcp_preference_flows', {})[seat['name']] = {
                'raw_pair': sorted(raw_pair), 'focus': str(focus), 'share': flow}
        result['seats'][seat['name']] = {k: seat.get(k) for k in
            ('region_name', 'estimated_tpp', 'live_preference_flow_deviation', 'fp_evidence', 'fp_integration',
             'final_specific_tpp_deviation', 'final_specific_fp_deviations',
             'fp_booth_type_sensitivity', 'tpp_booth_type_sensitivity',
             'fp_vote_type_sensitivity', 'tpp_vote_type_sensitivity')}
        result['seats'][seat['name']]['count_progress'] = {k: seat.get('node', {}).get(k) for k in
                ('fp_completion', 'tcp_completion', 'tpp_completion')}
    for row in analysis.get('preference_learning', []):
        key = f"{row['seat']}/{row['category']}/{row['pair']}"
        # Keep the source rates in local evidence. Only the comparison for
        # an actual booth return needs to reach either commentary writer.
        if 'pending_first_rates' in row:
            result.setdefault('ranked_source_rates', {})[key] = row['pending_first_rates']
        result['preferences'][key] = {k: v for k, v in row.items() if k != 'pending_first_rates'}
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
    changed_units_by_seat = defaultdict(int)
    for event in events:
        if event['kind'] != 'booths' or not event.get('changed_votes'):
            continue
        # Each record covers both FP and TCP at one booth or declaration
        # category. Count it before applying the display cutoffs: even a small
        # recheck elsewhere means a headline's named booth was only one update.
        changed_units_by_seat[event['seat']] += 1
        before, after = event.get('before') or {}, event.get('after') or {}
        has_votes = lambda row: any(v > 0 for field in ('fp_votes_current', 'tcp_votes_current')
                                   for v in row.get(field, {}).values())
        kind = 'new' if not has_votes(before) and has_votes(after) else 'updated'
        fp_size, tcp_size, new_pair = _booth_count_sizes(event)
        size = max(fp_size, tcp_size)
        if kind == 'updated' and size < minimum_revision:
            continue
        record = after or before
        units.append({'kind': kind, 'seat': event['seat'], 'name': record['name'],
                      'record': event['record'], 'evidence_id': event['id'],
                      'vote_type': record.get('vote_type'), 'booth_type': record.get('booth_type'),
                      'affected_votes': event['changed_votes'],
                      'size_for_ranking': size,
                      'fp_net_change': event.get('fp_vote_change', 0),
                      'tcp_net_change': event.get('tcp_vote_change', 0)})
        if new_pair and sum(before.get('tcp_votes_current', {}).values()) > 0:
            units[-1]['replacement_tcp_votes'] = tcp_size
    units.sort(key=lambda row: (-row['size_for_ranking'], row['seat'], row['name']))

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
            'seats_with_multiple_changed_units': sorted(s for s, n in changed_units_by_seat.items() if n > 1),
            'minimum_revision_votes': minimum_revision, **groups(units),
            'by_seat': {s: groups([r for r in units if r['seat'] == s]) for s in sorted({r['seat'] for r in units})}}


def tcp_against_previous_forecast(previous, current, *, ranked_pairs=(), ranked_estimates=None, booth_key=None,
                                  blended_estimates=None):
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

    pair = frozenset(measured)
    seat = current.get('seat_name')
    counted_total = sum(measured.values())
    ranked = (ranked_estimates or {}).get((booth_key, pair))
    category = current.get('vote_type')
    if (not ranked and category and category != 'Ordinary'
            and (current.get('name') or '').casefold() == category.casefold()):
        # A district-wide Early or Postal line can combine several venues
        # or batches. A TCP for just one of them can have a very different
        # FP mix. The full category's projected share cannot establish what
        # was expected of that batch. Allow small rechecks using the model's
        # two-percent FP/TCP comparability tolerance (LivePartialCount.h).
        category_fp = sum(current.get('fp_votes_current', {}).values())
        if counted_total < .98*category_fp:
            return {'available': False, 'partial_category_fp_votes': category_fp,
                    'reason': 'Partial category TCP with no earlier estimate for the counted batch.'}
    if (seat, pair) in ranked_pairs:
        # The usual booth TCP export predates the ranked live account. Even
        # identical opponents do not make it the expectation used for this
        # contest. The category comparison is supplied separately when usable.
        if ranked is None:
            return {'available': False, 'reason': 'The earlier ranked allocation does not identify a comparable booth estimate.'}
        forecasts = [ranked]
    elif blended_estimates is not None:
        estimate = blended_estimates.get((booth_key, pair))
        if estimate is None:
            return {'available': False, 'reason': 'No district-aligned booth benchmark is available for these finalists.'}
        forecasts = [(estimate, 'district-aligned TCP')]
    else:
        forecasts = [(previous.get('tcp_votes_projected'), 'TCP'), (previous.get('tpp_votes_projected'), 'TPP')]
    expectation = None
    expectation_kind = None
    for projected, kind in forecasts:
        if not projected or projected.keys() != measured.keys():
            continue
        if any(not math.isfinite(v) or v < 0 for v in projected.values()) or sum(projected.values()) <= 0:
            continue
        expectation, expectation_kind = projected, kind
        break
    if expectation is None:
        return {'available': False, 'reason': 'The reference forecast has no positive booth estimate for this exact pair.'}

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


def compare(current, previous, history, current_evidence, previous_evidence, zero_count_flows=None,
            continuing_pairs=None):
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
    reporting = tcp_reporting.changes(previous, current, previous_evidence, current_evidence)
    original_before, original_after = previous_evidence, current_evidence
    previous_evidence, current_evidence, retained = tcp_reporting.reporting_views(
        previous_evidence, current_evidence, tcp_reporting.zero_districts(previous), tcp_reporting.zero_districts(current))
    ranked_pairs, ranked_booths, ranked_groups, ranked_additions = ranked_tcp.references(
        previous, current, previous_evidence, current_evidence)
    old_booths, new_booths = previous_evidence.get('booths', {}), current_evidence.get('booths', {})
    changed_fp = {new_booths[k].get('seat_name') for k in old_booths.keys() & new_booths.keys()
                  if old_booths[k].get('fp_votes_current') != new_booths[k].get('fp_votes_current')}
    reference_fp, earlier_tcp = booth_tcp.comparison_benchmarks(
        previous, current, original_before, original_after, changed_fp, ranked_pairs)
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
            if kind == 'manual_count_observations' and event['changed_votes'] and event['after']:
                # Eligibility alone cannot establish that a draw actually used
                # the sample. Older diagnostics may record only the observation.
                used = current_evidence.get('manual_preferences', {}).get(key, {})
                event['sample_used_in_draws'] = used.get('sample_conditioned_draws', 0) > 0
            if kind == 'booths' and old.get(key) and new.get(key):
                event['fp_vote_change'] = sum(new[key]['fp_votes_current'].values())-sum(old[key]['fp_votes_current'].values())
                event['tcp_vote_change'] = sum(new[key]['tcp_votes_current'].values())-sum(old[key]['tcp_votes_current'].values())
                event['pair_changed'] = new[key]['tcp_votes_current'].keys() != old[key]['tcp_votes_current'].keys()
                # Compare the new cumulative composition with what the previous
                # forecast expected at that booth. This describes how the share
                # differs from expectations, not its effect on the seat's win chance.
                forecast = reference_fp.get(key, {})
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
                if fp_changed and sum(measured.values()) > 0:
                    estimates = rule_evidence.booth_projection_after_preferences(
                        original_before['booths'][key], original_after['booths'][key],
                        event['seat'], previous, current, ranked_pairs,
                        earlier_tcp=earlier_tcp, booth_key=key, expected_fp=forecast, measured_fp=measured,
                        reference_evidence=original_before, current_evidence=original_after)
                    if estimates:
                        event['rule_booth_tcp_estimates'] = estimates
            elif kind == 'booths':
                # Newly introduced or removed units have no matching booth
                # expectation, but their actual net count change is still known.
                before, after = old.get(key) or {}, new.get(key) or {}
                event['fp_vote_change'] = sum(after.get('fp_votes_current', {}).values())-sum(before.get('fp_votes_current', {}).values())
                event['tcp_vote_change'] = sum(after.get('tcp_votes_current', {}).values())-sum(before.get('tcp_votes_current', {}).values())
            if kind == 'booths':
                tcp_comparison = tcp_against_previous_forecast(old.get(key), new.get(key),
                    ranked_pairs=ranked_pairs, ranked_estimates=ranked_booths, booth_key=key, blended_estimates=earlier_tcp)
                if tcp_comparison is not None:
                    # The feed can show zero for a count already retained by the
                    # forecast, then publish both identical finalist totals again.
                    # This is a change in the displayed count, with no fresh
                    # preference split to compare against the earlier projection.
                    prior_tcp = (old.get(key) or {}).get('tcp_votes_current') or {}
                    reported = (new.get(key) or {}).get('tcp_votes_current') or {}
                    if (not any(prior_tcp.values()) and len(reported) == 2 and sum(reported.values()) > 0
                            and reported in original_before.get('booth_tcp_bounds', {}).get(key, ())):
                        tcp_comparison = {'available': False, 'already_in_forecast': True,
                                          'reason': 'Both reported TCP tallies were already included in the earlier forecast.'}
                    elif not tcp_comparison.get('available') and ranked_additions:
                        mapped = count_context.mapped_tcp(new[key]) if key in new else None
                        addition = ranked_additions.get((key, frozenset(mapped or ())))
                        if addition:
                            event['rule_ranked_tcp_addition'] = addition
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
    transfers = count_context.category_transfers(events) if counts.get('known') else []
    transferred_ids = {i for t in transfers for i in t['evidence_ids']}
    transferred_units = {e['record'] for e in events if e['id'] in transferred_ids}
    leads, replacements = count_context.district_tcp_changes(
        previous_evidence, current_evidence, details, continuing_pairs, transferred_units)
    if current['input_revision'] == previous['input_revision']:
        # An earlier pair can still inform the simulation after a reset. Only
        # describe a lead in a continuing count here, with comparable inputs.
        resets = tcp_reporting.zero_districts(previous) | tcp_reporting.zero_districts(current)
        for seat, count in leads.items():
            if seat not in resets and pairing_commentary.fewer_votes_favour_counted_leader(seat, count, previous, current):
                count['fewer_votes_favour_counted_leader'] = True
    if replacements:
        count_context.add_matched_booth_expectations(replacements, events)
    result = {'available': True, 'reference_forecast_id': previous['forecast_id'],
            'count_activity': counts,
            'reference_source_time': previous['source_time'],
            'input_revision_changed': current['input_revision'] != previous['input_revision'],
            'win_changes': rows, 'seat_changes': details, 'evidence': events,
            'category_transfers': transfers,
            'close_tcp_counts': leads, 'replacement_tcp_counts': replacements,
            'booth_changes': booth_changes(events, counts.get('known', False)),
            'statewide': {side: {k:v for k,v in item['statewide'].items() if k != 'preference_flow_medians'}
                          for side, item in [('before', previous), ('after', current)]}}
    # The largest earlier booth can explain why a second substantial return
    # changes how much the forecast relies on local counting. Include booths
    # whose totals did not change in this particular update.
    previous_places = defaultdict(list)
    for booth in previous_evidence.get('booths', {}).values():
        if booth.get('booth_type') == 'Normal' and booth.get('vote_type') == 'Ordinary':
            ballots = sum(booth.get('fp_votes_current', {}).values())
            if ballots:
                previous_places[booth.get('seat_name')].append((ballots, booth.get('name')))
    result['rule_earlier_ordinary_count'] = {}
    for seat, places in previous_places.items():
        size, name = max(places, key=lambda place: place[0])
        result['rule_earlier_ordinary_count'][seat] = dict(largest=size, name=name, total=sum(n for n, _ in places))
    if not result['input_revision_changed']:
        result['rule_tpp_components'] = rule_evidence.tpp_component_changes(
            previous, current, previous_evidence, current_evidence)
    # Older packages lack matched booth history. An absent reference does
    # not establish that all of today's matched booths are newly counted.
    if previous.get('origin') or 'matched_regional_counts' in original_before:
        result['rule_regional'] = rule_evidence.regional_contrast(
            original_before.get('matched_regional_counts', {}), original_after.get('matched_regional_counts', {}))
        result['rule_regional_changes'] = rule_evidence.regional_updates(
            original_before.get('matched_regional_counts', {}), original_after.get('matched_regional_counts', {}))
    calls = seat_alerts.call_changes(previous, current)
    if calls:
        result['seat_call_changes'] = calls
    warnings = seat_alerts.noteworthy_warnings(previous, current)
    if warnings:
        result['noteworthy_booth_warnings'] = warnings
    if retained:
        result['retained_tcp_changes_while_published_zero'] = retained
        result['close_contests_with_material_retained_tcp_changes'] = tcp_reporting.close_retained_contests(
            retained, current, current_evidence)
        if not result['input_revision_changed']:
            context = tcp_reporting.retained_pair_revisions(retained, previous, current, original_before, original_after)
            if context:
                result['retained_pair_revisions'] = context
    if not result['input_revision_changed']:
        if ranked_groups:
            result['ranked_tcp_groups'] = dict(ranked_groups)
        explanations = preference_context.compare(previous, current, previous_evidence, current_evidence)
        if explanations:
            result['district_preference_evidence'] = explanations
    if reporting:
        result['tcp_reporting_changes'] = reporting
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
            continuing_pairs = tcp_reporting.continuing_realigned_pairs(complete_anchor, current, earlier)
            cache[identity] = compare(current, complete_anchor, history,
                                      load_evidence(current['forecast_id']), load_evidence(identity), zero_count_flows,
                                      continuing_pairs)
        elapsed = None if not anchor['source_time'] else (now-instant(anchor['source_time'], zone)).total_seconds()
        result[name] = dict(cache[identity], actual_seconds=elapsed,
                            requested_seconds=windows.get(name), **daily_metadata)
    return result


def _pair_likelihood(seat, pair):
    # A pair that was plausible at either endpoint can be relevant to the story.
    return max(seat['before']['pairing_percent'].get(pair, 0), seat['after']['pairing_percent'].get(pair, 0))/100


def _same_pair(seat, pair):
    # The same independent slot can represent different people at the two
    # endpoints. Its numerical change then compares different contests.
    for party in pair.split('/'):
        old = (seat['before'].get('candidates', {}).get(party) or {}).get('name')
        new = (seat['after'].get('candidates', {}).get(party) or {}).get('name')
        if old and new and old != new:
            return False
    return True


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
        likelihood = _pair_likelihood(seat, pair)
        # A range for a pairing below 1% at both endpoints gives little
        # information about the winner, even if it is the only range to move.
        if likelihood < .01 or not _same_pair(seat, pair):
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
            score = (len(rows), movement * likelihood)
            if best_score is None or score > best_score:
                # Include the same pair's median alongside the range, even
                # when unchanged. A firmer projected lead can be distinguished
                # from a growing lead only if the writer can see both.
                old, new = before[pair].get('50'), after[pair].get('50')
                if old is not None and new is not None:
                    rows.append({'metric': f'/tcp_percentiles/{pair}/50',
                                 'before': old, 'after': new, 'change': new-old})
                best, best_score = rows, score
    return best


def _booth_score(affected_votes):
    """A 100-ballot update earns two editorial points, then one per doubling.

    Affected ballots include corrections even when net turnout is unchanged.
    The ten-point ceiling keeps size alone below a 15-point win-chance change.
    """
    if affected_votes <= 0:
        return 0
    return max(0, min(10, 2 + math.log2(affected_votes / 100)))


def _pairing_score(seat, movement):
    # A new opponent matters most when the winner is in doubt. Use the more
    # competitive endpoint so a race that just became clear still qualifies.
    # A 50% favourite gets full weight, a 95% favourite only a tenth. Four
    # points of pairing movement earn at most one point beside win movements.
    endpoints = (seat['before'], seat['after'])
    if any(not side['win_percent'] or not side['pairing_percent'] for side in endpoints):
        return 0
    favourite = min(max(side['win_percent'].values()) for side in endpoints)
    competitive_weight = min(1, 2 * (1 - favourite / 100))
    return min(8, abs(movement) / 4) * competitive_weight


def _contest_score(seat):
    """Give useful changes in the final contest some space alongside win odds.

    These weights allocate commentary space. They do not enter the forecast.
    For a likely close pairing, one point of median TCP movement earns one
    point; four points of change in the width of its middle 90% earn one.
    Ignore the far tails, which can move markedly on a handful of draws.
    """
    before, after = seat['before'], seat['after']
    score = 0
    for pair in before['pairing_percent'].keys() | after['pairing_percent'].keys():
        if not _same_pair(seat, pair):
            continue
        # Pairing frequencies cover simulated elections. A pairing absent
        # from an endpoint's frequency table had no occurrences there.
        pairing_move = abs(after['pairing_percent'].get(pair, 0) - before['pairing_percent'].get(pair, 0))
        score = max(score, _pairing_score(seat, pairing_move))
        old = before['tcp_percentiles'].get(pair) or {}
        new = after['tcp_percentiles'].get(pair) or {}
        if old.get('50') is None or new.get('50') is None:
            continue
        # A median within ten points of 50% at either end, or crossing that
        # band, can tell us about the lead. An isolated tail entering the band
        # in a comfortable race is a poor reason to select that district.
        low, high = sorted((old['50'], new['50']))
        if low > 60 or high < 40:
            continue
        movement = high - low
        if all(bands.get(q) is not None for bands in (old, new) for q in ('5', '95')):
            width_change = (new['95'] - new['5']) - (old['95'] - old['5'])
            movement = max(movement, abs(width_change) / 4)
        score = max(score, min(8, movement) * _pair_likelihood(seat, pair))
    return score


def _district_evidence_order(comparison, counted):
    """Choose up to ten districts whose before/after figures and booths to send.

    Reserve up to three places for the largest comparable win movements of
    at least one percentage point in districts with counting. This is a
    shortlist for the writer; the new ballots need not have caused the shift.
    Other places cover reversals, changes in the final contest and large counts.
    """
    scores = {}
    local_moves = {}
    for row in comparison['win_changes']:
        if row.get('identity_changed'):
            continue
        name = row['seat']
        movement = abs(row['change_pp'])
        reversal = row['maximum_within_window'] - row['minimum_within_window'] - movement
        scores[name] = max(scores.get(name, 0), movement, reversal)
        if movement >= 1 and counted.get(name, 0) > 0:
            local_moves[name] = max(local_moves.get(name, 0), movement)
    for seat in comparison['seat_changes']:
        name = seat['seat']
        scores[name] = max(scores.get(name, 0), _contest_score(seat))
    for event in comparison['evidence']:
        if event.get('seat') and event.get('changed_votes', 0):
            name = event['seat']
            size = event['changed_votes']
            if event.get('kind') == 'booths':
                fp_size, tcp_size, _ = _booth_count_sizes(event)
                size = max(fp_size, tcp_size)
            scores[name] = max(scores.get(name, 0), _booth_score(size))
    local = sorted(local_moves, key=lambda s: (-local_moves[s], s))[:3]
    ranked = sorted(scores, key=lambda s: (-scores[s], s))
    order = list(dict.fromkeys(local + ranked))[:10]
    # A statewide swing can move many uncounted seats. Keep at least one
    # actual count in view when the first ten would otherwise contain none.
    if counted and not any(counted.get(name, 0) > 0 for name in order):
        largest = min(counted, key=lambda s: (-counted[s], s))
        if counted[largest] > 0:
            order = order[:9] + [largest]
    return order


def headline_candidate_priority(row, chances):
    """Choose whose change in win chance to describe within a district. If
    two candidates account for at least 95% of the latest simulated wins,
    report the outlook for the current leader. With more wins going to other
    candidates, the largest absolute movement tells readers who gained or
    lost most. The 95% boundary is an editorial choice for both writers."""
    first_two = sorted(chances, key=lambda p: (-chances[p], p))[:2]
    clear_pair = len(first_two) == 2 and sum(chances[p] for p in first_two) >= 95
    # If the two are level neither is the leader. A candidate with no change
    # to display should not displace the candidate whose prospects moved.
    moves = abs(row['change_pp'])
    leads = clear_pair and chances[first_two[0]] > chances[first_two[1]] and row['party_id'] == first_two[0]
    return (leads and moves >= .05, moves, row['after'])


def _district_win_rows(comparison, selected, seats):
    # Keep the largest comparable win movement and the preferred headline
    # candidate for each chosen district even when they are different.
    # A busy update can otherwise use all 24 candidate rows on a few races
    # and leave the writer with a booth result but no change in win chance.
    ranked = [row for row in comparison['win_changes'] if row['seat'] in selected]
    first_by_district, headline_by_district = {}, {}
    for index, row in enumerate(ranked):
        if row['change_pp'] and not row.get('identity_changed'):
            first_by_district.setdefault(row['seat'], index)
            priority = headline_candidate_priority(row, seats[row['seat']]['win_percent'])
            if row['seat'] not in headline_by_district or priority > headline_by_district[row['seat']][0]:
                headline_by_district[row['seat']] = priority, index
    keep = set(first_by_district.values()) | {index for _, index in headline_by_district.values()}
    for index in range(len(ranked)):
        if len(keep) >= 24:
            break
        keep.add(index)
    return [ranked[index] for index in sorted(keep)]


def _booth_count_sizes(event):
    """Count new evidence for the opponents actually being reported. A thousand
    ballots for a replacement pair carry the same size weight as a thousand
    first TCP ballots. Old-pair ballots removed in the handover are not another
    thousand new observations. A separate withdrawal is still a real correction.
    """
    old, new = event.get('before') or {}, event.get('after') or {}
    fp, tcp = 'fp_votes_current', 'tcp_votes_current'
    old_fp, new_fp = old.get(fp, {}), new.get(fp, {})
    old_tcp, new_tcp = old.get(tcp, {}), new.get(tcp, {})
    fp_size = affected_ballots(old_fp, new_fp)
    tcp_size = affected_ballots(old_tcp, new_tcp)
    first_count = first_tcp_size(old, new)
    if first_count is not None:
        tcp_size = first_count
    return fp_size, tcp_size, first_count is not None


def _booth_story(event, candidates, replacement):
    """The local comparison archive keeps both complete records. The writer
    needs the changed counts and comparisons already made from the projections.
    A small simultaneous FP recheck would otherwise bring back the primary
    allocations and expectation figures for every candidate at every booth."""
    old, new = event.get('before') or {}, event.get('after') or {}
    fp, tcp = 'fp_votes_current', 'tcp_votes_current'
    old_tcp = old.get(tcp, {})
    new_total = sum(new.get(tcp, {}).values())
    fp_size, tcp_size, new_pair = _booth_count_sizes(event)
    # If the combined replacement account already covers the handover, show
    # booths with actual replacement votes ahead of old-pair withdrawals.
    withdrawal_only = replacement and new_total == 0 and not fp_size
    weight = max(fp_size, tcp_size)
    if not weight:
        return None
    # Treat at most 50 revised first preferences as incidental, and only
    # below 5% of both the old FP count and the size of the TCP update.
    # These editorial limits keep major corrections and every initial FP
    # result visible. The district totals always include the recheck.
    incidental_fp = (new_total > 0 and 0 < fp_size <= 50 and
                     fp_size < .05 * min(tcp_size, sum(old.get(fp, {}).values())))
    fields = []
    if fp_size and not incidental_fp:
        fields.append(fp)
    if tcp_size:
        fields.append(tcp)
    detail = {k: v for k, v in event.items() if k not in (
        'before', 'after', 'interpretation', 'fp_share_minus_previous_expectation_pp', 'pair_changed',
        'rule_booth_tcp_estimates', 'rule_ranked_tcp_addition')}
    if not fp_size or incidental_fp:
        detail.pop('fp_vote_change', None)
    if not tcp_size:
        detail.pop('tcp_vote_change', None)
    if new_pair and sum(old_tcp.values()) > 0:
        detail['pair_changed'] = True
        # A net change obtained by subtracting totals for different opponents
        # is not the number counted for the replacement. Give the writer the
        # current measured sample size instead. Keep the activity measure for
        # district selection and accounting in the mechanical report.
        detail.pop('tcp_vote_change', None)
        detail['replacement_tcp_votes'] = new_total
    for endpoint in ('before', 'after'):
        record = event.get(endpoint)
        if record is not None:
            detail[endpoint] = {k: record[k] for k in ('name', 'vote_type', 'booth_type', *fields) if k in record}
            raw_ids = set().union(*(record.get(k, {}).keys() for k in fields))
            aliases = {k: v for k, v in record.get('count_to_projection_ids', {}).items() if k in raw_ids}
            if aliases:
                detail[endpoint]['count_to_projection_ids'] = aliases
    if fp in fields:
        gaps = event.get('fp_share_minus_previous_expectation_pp', {})
        detail['fp_share_minus_previous_expectation_pp'] = {p: v for p, v in gaps.items() if p in candidates}
    return (1 if withdrawal_only else 3), weight, detail


def _diagnostic_story(event):
    """Keep the strongest supporting model accounts after measured counts.
    The weights retain the established ordering of statewide, regional,
    preference and district context. They are editorial priorities only."""
    kind = event['kind']
    row = event.get('after') or event.get('before') or {}
    if kind == 'seats':
        changes = sorted(numeric_changes(event.get('before'), event.get('after')), key=lambda r: -abs(r['change']))
        event = {k: v for k, v in event.items() if k not in ('before', 'after', 'interpretation')}
        event['diagnostic_changes'] = changes[:8]
        event['units'] = 'Internal diagnostic units as named; not win-probability contributions.'
    if kind == 'preferences':
        size = 5000 + abs(row.get('adjusted_first_votes_from_pending_fp', 0) - row.get('prior_first_votes_from_pending_fp', 0))
    elif kind == 'category_biases':
        size = row.get('vote_count', 0) / 100
    elif kind == 'input_recovery':
        size = abs(row.get('impact_votes') or 0)
    else:
        size = {'statewide': 110000, 'manual_preferences': 100000, 'regions': 100000, 'seats': 4000}.get(kind, 0)
    return 0, size, event


def _selected_evidence(events, order, seats, replacements):
    """Choose explanatory records within a bounded request. Size comparisons
    use the figures the writer receives. Each further booth in the same seat
    needs modestly more evidence to outrank an otherwise comparable booth
    elsewhere. Several large local observations can all earn places."""
    groups = defaultdict(list)
    for event in events:
        seat, kind = event.get('seat'), event['kind']
        if kind == 'booths':
            item = _booth_story(event, seats[seat]['candidates'], seat in replacements)
            if not item:
                continue
        elif kind == 'manual_count_observations' and event.get('changed_votes'):
            item = (3, event['changed_votes'], event)
        else:
            item = _diagnostic_story(event)
        groups[seat].append(item)
    queues = {s: deque(sorted(items, key=lambda i: (-i[0], -i[1], i[2].get('record', ''), i[2]['id'])))
              for s, items in groups.items()}
    positions = {s: i for i, s in enumerate(order)}
    seen = defaultdict(int)
    included, kinds = [], set()
    used = shared = biases = 0
    def rank(seat):
        tier, size, _ = queues[seat][0]
        # After one example, a second needs 15% more affected ballots to
        # outrank the first example elsewhere; after two it needs 30% more.
        return -tier, -size/(1 + .15*seen[seat]), positions.get(seat, len(order))
    while queues:
        seat = min(queues, key=rank)
        _, _, event = queues[seat].popleft()
        if not queues[seat]:
            del queues[seat]
        kind = event['kind']
        if (seat and kind != 'booths' and (seat, kind) in kinds) or (kind == 'category_biases' and biases >= 6):
            continue
        candidates = seats[seat]['candidates'] if seat else {}
        cost = prompt_layout.evidence_size(event, candidates)
        # Bytes of compact UTF-8 detail, including the booth's checked figures.
        # Reserve most of this allowance for local evidence. The separate
        # district comparisons and replacement summaries are always available.
        if used + cost > 9000 or (not seat and shared + cost > 2400):
            continue
        included.append(event)
        used += cost
        if seat:
            kinds.add((seat, kind))
            if kind == 'booths':
                seen[seat] += 1
        else:
            shared += cost
        biases += kind == 'category_biases'
    return included


def prompt_packet(current, comparison):
    """Supply a bounded selection of district details and an all-seat overview."""
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
    transfers = comparison.get('category_transfers', [])
    for transfer in transfers:
        # Both records appeared above. For editorial priority, count the
        # movement once, just as FP and TCP can describe the same ballots.
        count_changes_by_seat[transfer['seat']] -= max(transfer['fp_votes'], transfer['tcp_votes'])
    selection_order = _district_evidence_order(comparison, count_changes_by_seat)
    selected = set(selection_order)
    # Describe a matched relabelling once, before spending the limited booth
    # evidence allowance. The full mechanical accounts keep both entries.
    matched_ids = {identity for t in transfers for identity in t['evidence_ids']}
    events = [e for e in comparison['evidence'] if e['id'] not in matched_ids
              and (e['kind'] in ('statewide', 'category_biases', 'regions') or e.get('seat') in selected)]
    # A preference sample counts transfers from specified sources to three or
    # more survivors. Preserve its reported shares and coverage together. The
    # estimated candidate vote totals used to detect revisions are not exact
    # ballot counts and would give the writer a misleading second version.
    samples = {}
    for event in events:
        observed = event.get('after') or {}
        if (event['kind'] == 'manual_count_observations' and event.get('changed_votes')
                and observed.get('flow_percent') and observed.get('sampled_preference_ballots')):
            sample = {
                'description': observed.get('sample'), 'preference_ballots': observed['sampled_preference_ballots'],
                'sources': list(observed.get('sampled_first_preferences', {})),
                'destination_percent': observed['flow_percent'], 'revised': bool(event.get('before')),
                'use_in_simulations_confirmed': event.get('sample_used_in_draws', False)}
            totals = observed.get('sampled_candidate_totals', {})
            if len(totals) == 3 and totals.keys() == observed['flow_percent'].keys():
                # A full three-way tally includes the survivors' own primaries.
                # The two lowest totals identify the contest for second on
                # these ballots, even when a third party won most transfers.
                leaders = sorted(totals, key=totals.get, reverse=True)
                first, second, third = leaders
                if totals[first] > totals[second] > totals[third]:
                    sample['second_place'] = {'ahead': second, 'behind': third,
                                              'votes': totals[second] - totals[third]}
            samples[event['seat']] = sample
    if samples:
        events = [e for e in events if not (e.get('seat') in samples
                  and e['kind'] in ('manual_preferences', 'manual_count_observations'))]
    included = _selected_evidence(events, selection_order, current['seats'], comparison.get('replacement_tcp_counts', {}))
    seat_changes = []
    for seat in comparison['seat_changes']:
        if seat['seat'] not in selected:
            continue
        # Keep one central estimate per quantity and a few tail movements which
        # describe uncertainty. Repeating all fifteen percentiles adds tokens,
        # not independent information. Full changes remain in the package.
        central = []
        for row in seat['changes']:
            metric = row['metric']
            if metric.startswith(('/pairing_percent/', '/tcp_percentiles/')):
                pair = '/'.join(metric.split('/')[2:4])
                if not _same_pair(seat, pair) or _pair_likelihood(seat, pair) < .01:
                    continue
            if '/pairing_percent/' in metric or '/fp_mean_percent/' in metric or metric.endswith('/50'):
                central.append(row)
        # A large conditional margin change in a vanishingly unlikely pairing
        # should not displace evidence about plausible contests. This only
        # selects commentary context; it does not alter forecast probabilities.
        def importance(row):
            if row['metric'].startswith('/pairing_percent/'):
                return -_pairing_score(seat, row['change'])
            weight = 1
            if '/tcp_percentiles/' in row['metric']:
                pair = '/'.join(row['metric'].split('/')[2:4])
                weight = _pair_likelihood(seat, pair)
            return -abs(row['change'])*weight
        ranked = sorted(central, key=importance)[:4]
        already_included = {row['metric'] for row in ranked}
        ranked += [row for row in _tcp_interval_rows(seat) if row['metric'] not in already_included]
        # If a new count or a change in usable older counts helps explain a
        # seat result, keep the district median for those opponents alongside
        # that explanation, even when other movements fill the shortlist.
        # Only matched booth forecasts can set an expectation for a booth count.
        for evidence_kind in ('replacement_tcp_counts', 'retained_pair_revisions'):
            pair_evidence = comparison.get(evidence_kind, {}).get(seat['seat'])
            if not pair_evidence:
                continue
            pair = pair_evidence['pair']
            median_paths = {f'/tcp_percentiles/{a}/{b}/50' for a, b in (pair, pair[::-1])}
            included_paths = {row['metric'] for row in ranked}
            ranked += [row for row in seat['changes'] if row['metric'] in median_paths - included_paths]
        for row in seat['changes']:
            if row['metric'].startswith('/finalist_percent/'):
                party = row['metric'].rsplit('/', 1)[-1]
                if seat['before']['candidates'].get(party) == seat['after']['candidates'].get(party):
                    ranked.append(row)
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
            'replacement_tcp_counts': {s:v for s,v in comparison.get('replacement_tcp_counts', {}).items() if s in selected},
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
            'win_changes': _district_win_rows(comparison, selected, current['seats']),
            'seat_changes': seat_changes,
            'evidence': included, 'omitted_evidence_records': len(comparison['evidence'])-len(included)-len(matched_ids)}
    if samples:
        packet['reported_preference_samples'] = samples
    if comparison.get('noteworthy_booth_warnings'):
        packet['possible_booth_errors'] = comparison['noteworthy_booth_warnings']
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
    if comparison.get('tcp_reporting_changes'):
        reporting = comparison['tcp_reporting_changes']
        packet['tcp_reporting_changes'] = tcp_reporting.prompt_context(reporting)
        if reporting.get('earlier_booth_counts_match_statewide_drop'):
            packet['tcp_reporting_reconciliation'] = (
                'Earlier accepted booth counts in the reset districts account for the entire reported '
                'statewide TCP drop. Published candidate counts elsewhere did not change.')
    if progress:
        packet['district_fp_completion'] = progress
    # Give the writer the ranked expectation for an identifiable group only
    # when its reporting booths belong to a district already selected. Keep
    # tiny rechecks and the complete archive of other categories off the wire.
    groups = {seat: [row for row in comparison.get('ranked_tcp_groups', {}).get(seat, [])
                     if row['newly_counted'] == row['ballots'] >= 10] for seat in selection_order}
    if any(groups.values()):
        packet['ranked_tcp_groups'] = {seat: rows for seat, rows in groups.items() if rows}
    sources = comparison.get('district_preference_evidence', {})
    if sources:
        packet['district_preference_evidence'] = {
            s: {k: v for k, v in sources[s].items() if k != 'ordinary_offset_effect_by_source_log_odds_for_first'}
            for s in selection_order if s in sources}
    retained = comparison.get('retained_tcp_changes_while_published_zero', {})
    if retained:
        # The mechanical record retains individual units. Give the writer a
        # distinction about count status where relevant, without promoting
        # routine changes in eligibility into stories about withdrawn ballots.
        districts = [s for s in selection_order if s in retained]
        if districts:
            packet['retained_tcp_only_published_zero_at_both_ends'] = districts
        close = {s: row for s, row in comparison.get('close_contests_with_material_retained_tcp_changes', {}).items()
                 if s in selected}
        if close:
            packet['close_contests_with_material_retained_tcp_changes'] = close
        revisions = {s: row for s, row in comparison.get('retained_pair_revisions', {}).items() if s in selected}
        if revisions:
            packet['retained_pair_revisions'] = revisions
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

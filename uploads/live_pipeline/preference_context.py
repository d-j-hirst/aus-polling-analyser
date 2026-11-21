"""Trace a change in a district's estimated preferences back to counts elsewhere.

The simulation records the preference adjustment for each pair and vote type.
For ordinary booths we can repeat the small calculation that combines districts,
holding one district's evidence at its earlier value. This isolates its effect
on the shared preference estimate. Win chances also depend on local counting,
turnout, the order of exclusions and uncertainty.
"""
import csv
import math
from pathlib import Path


def ordinary_rows(evidence):
    pairs = {}
    for row in evidence.get('preferences', {}).values():
        if row.get('category') == 'Ordinary' and len(row.get('pair', ())) == 2:
            pair = '/'.join(map(str, row['pair']))
            pairs.setdefault(pair, {})[row['seat']] = row
    return pairs


def historical_types(election):
    """Earlier exports omitted geography. Look up the district classification
    used in preparation, then require it to reproduce the archived adjustment."""
    if not election:
        return {}
    path = Path(__file__).resolve().parents[2]/'analysis'/'Data'/'seat-types.csv'
    if not path.is_file():
        return {}
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return {row[0]: int(row[2]) for row in csv.reader(stream)
                if len(row) == 3 and row[1] == election[4:] and row[2] in ('0', '1', '2', '3')}


def district_inputs(rows, types):
    """One item represents a district supplying matched ordinary FP and TCP.
    LivePreferenceLearning::estimate gives each district less than one unit
    of weight. Older diagnostics export the total weight excluding each
    district and its local adjustment. Those identify its weight and mean:
    local adjustment = weight times (district mean minus shared estimate)."""
    donors = {s: r for s, r in rows.items() if r.get('local_matched_units', 0) > 0}
    if not donors:
        return {}
    if not all('other_district_weight' in r for r in rows.values()):
        return None
    totals = [r['other_district_weight'] for s, r in rows.items() if s not in donors]
    if totals:
        total = totals[0]
    elif len(donors) > 1:
        total = sum(r['other_district_weight'] for r in donors.values())/(len(donors)-1)
    else:
        total = None
    result = {}
    for seat, row in donors.items():
        weight = row.get('ordinary_district_weight')
        mean = row.get('ordinary_district_mean_offset')
        if weight is None or mean is None:
            if total is None or 'local_offset' not in row or 'ordinary_offset' not in row:
                return None
            weight = total-row['other_district_weight']
            if weight <= 0:
                return None
            mean = row['ordinary_offset']+row['local_offset']/weight
        geo = row.get('geographic_type', types.get(seat))
        if geo not in range(4) or not 0 < weight <= 1 or not math.isfinite(mean):
            return None
        result[seat] = (weight, mean, geo)
    return result


def shared(target, geo, inputs):
    """Repeat the ordinary-booth calculation from LivePreferenceLearning.h.
    Each step from inner metro towards rural retains 70% geographic weight.
    The prior has the weight of two districts; geography changes the mix of
    evidence while preserving the total amount. Positive favours finalist one."""
    total = comparable = numerator = 0
    for seat, (weight, mean, kind) in inputs.items():
        if seat == target:
            continue
        scaled = weight * .7**abs(kind-geo)
        total += weight
        comparable += scaled
        numerator += scaled*mean
    return (numerator/comparable * total/(2+total) if comparable else 0), total


def source_changes(target, old, new, previous_inputs, current_inputs, types):
    """Attribute only a reproducible shift. Replacing one donor at a time
    includes both changes in its preference split and in its influence as
    more booths report. These effects need not add up to the total change."""
    if previous_inputs is None or current_inputs is None:
        return None
    geo = new.get('geographic_type', types.get(target))
    if geo not in range(4) or old.get('geographic_type', types.get(target)) != geo:
        return None
    for row, inputs in ((old, previous_inputs), (new, current_inputs)):
        offset, weight = shared(target, geo, inputs)
        if (abs(offset-row['ordinary_offset']) > .00003
                or abs(weight-row['other_district_weight']) > .00005):
            return None
    changes = []
    current_offset = new['ordinary_offset']
    for donor in sorted((previous_inputs.keys() | current_inputs.keys()) - {target}):
        held = dict(current_inputs)
        if donor in previous_inputs:
            held[donor] = previous_inputs[donor]
        else:
            del held[donor]
        change = current_offset-shared(target, geo, held)[0]
        if abs(change) >= .001:  # ignore rounding and editorially tiny changes on the log-odds scale
            changes.append((change, donor))
    return changes


def extra_allocations(evidence):
    """Sum, across vote types, the additional ballots allocated to the first
    finalist by preference learning. Each category uses the same recorded
    FP ballots before and after applying its snapshot's learned rates."""
    totals = {}
    for row in evidence.get('preferences', {}).values():
        if ('adjusted_first_votes_from_pending_fp' not in row or 'prior_first_votes_from_pending_fp' not in row
                or len(row.get('pair', ())) != 2):
            continue
        key = row['seat'], '/'.join(map(str, row['pair']))
        extra_votes = row['adjusted_first_votes_from_pending_fp'] - row['prior_first_votes_from_pending_fp']
        totals[key] = totals.get(key, 0) + extra_votes
    return totals


def compare(previous, current, previous_evidence, current_evidence):
    """Find plausible explanations in competitive districts. Check that the
    shared preference estimate, expected allocation of still-unassigned FP
    ballots, and win chance within a likely pairing moved in the same direction.
    A district can affect the shared ordinary estimate even if its own race is safe."""
    old_pairs, new_pairs = ordinary_rows(previous_evidence), ordinary_rows(current_evidence)
    old_allocations, new_allocations = extra_allocations(previous_evidence), extra_allocations(current_evidence)
    types = historical_types(current.get('election')) if old_pairs and new_pairs else {}
    result = {}
    for pair in sorted(old_pairs.keys() & new_pairs.keys()):
        old, new = old_pairs[pair], new_pairs[pair]
        previous_inputs = district_inputs(old, types)
        current_inputs = district_inputs(new, types)
        parties = pair.split('/')
        for seat in sorted(old.keys() & new.keys()):
            before = previous.get('conditional_wins', {}).get(seat, {}).get(pair)
            after = current.get('conditional_wins', {}).get(seat, {}).get(pair)
            # Editorial thresholds: a point of win probability within a
            # majority-probability pairing, plus a shared offset of at least
            # .01 log odds (about a quarter point on an even preference split).
            if before is None or after is None or abs(after-before) < 1:
                continue
            levels = [s['seats'][seat] for s in (previous, current)]
            if any(level.get('pairing_percent', {}).get(pair, 0) < 50 for level in levels):
                continue
            if any(levels[0]['candidates'].get(p) != levels[1]['candidates'].get(p) for p in parties):
                continue
            delta = new[seat]['ordinary_offset']-old[seat]['ordinary_offset']
            if abs(delta) < .01 or delta*(after-before) <= 0:
                continue
            # Compare the additional votes estimated for party one versus
            # that snapshot's ranking prior, across all pending FP categories.
            # These are model estimates, which can also move with the FP mix.
            pending_before = old_allocations.get((seat, pair))
            pending_after = new_allocations.get((seat, pair))
            if pending_before is None or pending_after is None or (pending_after-pending_before)*delta <= 0:
                continue
            changes = source_changes(seat, old[seat], new[seat], previous_inputs, current_inputs, types)
            if not changes:
                continue
            direction = 1 if delta > 0 else -1
            ranked = sorted(((v*direction, s) for v,s in changes), reverse=True)
            strength, donor = ranked[0]
            # Require a source large enough to explain a material part of the
            # ordinary shift. Avoid naming a tiny contributor as the main story.
            if strength < abs(delta)*.25:
                continue
            item = {'pair': parties, 'ordinary_preference_shift_favours': parties[0 if delta > 0 else 1],
                    'strongest_supporting_district': donor,
                    'matched_ordinary_units_before_after': [rows.get(donor, {}).get('local_matched_units', 0)
                                                            for rows in (old, new)],
                    'holding_source_at_previous_evidence_reverses_direction': strength > abs(delta)+.0001,
                    'first_candidate_conditional_win_percent': [before, after],
                    'estimated_extra_first_candidate_votes_from_learning_on_pending_fp': [pending_before, pending_after]}
            if ranked[-1][0] < -max(.001, strength*.1):
                item['strongest_offsetting_district'] = ranked[-1][1]
            # Keep the complete list of material checks in the mechanical
            # comparison for inspection. Only the leading sources go to the writer.
            item['ordinary_offset_effect_by_source_log_odds_for_first'] = {
                s: change for change, s in sorted(changes, key=lambda row: (-abs(row[0]), row[1]))}
            result[seat] = item
    return result

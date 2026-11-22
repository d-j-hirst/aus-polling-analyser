"""Test the preceding state result as a source of within-district One Nation variation.

The big change in the party's level makes an old vote share a poor starting
level. An old booth's relative strength might still help. Test how much of
that pattern to retain alongside three nearby federal booths in SA, then
illustrate the choices with the Victorian district forecasts and turnout units.
"""

import argparse
import json
from collections import Counter, defaultdict
from dataclasses import replace
from pathlib import Path

import numpy as np

from evidence import BOOTH_CATEGORIES, HERE, ROOT, key, save_rows
from study import (LOCAL_LIMIT_KM, anchored, district_data, district_scores, federal_log_odds,
                   paired_interval, read_first_returns, read_rows, replay_scores, training_rules)


def preceding_votes(district, party):
    """Return formal and party ballots at earlier places only if a candidate stood."""
    candidates = [i for i, c in district['candidates'].items() if c['party'] == party]
    if not candidates:
        return {}
    candidate, = candidates
    booths = {}
    for name, booth in district['booths'].items():
        votes = booth['fp']
        if key(name) in booths:
            raise ValueError(f'Ambiguous historical booth name {name}')
        if candidate in votes and sum(votes.values()):
            booths[key(name)] = (votes[candidate], sum(votes.values()))
    return booths


def state_signal(previous, names):
    """A missing candidacy or unmatched venue supplies no old-state comparison.

    The half-ballot adjustment is for observed counts, including a genuine
    zero. A district in which the party did not stand never receives it.
    """
    return np.array([federal_log_odds('{}/{}'.format(*previous[key(n)]))
                     if key(n) in previous else np.nan for n in names])


def historical_difference(federal, state, sizes):
    """Find the part of the old local pattern that differs from the federal one.

    Among venues with both sources, compare the two log-odds patterns after
    removing their difference in average level. Pre-count size estimates set
    that average. Adding half the result is a 50/50 blend of local patterns.
    Where a comparison is missing the federal expectation carries through.
    """
    matched = np.isfinite(federal) & np.isfinite(state) & (sizes > 0)
    difference = np.zeros(len(sizes))
    if matched.any():
        gap = state[matched] - federal[matched]
        difference[matched] = gap - np.average(gap, weights=sizes[matched])
    return difference


def fitted_state_weight(districts, historical, federal_strength):
    """Learn whether the old local pattern improves the federal one in training seats.

    Each previous-candidacy district contributes equally. Within it, weight by
    final formal counts at matched booths. The coefficient is the fraction of
    the federal-to-state difference explaining the remaining 2026 error. Limit
    it to 0–1 so the result interpolates between the two candidate patterns.
    """
    agreement = variation = 0.
    for d in districts:
        federal = federal_strength * d.x['three-10km']
        state = historical[d.seat]
        keep = np.isfinite(federal) & np.isfinite(state) & (d.formal > 0)
        if np.count_nonzero(keep) < 2:
            continue
        weights = d.formal[keep] / d.formal[keep].sum()
        predictor = state[keep] - federal[keep]
        actual = np.log((d.on[keep] + .5) / (d.formal[keep] - d.on[keep] + .5))
        residual = actual - federal[keep]
        predictor -= weights @ predictor
        residual -= weights @ residual
        variation += weights @ (predictor ** 2)
        agreement += weights @ (predictor * residual)
    return float(np.clip(agreement / variation, 0, 1)) if variation > 1e-12 else 0.


def south_australian_test(inputs, output):
    """Compare the two sources in the same districts and the same real first returns."""
    districts = {k: d for k, d in district_data(read_rows(inputs / 'sa-booths.csv')).items()
                 if d.mode == 'ordinary'}
    state_2022 = json.loads((ROOT / 'analysis/Booth Results/2022sa.json').read_bytes())
    historical, old_levels = {}, {}
    for d in districts.values():
        predecessor = 'Frome' if d.seat == 'Ngadjuri' else d.seat
        votes = preceding_votes(state_2022[predecessor], 'ONP')
        historical[d.seat] = state_signal(votes, d.names)
        if votes:
            old_levels[d.seat] = sum(a for a, _ in votes.values()) / sum(n for _, n in votes.values())
        if bool(votes) != d.prior_candidate:
            raise ValueError(f'Inconsistent 2022 candidacy for {d.seat}')

    # Withhold a whole federal area from both coefficients. Giving every rule
    # the same test-district level isolates the geographic pattern. The replay
    # obtains that level instead from the actual first reported booths.
    evaluated, rules, coefficients = {}, {}, []
    for area in sorted({d.area for d in districts.values()}):
        training = [d for d in districts.values() if d.area != area]
        strength = training_rules(training)['three'][1]
        weight = fitted_state_weight(training, historical, strength)
        coefficients.append(dict(held_out=area, federal_strength=strength, state_weight=weight))
        for k, d in districts.items():
            if d.area != area:
                continue
            federal = strength * d.x['three-10km']
            signals = {'uniform': np.zeros(len(d.names)), 'federal': federal}
            if d.prior_candidate:
                state = historical[d.seat]
                difference = historical_difference(federal, state, d.sizes)
                signals.update(state_only=state, equal_blend=federal + .5 * difference,
                               fitted_blend=federal + weight * difference)
            evaluated[k] = replace(d, x=signals)
            rules[k] = {label: (label, 1.) for label in signals}

    controlled = district_scores(evaluated, rules, 'federal_area')
    live = [r for r in read_first_returns(inputs) if r['mode'] == 'ordinary']
    replay = replay_scores(evaluated, rules, live)
    cases = [(r['seat'], 'booth', r['rule'], r['mae']) for r in controlled]
    cases += [(r['seat'], f"remaining_after_{r['stage']}", r['rule'], abs(r['error_pp']))
              for r in replay if r['weights'] == 'prior']
    reference = {(s, test): error for s, test, rule, error in cases if rule == 'federal'}
    groups = defaultdict(list)
    for seat, test, rule, error in cases:
        d = evaluated[seat, 'ordinary']
        group = 'stood' if d.prior_candidate else 'absent'
        entry = d.area, error, reference[seat, test] - error
        groups[group, test, rule].append(entry)
        # The largest former share in the five Victorian seats was 6.12%.
        # This subset asks whether the test travels to such small starting
        # shares. Its outcomes play no part in fitting or choosing a weight.
        if d.prior_candidate and old_levels[seat] <= .062:
            groups['stood_at_most_6.2pct', test, rule].append(entry)
    summary = []
    for (prior, test, rule), values in sorted(groups.items()):
        areas, errors, gains = zip(*values)
        lo, hi = paired_interval(gains, areas)
        summary.append(dict(previous_state_result=prior, test=test, method=rule, districts=len(values),
                            mean_absolute_error_pp=round(np.mean(errors), 4), gain_vs_federal_pp=round(np.mean(gains), 4),
                            gain_low_pp=round(lo, 4), gain_high_pp=round(hi, 4),
                            districts_better_than_federal=sum(g > 1e-9 for g in gains)))
    save_rows(output / 'baseline-comparisons.csv', summary)
    strength = training_rules(list(districts.values()))['three'][1]
    weight = fitted_state_weight(districts.values(), historical, strength)
    coefficients.append(dict(held_out='none', federal_strength=strength, state_weight=weight))
    save_rows(output / 'baseline-strengths.csv',
              [dict(held_out=r['held_out'], federal_strength=round(r['federal_strength'], 6),
                    state_weight=round(r['state_weight'], 6)) for r in coefficients])
    both = [d for d in districts.values() if d.prior_candidate]
    matched = sum(np.count_nonzero((d.formal > 0) & np.isfinite(historical[d.seat])) for d in both)
    print(f'{matched} of {sum(np.count_nonzero(d.formal) for d in both)} positive ordinary booths '
          f'in the {len(both)} returning SA districts match their 2022 state result.')
    return strength, weight


def compare_sa_vote_types(folder, output):
    """Check the difference between a whole-district and a voting-mode share.

    Use ECSA's final counts for every mode in the district denominator. Compare
    the named ordinary booths, named early centres and postal batches with that
    overall result. Give each district one observation for each type. The check
    describes SA at a high party vote and does not set a Victorian mode offset.
    """
    static = json.loads((folder / 'static.json').read_bytes())
    final = json.loads((folder / 'results.json').read_bytes())
    candidates = {d['districtName']: next(c['candidateId'] for c in d['candidates'] if c['partyId'] == 'ONP')
                  for d in static['districts']}
    if int(final['dataVersion']) != 912 or final['electionStatus'] != 'final' or len(candidates) != 47:
        raise ValueError('Expected the reviewed 47-district SA version 912 return')
    gaps = defaultdict(list)
    for district in final['districts']:
        candidate = candidates[district['districtId']]
        on, formal = Counter(), Counter()
        for b in district['pollingPlaces']:
            mode = BOOTH_CATEGORIES[b['pollingPlaceTypeIdName']]
            for c in b['pollingCandidates']:
                formal[mode] += c['formalVotes']
                if c['candidateId'] == candidate:
                    on[mode] += c['formalVotes']
        for section in ('declarations', 'absentOrdinary'):
            for batch in district[section]:
                mode = 'postal' if batch.get('declarationType', '').split(' - ')[0] == 'Postal' else 'other'
                formal[mode] += batch['formalVotes']
                on[mode] += sum(c['votes'] for c in batch['candidateVotes'] if c['candidateId'] == candidate)
        all_votes = sum(on.values()) / sum(formal.values())
        for mode in ('election_day_ordinary', 'early_in_person', 'postal'):
            gaps[mode].append(100 * (on[mode] / formal[mode] - all_votes))
    summary = []
    for mode, values in gaps.items():
        low, middle, high = np.percentile(values, [10, 50, 90])
        summary.append(dict(mode=mode, districts=len(values), median_difference_pp=round(middle, 4),
                            tenth_percentile_pp=round(low, 4), ninetieth_percentile_pp=round(high, 4)))
    save_rows(output / 'baseline-vote-types.csv', summary)


def victorian_examples(inputs, output, report_path, federal_strength, state_weight):
    """Exercise the allocation at the published 20 November district medians.

    Each published median covers all voting modes. For this illustration set
    the ordinary target to that median and use the live turnout prior's
    relative sizes. Actual integration needs the ordinary target consistent
    with the other categories in the live no-count baseline.
    """
    report = json.loads(report_path.read_bytes())['report']
    if (report['reportDate'], report['reportMode'], report['termCode']) != ('2026-11-20T03:10:24', 'RF', '2026vic'):
        raise ValueError('Expected the archived 20 November 2026 Victorian general forecast')
    party, = (i for i, code in report['partyAbbr'] if code == 'ONP')
    mid = report['probabilityBands'].index(50)
    medians = {s: dict(bands)[party][mid] / 100 for s, bands in zip(report['seatNames'], report['seatFpBands'])}
    history = json.loads((ROOT / 'analysis/Booth Results/2022vic.json').read_bytes())
    folder = ROOT / 'forecasts/2026vic/live-inputs'
    turnout = json.loads((folder / 'turnout-prior.json').read_bytes())
    weights = {(turnout['prior']['seats'][u['seat']], u['name']): u['weight'] for u in turnout['units']
               if u['kind'] == 'ordinary' and not (u['closed'] or u['excluded'])}
    by_seat = defaultdict(list)
    for r in read_rows(inputs / 'operator/vic-booths.csv'):
        if r['mode'] == 'ordinary':
            if r['standing'] != 'True':
                raise ValueError(f"The archived illustration requires a One Nation candidate in {r['seat']}")
            by_seat[r['seat']].append(r)
    if set(by_seat) != set(medians) or len(medians) != 88:
        raise ValueError('The roster and 88-district pre-election forecast differ')
    if {(r['seat'], r['booth']) for rows in by_seat.values() for r in rows} != set(weights):
        raise ValueError('The research and active ordinary turnout rosters differ')

    # Use only a comparison identified in this same district's earlier data.
    # Narracan's size prior uses pooled November rates, but its detailed
    # supplementary election still supplies actual named candidate counts.
    audit = json.loads((folder / 'turnout-input-audit.json').read_bytes())
    matches = {(d['seat'], b['name']): b['historical_match']['name'] for d in audit['districts']
               for b in d['ordinary_booths'] if b['historical_match'] and b['historical_match']['seat'] == d['seat']}
    summaries, places = [], []
    for seat, rows in sorted(by_seat.items()):
        old = preceding_votes(history[seat], "PAULINE HANSON'S ONE NATION")
        names = [r['booth'] for r in rows]
        state = state_signal(old, [matches.get((seat, n), '') for n in names])
        sizes = np.array([weights[seat, n] for n in names])
        if abs(sizes.sum() - 1) > 1e-6:
            raise ValueError(f'Live turnout unit weights do not sum to one for {seat}')
        federal = np.array([federal_strength * federal_log_odds(r['three']) if r['nearest_km']
                            and float(r['nearest_km']) <= LOCAL_LIMIT_KM else np.nan for r in rows])
        target = medians[seat]
        signals = {'federal_pct': federal,
                   'blend_pct': federal + state_weight * historical_difference(federal, state, sizes)}
        if old:
            signals['state_only_pct'] = state
        shares = {label: 100 * anchored(offsets, sizes, target)[0] for label, offsets in signals.items()}
        if any(abs(np.average(v, weights=sizes) - 100 * target) > 1e-8 for v in shares.values()):
            raise ValueError(f'Geography changed the target in {seat}')
        # The original illustration uses the VEC venue workbook, whereas the
        # live turnout model has its own relative expected volumes. Measure
        # how far the district would move if the two were used inconsistently.
        venue_sizes = np.array([float(r['prior_size']) for r in rows])
        venue_centred, _ = anchored(federal, venue_sizes, target)
        mixed_weight_shift = 100 * (np.average(venue_centred, weights=sizes) - target)
        old_formal = sum(formal for _, formal in old.values())
        old_pct = 100 * sum(on for on, _ in old.values()) / old_formal if old_formal else None
        summaries.append(dict(seat=seat, former_state_all_votes_pct=round(old_pct, 3) if old else '',
                              current_forecast_median_pct=round(100 * target, 3), ordinary_venues=len(rows),
                              matched_former_on_venues=int(np.isfinite(state).sum()),
                              mixed_turnout_weight_shift_pp=round(mixed_weight_shift, 4),
                              federal_min_pct=round(min(shares['federal_pct']), 3),
                              federal_max_pct=round(max(shares['federal_pct']), 3),
                              blend_min_pct=round(min(shares['blend_pct']), 3), blend_max_pct=round(max(shares['blend_pct']), 3)))
        for i, r in enumerate(rows):
            places.append(dict(seat=seat, booth=r['booth'], turnout_weight=round(sizes[i], 8),
                               assumed_ordinary_pct=round(100 * target, 4),
                               federal_pct=round(shares['federal_pct'][i], 4),
                               blend_pct=round(shares['blend_pct'][i], 4),
                               state_only_pct=round(shares['state_only_pct'][i], 4) if old else ''))
    save_rows(output / 'baseline-districts.csv', summaries)
    save_rows(output / 'operator/baseline-booths.csv', places)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--inputs', type=Path, default=HERE)
    parser.add_argument('--out', type=Path, default=HERE)
    parser.add_argument('--victoria', action='store_true', help='Also run the illustration on the prepared Victorian inputs')
    parser.add_argument('--report', type=Path, default=HERE / 'raw/20261120-regular.json')
    parser.add_argument('--sa-final', type=Path, help='Also compare whole-seat and category shares from reviewed SA source files')
    args = parser.parse_args()
    strength, weight = south_australian_test(args.inputs, args.out)
    if args.sa_final:
        compare_sa_vote_types(args.sa_final, args.out)
    if args.victoria:
        victorian_examples(args.inputs, args.out, args.report, strength, weight)

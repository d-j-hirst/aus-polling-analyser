"""Compare two proposed changes with the split booth update on frozen replays.

Run from the repository root with the analysis Python and pass a replay folder
name and an output filename ending in .json.gz. The output has one short record
per seat and capture. Final results are used only in scoring. No GUI, fitting
job, download or new forecast is required.
"""
import argparse
from collections import Counter, defaultdict
import gzip
import json
from pathlib import Path
from statistics import NormalDist
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lib.live_analysis_archive import load_json
from scripts.exploratory.booth_transfer_trial import changes
from scripts.exploratory.nonmajor_booth_response import projection_inputs
from scripts.exploratory.score_booth_fp_update import (
    ROOT, actual_vector, group_votes, official_results, opening_backgrounds,
    read, shared_background, snapshot,
)

NORMAL = NormalDist()
TARGETS = {'Shepparton': 'NAT', 'Bendigo': 'NAT', 'Mildura': 'IND'}


def pair_rows(report, field, index, key):
    return {(str(r['party_a']['party_index']), str(r['party_b']['party_index'])): r[key]
            for r in report[field][index]}


def pairing_trial(frequencies, bands, levels, parties, delta, correlation):
    """Sensitivity to exchanging the runner-up in two common final pairings.

    Anchor at the published probability. Move the unobserved exclusion margin
    by the change in the two rivals' FP gap, keeping inherited preferences
    fixed. Its approximate scale comes from their FP intervals and an assumed
    correlation. Never invent a pairing the archive did not measure.
    """
    result = frequencies.copy()
    ordered = sorted(frequencies, key=frequencies.get, reverse=True)
    if len(ordered) < 2:
        return result, False
    a, b = ordered[:2]
    mass = frequencies[a] + frequencies[b]
    if mass < .90 or frequencies[b] < .01 or len(set(a) & set(b)) != 1:
        return result, False
    x, = set(a) - set(b)
    y, = set(b) - set(a)
    if not all(p in parties and p in bands for p in (x, y)):
        return result, False
    sd = [(bands[p][levels.index(90)] - bands[p][levels.index(10)]) / (2 * NORMAL.inv_cdf(.90))
          for p in (x, y)]
    gap_sd = np.sqrt(sd[0]**2 + sd[1]**2 - 2 * correlation * sd[0] * sd[1])
    if gap_sd <= 0:
        return result, False
    movement = delta[parties.index(x)] - delta[parties.index(y)]
    new = NORMAL.cdf(NORMAL.inv_cdf(frequencies[a] / mass) + movement / gap_sd)
    result[a], result[b] = mass * new, mass * (1 - new)
    return result, True


def conditional_projection(pair, bands, levels, chance, parties, labels, fp, delta, booths, counts, total):
    """Shift a conditional TCP distribution using the uncounted primary mix.

    Preserve directly matched TCP and infer the pooled preference rate from
    the archived median on the remaining ballots. Against Labor a second
    Coalition party transfers mainly to its partner (80% in this sensitivity).
    Differences between individual minor sources and changes in which draws
    produce the pairing cannot be recovered from marginal percentile exports.
    """
    if not set(pair) <= set(parties):
        return None
    j, k = [parties.index(p) for p in pair]
    matched = np.zeros_like(fp)
    confirmed = 0.
    for b, primaries in zip(booths, counts):
        tcp = Counter()
        aliases = b.get('count_to_projection_ids', {})
        for p, v in b['tcp_votes_current'].items():
            tcp[aliases.get(p, p)] += v
        weight = min(primaries.sum(), sum(tcp.values()))
        if set(tcp) == set(pair) and weight > 0:
            matched += primaries * (weight / primaries.sum())
            confirmed += weight * tcp[pair[0]] / sum(tcp.values())
    free = fp - 100 * matched / total
    rates = np.full(len(fp), np.nan)
    rates[j], rates[k] = 1., 0.
    names = [labels[p] for p in pair]
    if 'ALP' in names and set(names) & {'LIB', 'NAT'}:
        for index, p in enumerate(parties):
            if p not in pair and labels[p] in ('LIB', 'NAT'):
                rates[index] = .2 if names[0] == 'ALP' else .8
    pool = np.isnan(rates)
    median = bands[levels.index(50)]
    available = free[pool].sum()
    if available <= 0:
        return None
    rate = (median - 100 * confirmed / total - free[~pool] @ rates[~pool]) / available
    if not 0 <= rate <= 1:
        return None
    rates[pool] = rate
    movement = delta @ rates
    # Use the archive's win frequency at 50% exactly. Percentile interpolation
    # estimates only the incremental change. Bound at the outer available
    # quantiles, whose tails are only resolved to one draw in a thousand.
    loss = 100 - chance
    keep = [(v, p) for v, p in zip(bands, levels)
            if (v < 50 and p < loss) or (v > 50 and p > loss)]
    anchors = sorted(keep + [(50., loss)])
    return median + movement, 100 - np.interp(50 - movement, *zip(*anchors))


def trial_primaries(published, observed, effects, weights):
    """Apply differences to the published forecast's outstanding ballot pool.

    The native simulator blends the ordinary fit back with other evidence as
    other votes report. Match that candidate-specific fraction from the export.
    The current replay is exactly the zero-change control. Cap any proposed
    reduction at the counted floor and rebalance only the outstanding voters.
    """
    available = np.maximum(0., published - observed)
    result = [published]
    for effect in (effects[0], effects[1], effects.sum(axis=0)):
        change = effect * weights
        if not np.any(change) or not available.sum():
            result.append(published)
            continue
        revised = np.maximum(0., available + change)
        revised *= available.sum() / revised.sum()
        result.append(published + revised - available)
    return np.array(result)


def score_district(context, seat, index, opening, current, places, following, report, native):
    parties, base, prior = context['backgrounds'][seat]
    labels = context['labels']
    bs, opening_mix, counts, ordinary, rem = projection_inputs(places, base, parties)
    sizes = counts.sum(axis=1)
    use = ordinary & (sizes > 0)
    total = sizes.sum() + rem.sum()
    if not use.any() or total <= 0:
        return
    # The historical robustness sample compares reconstructions only while
    # the ordinary fit is the sole local FP evidence. No later model's median
    # is mixed with older published probabilities.
    if native is None and sizes[~ordinary].sum():
        return
    district = current['seats'][seat]
    shared, shapes = shared_background(prior, opening_mix, (opening['seats'][seat], district), parties)
    effects, control, adjustments = changes(bs, opening_mix, shapes, counts, ordinary, rem,
                                            shared, parties, district['pairing_percent'], context['reversal_only'])
    fraction = sizes[use].sum() / sizes.sum()
    reconstructed = 100 * (counts.sum(axis=0) + rem @ control) / total
    if native is None:
        weights = np.full(len(prior), fraction)
        published = reconstructed
    else:
        exported = {str(r['party_index']): r['ordinary_fraction'] for r in native
                    if r.get('fitted_remaining_share') is not None}
        weights = np.array([exported.get(p, 0.) for p in [*parties, '-1']])
        published = group_votes(district['fp_mean_percent'], parties, {})
    observed = 100 * counts.sum(axis=0) / total
    forecasts = trial_primaries(published, observed, effects, weights)
    truth, finalists = actual_vector(context['truth'][seat], parties, labels, opening['seats'][seat]['candidates'])
    row = dict(seat=seat, time=current['source_time'], n=int(use.sum()), count=round(100 * sizes.sum()/total, 2),
               eligible=bool(weights.any()), fp_error=list(abs(forecasts - truth).sum(axis=1) / 2))
    focus = next((p for p in parties if labels[p] == TARGETS.get(seat)), None)
    if focus:
        j = parties.index(focus)
        row['candidate_fp'] = list(forecasts[:, j])
        row['final_fp'] = truth[j]

    # Check the actual FP at the next set of previously uncounted locations.
    # Each location enters a comparison at its first report in this sequence.
    # Score the two mechanisms in their respective vote types.
    arrivals = {}
    for i, booth in enumerate(bs):
        if sizes[i] or not rem[i]:
            continue
        key = (seat, booth['vote_type'], booth['booth_type'], booth['name'])
        new = following.get(key)
        if not new or key in context['seen']:
            continue
        votes = group_votes(new['fp_votes_current'], parties, new.get('count_to_projection_ids', {}))
        weight = votes.sum()
        if weight <= 0:
            continue
        comparison = 'ordinary' if ordinary[i] else 'other'
        sums = arrivals.setdefault(comparison, np.zeros(3))
        actual = votes / weight
        sums += [weight, weight * 50 * abs(control[i] - actual).sum(),
                 weight * 50 * abs(control[i] + adjustments[i] - actual).sum()]
    row['arrivals'] = {key: list(v) for key, v in arrivals.items()}
    if native is None:
        return row

    # The FP outcome never selects a pairing rule. Actual finalists are used
    # only to identify which forecast event eventually occurred for scoring.
    freqs = pair_rows(report, 'seat_tcp_scenario_percent', index, 'value')
    fp_bands = {str(r['party_index']): r['values'] for r in report['seat_fp_probability_band'][index]}
    bands = pair_rows(report, 'seat_tcp_probability_band', index, 'values')
    wins = pair_rows(report, 'seat_tcp_win_percent', index, 'value')
    levels = report['probability_bands']
    if len(finalists) != 2 or any(p is None for p, _, _ in finalists):
        return row
    actual_pair = frozenset(p for p, _, _ in finalists)
    winner = max(finalists, key=lambda r:r[2])[0]
    mass = sum(freqs.values())
    if abs(mass - 1.) > .01:
        return row
    # The archive omits very rare pairings and rounds frequencies for export.
    # Reserve a small unlisted-pair bucket, scaling down only if rounding
    # took the sum slightly over one.
    if mass > 1.:
        freqs = {p: v/mass for p, v in freqs.items()}
    unlisted = max(0., 1.-mass)
    result_key = next((p for p in freqs if frozenset(p) == actual_pair), None)
    for correlation in (-.35, 0., -.65):
        estimates = [pairing_trial(freqs, fp_bands, levels, parties, f-published, correlation) for f in forecasts]
        if correlation == -.35:
            row['pair_adjustable'] = estimates[0][1]
            row['p_final_pair'] = [p.get(result_key, 0.) for p, _ in estimates]
            if focus:
                row['p_reaches'] = [sum(v for pair, v in p.items() if focus in pair) for p, _ in estimates]
                primary_pairs = [p for p, _ in estimates]
        row['pair_brier_'+str(correlation)] = [
            sum(v*v for v in p.values()) + unlisted**2 + 1 - 2*p.get(result_key, unlisted)
            for p, _ in estimates]
    if focus:
        archived_win = next((r['value'] for r in report['seat_party_win_percent'][index]
                             if str(r['party_index']) == focus), 0.)
        revised_win = np.full(4, float(archived_win))
        for p in freqs:
            if focus not in p or p not in wins or p not in bands:
                continue
            for m, forecast in enumerate(forecasts):
                outcome = conditional_projection(p, bands[p], levels, wins[p], parties, labels,
                          published, forecast-published, bs, counts, total)
                # An unavailable transfer rate preserves the archived conditional
                # chance for the purpose of weighting the changed finalist mix.
                old = wins[p] if p[0] == focus else 100-wins[p]
                new = old if outcome is None else (outcome[1] if p[0] == focus else 100-outcome[1])
                revised_win[m] += primary_pairs[m][p]*new - freqs[p]*old
        row['seat_win'] = list(np.clip(revised_win, 0, 100))
    if result_key in bands and result_key in wins and freqs[result_key] >= .01:
        conditional = [conditional_projection(result_key, bands[result_key], levels, wins[result_key],
                       parties, labels, published, f-published, bs, counts, total) for f in forecasts]
        if all(v is not None for v in conditional):
            med, chance = np.array(conditional).T
            if winner != result_key[0]:
                chance = 100 - chance
            row['conditional_brier'] = list((1 - chance/100)**2)
            row['base_conditional'] = chance[0]
            row['actual_pair_mass'] = freqs[result_key]
            if focus in result_key:
                row['conditional_focus'] = list(chance if focus == winner else 100 - chance)
                row['tcp_focus'] = list(med if focus == result_key[0] else 100 - med)
    return row


def evaluate(run, archived=False, reversal_only=False):
    paths = sorted((ROOT/'live_runs'/run).glob('snapshot_*.json'))
    truth = official_results(run.split('-')[0])
    opening, initial = snapshot(paths[0])
    skipped = Counter()
    backgrounds, labels = opening_backgrounds(opening, initial, truth, skipped)
    seen = set()
    context = dict(truth=truth, backgrounds=backgrounds, labels=labels, seen=seen, reversal_only=reversal_only)
    rows = []
    current, places = opening, initial
    for step, path in enumerate(paths):
        later = snapshot(paths[step + 1]) if step + 1 < len(paths) else None
        def counted(all_places):
            return {(n, b['vote_type'], b['booth_type'], b['name']): b for n, v in all_places.items()
                    for b in v.values() if sum(b['fp_votes_current'].values())}
        seen.update(counted(places))
        following = counted(later[1]) if later else {}
        report = read(path)['simulation_report']
        inputs = {} if archived else {s['name']: s['fp_integration'].get('ordinary_comparisons', [])
                   for s in load_json(path.with_suffix('.analysis.json.gz'))['seats'] if s.get('fp_integration')}
        for i, name in enumerate(report['seat_name']):
            if name not in backgrounds:
                continue
            try:
                row = score_district(context, name, i, opening, current, places[name], following,
                                     report, None if archived else inputs.get(name, []))
            except ValueError as e:
                if str(e) not in ('Missing opening booth','A counted party has no opening support at its booth.',
                                  'Booth fit did not converge.'):
                    raise
                skipped[str(e)] += 1
                continue
            if row:
                rows.append(row)
        if later:
            current, places = later
    return dict(run=run, historical=archived, reversal_only=reversal_only,
                captures=len(paths), rows=rows, skipped=dict(skipped))


def summarise(paths):
    """Give each district equal weight and hold the three design seats out.

    Within a district average the saved captures. For the next-booth test
    average by the number of newly counted ballots first. Mixing builds or
    sensitivity settings would count some captures more than once.
    """
    metrics = defaultdict(lambda: defaultdict(list))
    configurations, runs = set(), set()
    for path in paths:
        with gzip.open(path, 'rt', encoding='utf-8') as source:
            data = json.load(source)
        configurations.add((data['historical'], data['reversal_only']))
        if len(configurations) != 1 or data['run'] in runs:
            raise ValueError('Summarise distinct runs with the same build type and trial settings.')
        runs.add(data['run'])
        for row in data['rows']:
            seat = (data['run'].split('-')[0], row['seat'])
            if seat in {('2022vic','Mildura'), ('2022vic','Shepparton'), ('2025fed','Bendigo')}:
                continue
            for key in ('fp_error','pair_brier_-0.35','conditional_brier'):
                if key in row:
                    metrics[key][seat].append(row[key])
            for kind, value in row['arrivals'].items():
                metrics[kind][seat].append(value)
    for metric, seats in metrics.items():
        values = []
        for observations in seats.values():
            if metric in ('ordinary', 'other'):
                weight, old, revised = np.sum(observations, axis=0)
                values.append([old/weight, revised/weight])
            else:
                values.append(np.mean(observations, axis=0))
        print(metric, len(seats), 'districts', sum(map(len, seats.values())),
              'comparisons', np.round(np.mean(values, axis=0), 6))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', nargs='?')
    parser.add_argument('output', nargs='?', type=Path)
    parser.add_argument('--summarise', nargs='+', type=Path, metavar='RESULT',
                        help='Print district-weighted control and alternative scores from saved trial results')
    parser.add_argument('--historical', action='store_true', help='Reconstruct FP on an older build before other vote types reported')
    parser.add_argument('--reversal-only', action='store_true', help='Require both booth diagnostics to reverse direction for restraint')
    args = parser.parse_args()
    if args.summarise:
        summarise(args.summarise)
        sys.exit()
    if not args.run or not args.output:
        parser.error('Specify a replay name and result filename, or use --summarise.')
    study = evaluate(args.run, args.historical, args.reversal_only)
    with gzip.open(args.output, 'wt', encoding='utf-8') as dest:
        json.dump(study, dest, allow_nan=False, separators=(',', ':'))
    print(args.run, study['captures'], 'captures', len(study['rows']), 'seat/captures', study['skipped'])

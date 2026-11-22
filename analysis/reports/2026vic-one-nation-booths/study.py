"""Measure the contribution of federal geography to One Nation booth estimates.

Compare booths *within* a state district and voting mode. The completed-district
test gives every method the same district result. The election-night test
instead learns that level from the booths actually reporting at that time.
In both tests the transfer strength comes from geographically held-out data.
"""

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.optimize import brentq
from scipy.special import expit

from evidence import FIRST_RETURNS, HERE, METHODS, save_rows


# Only three SA ordinary booths have their closest federal site over 10 km
# away. Test holding such booths at the district level as a precaution when
# transferring the result to Victoria's more numerous remote sites.
LOCAL_LIMIT_KM = 10


def read_rows(path):
    with path.open(encoding='utf-8', newline='') as handle:
        return list(csv.DictReader(handle))


def read_first_returns(inputs):
    """Load the operator's recorded observations for the election-night replay."""
    path = inputs / FIRST_RETURNS
    if not path.is_file():
        raise FileNotFoundError(f'{path} is required to rerun the replay. Prepare it with record.py --feeds using the SA capture directory.')
    return read_rows(path)


def federal_log_odds(pair):
    """Translate federal votes into log odds of a ballot being for One Nation.

    Moving from 3% to 6% is a larger relative change than moving from 23% to
    26%. Log odds express this distinction and let the state election have a
    different overall level. Half a ballot on each side keeps a zero or
    unanimous observed count usable. A blank is missing evidence.
    """
    if not pair:
        return np.nan
    on, formal = map(int, pair.split('/'))
    if not 0 <= on <= formal or formal <= 0:
        raise ValueError(f'Invalid federal count {pair}')
    return np.log((on + .5) / (formal - on + .5))


def anchored(offsets, weights, target):
    """Convert relative local strengths to shares at a supplied weighted mean.

    Add a common amount to every located booth's log odds until its group
    reproduces the target on the actual vote-share scale. A booth with no
    local evidence receives the supplied share. The same intercept can then
    predict the still-unreported booths when only the reported group sets it.
    """
    if not 0 <= target <= 1 or np.any(weights < 0) or weights.sum() <= 0:
        raise ValueError('A group needs a valid share and positive total weight')
    if target in (0, 1):
        return np.full(len(offsets), target), -np.inf if target == 0 else np.inf
    located = np.isfinite(offsets) & (weights > 0)
    prediction = np.full(len(offsets), float(target))
    if not located.any():
        return prediction, np.nan
    values, sizes = offsets[located], weights[located]
    # The solution lies between the intercepts that assign the target share
    # to the highest- and lowest-predicted booth individually.
    log_odds = np.log(target / (1 - target))
    low, high = log_odds - values.max(), log_odds - values.min()
    intercept = low if high - low < 1e-12 else brentq(
        lambda a: np.average(expit(a + values), weights=sizes) - target,
        low - 1e-10, high + 1e-10)
    known = np.isfinite(offsets)
    prediction[known] = expit(intercept + offsets[known])
    return prediction, intercept


@dataclass
class District:
    seat: str
    mode: str
    area: str
    prior_candidate: bool
    names: list
    on: np.ndarray
    formal: np.ndarray
    sizes: np.ndarray
    x: dict
    moments: dict


def district_data(rows):
    """Keep every rostered booth for replay, including any with no final votes.

    For fitting, compare log odds with their own district/mode average. Each
    district contributes equally, with more weight on larger counts inside
    it. The sums of products measure how strongly the state and federal
    deviations line up. They can be pooled across the training districts
    without including the district being predicted.
    """
    groups = defaultdict(list)
    for r in rows:
        groups[r['seat'], r['mode']].append(r)
    districts = {}
    for (seat, mode), booths in groups.items():
        on = np.array([int(r['on']) for r in booths])
        formal = np.array([int(r['formal']) for r in booths])
        sizes = np.array([float(r['prior_size']) for r in booths])
        x = {m: np.array([federal_log_odds(r[m]) for r in booths]) for m in METHODS}
        if any(not np.all(np.isfinite(v)) for v in x.values()):
            raise ValueError(f'Incomplete calibration geography for {seat} / {mode}')
        if np.any(on < 0) or np.any(on > formal):
            raise ValueError(f'Invalid first-preference votes for {seat} / {mode}')
        moments = {}
        if np.count_nonzero(formal) >= 2:
            weights = formal / formal.sum()
            state = np.log((on + .5) / (formal - on + .5))
            state -= weights @ state
            for method, signal in x.items():
                local = signal - weights @ signal
                moments[method] = weights @ (local * local), weights @ (local * state)
        nearby = np.array([float(r['nearest_km']) <= LOCAL_LIMIT_KM for r in booths])
        x['three-10km'] = np.where(nearby, x['three'], np.nan)
        districts[seat, mode] = District(seat, mode, booths[0]['federal_area'],
                                         booths[0]['prior_candidate'] == 'True',
                                         [r['booth'] for r in booths], on, formal, sizes, x, moments)
    return districts


def training_rules(groups):
    """Estimate one transfer strength per method using only the training groups.

    A strength of one retains the federal difference in log odds. Zero leaves
    every booth at the same share. We require a nonnegative relationship.
    The selectable rule chooses the candidate reducing the most squared
    within-district log-odds error in the training data. Every aspect of that
    choice, including its strength, is withheld from the test geography.
    """
    rules, improvements = {'uniform': ('mapped', 0.)}, {'uniform': 0.}
    for method in METHODS:
        variance = sum(d.moments[method][0] for d in groups if d.moments)
        agreement = sum(d.moments[method][1] for d in groups if d.moments)
        slope = max(0., agreement / variance) if variance > 1e-12 else 0.
        rules[method] = method, slope
        improvements[method] = 2 * slope * agreement - slope ** 2 * variance
    rules['selected'] = rules[max(improvements, key=improvements.get)]
    rules['mapped-half'] = 'mapped', .5
    rules['mapped-full'] = 'mapped', 1.
    rules['three-half'] = 'three', .5
    rules['three-full'] = 'three', 1.
    rules['three-10km'] = 'three-10km', rules['three'][1]
    return rules


def held_out_rules(districts, split):
    """Fit on other state districts, or leave out an entire federal area at once.

    The latter is a check against neighbouring seats sharing some of the same
    federal venues or distinctive local conditions. A state district belongs
    to the federal division supplying the most votes in its ordinary booth map.
    """
    def label(d):
        return d.seat if split == 'district' else d.area

    fits = {}
    for held_out in sorted({label(d) for d in districts.values()}):
        for mode in ('ordinary', 'early'):
            training = [d for d in districts.values() if d.mode == mode and label(d) != held_out]
            fits[held_out, mode] = training_rules(training)
    return {k: fits[label(d), d.mode] for k, d in districts.items()}


def district_scores(districts, fits, split):
    """Measure booth error with the *same actual group share* given to each rule.

    Absolute and squared errors weight booths by final formal votes within a
    group. A district with one positive booth cannot test within-seat variation.
    The election result supplies the common level solely for this controlled
    comparison. No outcome here determines a held-out geographic coefficient.
    """
    scores = []
    for identity, d in districts.items():
        if not d.moments:
            continue
        weights = d.formal / d.formal.sum()
        actual = np.divide(d.on, d.formal, out=np.zeros(len(d.on)), where=d.formal > 0)
        for rule, (method, strength) in fits[identity].items():
            level = d.on.sum() / d.formal.sum()
            pred, _ = anchored(strength * d.x[method], weights, level)
            if abs(weights @ pred - level) > 1e-9:
                raise ValueError('District share was not preserved')
            error = 100 * (pred - actual)
            scores.append(dict(split=split, seat=d.seat, area=d.area, mode=d.mode, rule=rule, method=method,
                               strength=strength, prior_candidate=d.prior_candidate,
                               booths=np.count_nonzero(d.formal), formal=d.formal.sum(),
                               mae=weights @ abs(error), mse=weights @ error ** 2))
    return scores


def paired_interval(gain, areas):
    """Resample federal areas to describe uncertainty in average district gain.

    Keep neighbouring districts together. An area drawn twice contributes all
    its districts twice, retaining the equal-district scoring rule. This
    interval describes geographic variation within a single state election.
    """
    blocks = defaultdict(list)
    for difference, area in zip(gain, areas):
        blocks[area].append(difference)
    totals = np.array([sum(blocks[a]) for a in sorted(blocks)])
    sizes = np.array([len(blocks[a]) for a in sorted(blocks)])
    rng = np.random.default_rng(20260321)
    draws = rng.integers(len(blocks), size=(4000, len(blocks)))
    means = totals[draws].sum(axis=1) / sizes[draws].sum(axis=1)
    return np.quantile(means, [.025, .975])


def score_summary(scores):
    """Give each eligible district one vote in the reported accuracy measures."""
    base = {(r['split'], r['mode'], r['seat']): r['mae'] for r in scores if r['rule'] == 'uniform'}
    groups = defaultdict(list)
    for r in scores:
        groups[r['split'], r['mode'], r['rule']].append(r)
    out = []
    for (split, mode, rule), rows in groups.items():
        gain = np.array([base[split, mode, r['seat']] - r['mae'] for r in rows])
        lo, hi = paired_interval(gain, [r['area'] for r in rows])
        out.append(dict(split=split, mode=mode, rule=rule, districts=len(rows),
                        booths=sum(r['booths'] for r in rows), formal=sum(r['formal'] for r in rows),
                        mae_pp=round(np.mean([r['mae'] for r in rows]), 4),
                        rmse_pp=round(np.sqrt(np.mean([r['mse'] for r in rows])), 4),
                        gain_pp=round(gain.mean(), 4), gain_low=round(lo, 4), gain_high=round(hi, 4),
                        districts_improved=int((gain > 1e-9).sum())))
    return out


def shares_from_returns(offsets, reported, prior_sizes, observed):
    """Set the district level from this update, including any remote reports.

    A booth without a geographic estimate represents the district average.
    Its reported ballots then have the same influence as a representative mix
    of the located booths, using sizes known before the election. Conversely,
    any unreported remote booth receives that estimated district average.
    """
    known = np.isfinite(offsets)
    if not known.any() or prior_sizes[known].sum() == 0:
        return np.full(len(reported), observed)
    if known.all():
        return anchored(offsets, reported, observed)[0]
    effective = np.where(known, reported, 0.).astype(float)
    effective[known] += reported[~known].sum() * prior_sizes[known] / prior_sizes[known].sum()
    shares, _ = anchored(offsets, effective, observed)
    shares[~known] = np.average(shares[known], weights=prior_sizes[known])
    return shares


def replay_scores(districts, fits, returns):
    """Infer each district's level from published votes, then predict other sites.

    The uniform forecast carries the reported share straight across. The
    geographic forecast finds the level consistent with *those reporting
    booths* and their actual counts, then applies their fitted relationship
    to the remaining sites. Only the score uses the final unreported votes.
    Pre-election relative sizes combine the predictions. Final sizes give a
    second diagnostic that isolates geographic skill from turnout errors.
    """
    groups = defaultdict(list)
    for r in returns:
        groups[r['seat'], r['mode'], int(r['stage'])].append(r)
    result = []
    for (seat, mode, stage), live in sorted(groups.items()):
        d = districts[seat, mode]
        seen = {r['booth']: (int(r['reported_on']), int(r['reported_formal'])) for r in live}
        if len(seen) != len(live) or not seen.keys() <= set(d.names):
            raise ValueError(f'Ambiguous or unrecognised first returns at {seat} / {mode}')
        votes = np.array([seen.get(n, (0, 0)) for n in d.names])
        reported = votes[:, 1] > 0
        remaining = ~reported
        final = d.formal[remaining].sum()
        if final <= 0:
            continue
        observed = votes[:, 0].sum() / votes[:, 1].sum()
        actual = d.on[remaining].sum() / final
        for rule, (method, strength) in fits[seat, mode].items():
            shares = shares_from_returns(d.x[method] * strength, votes[:, 1], d.sizes, observed)
            for weight, size in [('prior', d.sizes), ('final', d.formal)]:
                predicted = np.average(shares[remaining], weights=size[remaining])
                result.append(dict(seat=seat, area=d.area, mode=mode, stage=stage, capture=live[0]['capture'],
                                   source_time=live[0]['source_time'],
                                   reported_booths=int(reported.sum()), published_formal=int(votes[:, 1].sum()),
                                   remaining_formal=int(final), rule=rule, weights=weight, method=method,
                                   observed_pp=100 * observed, actual_remaining_pp=100 * actual,
                                   predicted_remaining_pp=100 * predicted, error_pp=100 * (predicted - actual)))
    return result


def replay_summary(scores):
    groups = defaultdict(list)
    base = {}
    for r in scores:
        groups[r['mode'], r['stage'], r['weights'], r['rule']].append(r)
        if r['rule'] == 'uniform':
            base[r['seat'], r['mode'], r['stage'], r['weights']] = abs(r['error_pp'])
    out = []
    for (mode, stage, weights, rule), rows in sorted(groups.items()):
        errors = np.array([abs(r['error_pp']) for r in rows])
        gains = [base[r['seat'], mode, stage, weights] - e for r, e in zip(rows, errors)]
        lo, hi = paired_interval(gains, [r['area'] for r in rows])
        out.append(dict(mode=mode, stage=stage, weights=weights, rule=rule, districts=len(rows),
                        mae_pp=round(errors.mean(), 4), gain_pp=round(np.mean(gains), 4),
                        gain_low=round(lo, 4), gain_high=round(hi, 4),
                        districts_improved=sum(g > 1e-9 for g in gains)))
    return out


def select_for_replay(training, returns):
    """Choose the method on actual first returns from *training areas only*.

    A good fit to individual booths may exaggerate the significance of the
    very first report. Compare the six fitted neighbourhoods and the uniform
    rule on the remaining vote, using pre-count sizes. Give each available
    milestone equal weight within its mode, then choose the lowest mean error.
    """
    rules = {mode: training_rules([d for d in training.values() if d.mode == mode])
             for mode in ('ordinary', 'early')}
    candidates = ('uniform', *METHODS)
    fits = {identity: {m: rules[d.mode][m] for m in candidates} for identity, d in training.items()}
    available = [r for r in returns if (r['seat'], r['mode']) in training]
    scores = replay_scores(training, fits, available)
    by_stage = defaultdict(list)
    for r in scores:
        if r['weights'] == 'prior':
            by_stage[r['mode'], r['rule'], r['stage']].append(abs(r['error_pp']))
    losses = defaultdict(list)
    for (mode, rule, _), errors in by_stage.items():
        losses[mode, rule].append(np.mean(errors))
    selected = {}
    for mode in rules:
        eligible = [m for m in candidates if (mode, m) in losses]
        choice = min(eligible, key=lambda m: np.mean(losses[mode, m])) if eligible else 'uniform'
        selected[mode] = rules[mode][choice]
    return selected


def replay_held_out(districts, returns, fits):
    """Withhold a whole federal area from both slope fitting and choice of geography."""
    result = {identity: dict(rules) for identity, rules in fits.items()}
    for area in {d.area for d in districts.values()}:
        training = {k: d for k, d in districts.items() if d.area != area}
        selection = select_for_replay(training, returns)
        for k, d in districts.items():
            if d.area == area:
                result[k]['replay-selected'] = selection[d.mode]
    return result


def illustrate_victoria(rows, strength, target, output):
    """Distribute an illustrative district share over the actual 2026 roster.

    Use the three nearest 2025 ordinary sites for an ordinary venue, with a
    neutral expectation when even the nearest is over 10 km away. The SA
    early-centre comparisons give too little support for a separate gradient.
    A common share within each district and mode makes the examples comparable.
    District forecasts can supply their own ordinary and early levels later.
    """
    groups = defaultdict(list)
    for r in rows:
        groups[r['seat'], r['mode']].append(r)
    examples, summaries = [], []
    for (seat, mode), booths in sorted(groups.items()):
        standing = {r['standing'] == 'True' for r in booths}
        if len(standing) != 1:
            raise ValueError(f'Inconsistent candidate roster for {seat}')
        level = target if True in standing else 0.
        sizes = np.array([float(r['prior_size']) for r in booths])
        usable = [r['nearest_km'] != '' and float(r['nearest_km']) <= LOCAL_LIMIT_KM for r in booths]
        if mode == 'ordinary':
            offsets = np.array([strength * federal_log_odds(r['three']) if ok else np.nan
                                for r, ok in zip(booths, usable)])
        else:
            offsets = np.zeros(len(booths))
        shares, _ = anchored(offsets, sizes, level)
        check = np.average(shares, weights=sizes)
        if abs(check - level) > 1e-9:
            raise ValueError(f'District expectation changed for {seat} / {mode}')
        for r, ok, share in zip(booths, usable, shares):
            reason = 'three federal sites' if mode == 'ordinary' and ok else 'district share'
            examples.append(dict(seat=seat, mode=mode, booth_id=r['booth_id'], booth=r['booth'],
                                 role=r['role'], vec_estimated_votes=r['prior_size'], basis=reason,
                                 nearest_km=r['nearest_km'], third_km=r['third_km'], three_sites=r['three_sites'],
                                 federal_on_formal=r['three'], target_pct=round(100 * level, 4),
                                 illustrated_pct=round(100 * share, 4), change_pp=round(100 * (share - level), 4)))
        summaries.append(dict(seat=seat, mode=mode, prior_candidate=booths[0]['prior_candidate'],
                              venues=len(booths), estimated_votes=int(sizes.sum()),
                              geographic_venues=sum(usable) if mode == 'ordinary' else 0,
                              target_pct=round(100 * level, 4), weighted_pct=round(100 * check, 6),
                              minimum_pct=round(100 * shares.min(), 3), maximum_pct=round(100 * shares.max(), 3)))
    save_rows(output / 'operator/vic-illustration.csv', examples)
    save_rows(output / 'vic-districts.csv', summaries)


def run(inputs, output, historical_only, target=.2):
    """Regenerate the SA comparisons and optionally evaluate the Victorian roster."""
    districts = district_data(read_rows(inputs / 'sa-booths.csv'))
    returns = read_first_returns(inputs)
    fits = replay_held_out(districts, returns, held_out_rules(districts, 'federal_area'))
    scores = district_scores(districts, fits, 'federal_area')
    scores += district_scores(districts, held_out_rules(districts, 'district'), 'district')
    replay = replay_scores(districts, fits, returns)
    save_rows(output / 'scores.csv', score_summary(scores))
    # Most Victorian districts have no same-district One Nation booth history.
    # The SA subset tests how the federal rule performed in that situation.
    subgroups = []
    for prior in (False, True):
        sample = [r for r in scores if r['prior_candidate'] == prior and r['mode'] == 'ordinary'
                  and r['split'] == 'federal_area' and r['rule'] in ('uniform', 'three', 'nearest', 'three-10km')]
        subgroups.extend(dict(prior_candidate=prior, **r) for r in score_summary(sample))
    save_rows(output / 'prior-candidate.csv', subgroups)
    save_rows(output / 'election-night.csv', replay_summary(replay))
    # Per-district figures let a reader see where the comparison failed as
    # well as where it helped. Retain one geography holdout and pre-count volumes.
    detail = [dict(seat=r['seat'], mode=r['mode'], rule=r['rule'], method=r['method'],
                   strength=round(r['strength'], 5), mae_pp=round(r['mae'], 4))
              for r in scores if r['split'] == 'federal_area']
    save_rows(output / 'district-errors.csv', detail)
    milestones = [{k: round(v, 4) if isinstance(v, float) else v for k, v in r.items()}
                  for r in replay if r['weights'] == 'prior'
                  and r['rule'] in ('uniform', 'three', 'nearest', 'three-10km', 'replay-selected')]
    save_rows(output / 'operator/count-history/replay-districts.csv', milestones)
    # The reporting milestone, prediction and eventual outstanding share suffice
    # to compare errors. The timestamp and ballot account belong to the local replay.
    columns = ('seat', 'area', 'mode', 'stage', 'rule', 'weights', 'method',
               'actual_remaining_pp', 'predicted_remaining_pp', 'error_pp')
    save_rows(output / 'replay-comparisons.csv', [{k: r[k] for k in columns} for r in milestones])
    trained = {mode: training_rules([d for d in districts.values() if d.mode == mode])
               for mode in ('ordinary', 'early')}
    for mode, chosen in select_for_replay(districts, returns).items():
        trained[mode]['replay-selected'] = chosen
    save_rows(output / 'strengths.csv',
              [dict(mode=mode, rule=rule, geography=method, log_odds_strength=round(slope, 6))
               for mode, rules in trained.items() for rule, (method, slope) in rules.items()])
    if not historical_only:
        illustrate_victoria(read_rows(inputs / 'operator/vic-booths.csv'),
                            trained['ordinary']['three'][1], target, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--inputs', type=Path, default=HERE)
    parser.add_argument('--out', type=Path, default=HERE)
    parser.add_argument('--historical-only', action='store_true')
    parser.add_argument('--target-pct', type=float, default=20., help='Illustrative Victorian group share in percent')
    args = parser.parse_args()
    if not 0 <= args.target_pct <= 100:
        parser.error('The illustrative target must be between 0 and 100 percent.')
    run(args.inputs, args.out, args.historical_only, args.target_pct / 100)

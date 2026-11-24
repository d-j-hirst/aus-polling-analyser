"""Test whether a returning independent's preference flow changes by a similar
amount across booths. Read frozen replay exports; write only small trial results.
The final counts are opened separately for scoring and never enter the fit.
"""
import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'analysis'))
from lib.live_analysis_archive import analysis_files, load_json, main_path_for_analysis
from scripts.exploratory.score_booth_fp_update import name_key, official_results


# Each trial uses one model version throughout an election. The last archive
# named here supplies independent observations for scoring missing booth TCPs.
RUNS = {
    '2025fed': ('2025fed-testing3', '2022fed', '2025fed-preferences11'),
    '2022vic': ('2022vic-testing6', '2018vic', '2022vic-checkpoint2-261014'),
    '2026sa': ('2026sa-testing2', '2022sa', '2026sa-preferences1'),
    '2022fed': ('2022fed-turnout8', '2019fed', '2022fed-turnout8'),
    '2019fed': ('2019fed-turnout7', '2016fed', '2019fed-turnout7'),
    '2016fed': ('2016fed-turnout5', '2013fed', '2016fed-turnout5'),
    '2013fed': ('2013fed-turnout4', '2010fed', '2013fed-turnout4'),
    '2024qld': ('2024qld-turnout15', '2020qld', '2024qld-turnout15'),
    '2025wa': ('2025wa-turnout2', '2021wa', '2025wa-turnout2'),
}
PARTIES = {'Independent': 'IND', 'Liberal': 'LIB', 'Labor': 'ALP', 'National': 'NAT',
           'Greens': 'GRN', 'One Nation': 'ON'}


def vote_map(node, field):
    return {r['party_index']: r['value'] for r in node[field]}


def results(election):
    """Read named finalists. Verify historical eligibility independently of booth IDs."""
    if election == '2026sa':
        return {k: v['tcp'] for k, v in official_results(election).items()}
    records = defaultdict(list)
    with (ROOT / 'analysis/elections' / f'results_{election}.csv').open(encoding='utf-8-sig') as f:
        section = seat = None
        for row in csv.reader(f):
            if not row:
                continue
            if row[0] == 'Seat':
                seat, section = row[1], None
            elif row[0] in ('tcp', 'fp'):
                section = row[0]
            elif section == 'tcp' and len(row) >= 4:
                records[seat].append((name_key(row[0]), PARTIES.get(row[1], row[1]), int(row[2])))
    return records


def named_pair(records, name, opponent):
    key = name_key(name)
    # The 2019 Cowper feed uses Robert; both elections' results use Rob.
    if key == 'robertoakeshott':
        key = 'roboakeshott'
    return (len(records) == 2 and any(person == key and party == 'IND'
            for person, party, _ in records) and any(party == opponent for _, party, _ in records))


def transfers(fp, tcp, independent, opponent, allow_near=False):
    """Extra TCP votes for the independent, out of first votes for neither finalist.

    Ordinarily use identical totals. The near-match sensitivity follows the
    live compulsory-vote reliability rule in LivePartialCount.h. Express the
    TCP on the same ballot total as the FP, then reduce its contribution toward
    zero as the discrepancy approaches 2% of FP or the size of the minor pool.
    A missing candidate entry is never interpreted as a measured zero.
    """
    pair = {independent, opponent}
    total, tcp_total = sum(fp.values()), sum(tcp.values())
    if set(tcp) != pair or not pair <= fp.keys() or not tcp_total:
        return None
    pool = total - fp[independent] - fp[opponent]
    if pool <= 0:
        return None
    weight = 1.
    if total != tcp_total:
        if not allow_near:
            return None
        fraction = 1 - abs(total-tcp_total) / min(.02*total, pool)
        if fraction <= 0:
            return None
        weight = fraction**2 * (3 - 2*fraction)
    gained = tcp[independent] * total / tcp_total - fp[independent]
    return (weight*gained, weight*pool) if 0 <= gained <= pool else None


def logodds(p):
    p = np.clip(p, 1e-10, 1 - 1e-10)
    return np.log(p / (1 - p))


def logistic(x):
    return 1 / (1 + np.exp(-np.clip(x, -40, 40)))


def preference_fit(history, observed):
    """Fit one change in preference odds to the booths counted by this capture.

    Allow a booth with sparse old returns to draw on 50 historical preference
    ballots at the district rate. Fit using *current minor ballots* as the
    weights. Thus large booths with few preferences don't dominate the change.
    All fitted rates stay between zero and one. Before extrapolating, temper
    the change for sparse current evidence and incomplete historical coverage.
    Both the 50 old and 100 current ballots are trial assumptions.
    """
    keys = sorted(history.keys() & observed.keys())
    if not keys:
        return
    old_mean = sum(x for x, _ in history.values()) / sum(n for _, n in history.values())
    old = {k: (x + 50 * old_mean) / (n + 50) for k, (x, n) in history.items()}
    counted = sum(observed[k][0] for k in keys)
    weights = np.array([observed[k][1] for k in keys])
    historical_odds = logodds(np.array([old[k] for k in keys]))
    lo, hi = -32., 32.
    for _ in range(44):
        mid = (lo + hi) / 2
        if weights @ logistic(historical_odds + mid) < counted:
            lo = mid
        else:
            hi = mid
    share = weights.sum() / (100 + sum(n for _, n in observed.values()))
    fitted = {k: float(logistic(logodds(p) + (lo + hi) / 2)) for k, p in old.items()}
    return fitted, share, int(weights.sum()), float(weights @ np.array([old[k] for k in keys]) / weights.sum())


def case_at_capture(booths, seat, ind_slot, opponent, allow_near, previous_id):
    """Replace the pooled transfer estimate only in booths with no TCP yet.

    Keep each booth's FP, turnout, alternative historical TCP-swing estimate
    and fallback weight from LiveV2::recomposeBoothTcpVotes. Measured TCPs and
    unmatched locations retain their full published estimates, including any
    later batches still outstanding at an already counted location.
    """
    ind = seat['live_independent_party_index']
    history, current = {}, {}
    for key, b in booths.items():
        node = b['node']
        for suffix, target in [('previous', history), ('current', current)]:
            result = transfers(vote_map(node, 'fp_votes_' + suffix), vote_map(node, 'tcp_votes_' + suffix),
                               previous_id if suffix == 'previous' else ind, opponent, allow_near)
            if result:
                target[key] = result
    fit = preference_fit(history, current)
    conf = seat['tcp_focus_party_confidence']
    pooled = seat['tcp_focus_party_pref_flow']
    focus = seat['tcp_focus_party_index']
    if not fit or not conf or pooled is None or focus not in (ind, opponent):
        return
    rates, strength, ballots, expected = fit
    pooled = pooled / 100 if focus == ind else 1 - pooled / 100
    def supplies_swing(booth):
        node = booth['node']
        pair = {ind, opponent}
        return ((sum(vote_map(node, 'tcp_votes_current').values()) > 0 and
                 pair <= vote_map(node, 'tcp_swings').keys()) or
                (sum(vote_map(node, 'fp_votes_current').values()) > 0 and
                 pair <= vote_map(node, 'fp_swings').keys()))
    has_swing = any(supplies_swing(booth) for booth in booths.values())
    swing_weight = .01 if has_swing else .001
    if previous_id != ind:
        # The archived simulator treated the major party as the only matching
        # finalist. Preserve the weight on its shared-finalist TCP estimate.
        coverage = expected_turnout = 0.
        for booth in booths.values():
            node = booth['node']
            expected_turnout += sum(vote_map(node, 'fp_votes_projected').values())
            old_tcp, tcp = (vote_map(node, 'tcp_votes_' + s) for s in ('previous', 'current'))
            if opponent in old_tcp and sum(old_tcp.values()) and opponent in tcp and sum(tcp.values()):
                coverage += sum(vote_map(node, 'fp_votes_previous').values()) * node['relevance_modifier']
        swing_weight = .1 * math.sqrt(min(1., coverage/expected_turnout)) if expected_turnout else 0.
    changes = {}
    for key, rate in rates.items():
        b = booths[key]
        node = b['node']
        if sum(vote_map(node, 'tcp_votes_current').values()):
            continue
        tcp = vote_map(node, 'tcp_votes_projected')
        if set(tcp) != {ind_slot, opponent} or sum(tcp.values()) <= 0:
            continue
        fp = vote_map(node, 'fp_votes_current')
        use_current = b['vote_type'] == 'Ordinary' and {ind, opponent} <= fp.keys() and sum(fp.values()) > 0
        fp_ind = ind if use_current else ind_slot
        if not use_current:
            fp = vote_map(node, 'fp_votes_projected')
        if not {fp_ind, opponent} <= fp.keys() or sum(fp.values()) <= 0:
            continue
        turnout = sum(tcp.values())
        pool = turnout * (sum(fp.values()) - fp[fp_ind] - fp[opponent]) / sum(fp.values())
        flow_weight = math.sqrt(conf) * (.5 if use_current else .2)
        # Eligible targets have full historical TCP comparison. Its swing
        # method has weight .01 where the reporting comparisons supply swings.
        other_weight = 0. if previous_id != ind and sum(vote_map(node, 'fp_votes_current').values()) else swing_weight
        fraction = flow_weight / (flow_weight + other_weight + .0001)
        revised = pooled + strength * (rate - pooled)
        delta = pool * fraction * (revised - pooled)
        changes[key] = dict(delta=delta, old=tcp[ind_slot] / turnout, new=(tcp[ind_slot]+delta) / turnout,
                            pooled=pooled, revised=revised)
    tcp = vote_map(seat['node'], 'tcp_votes_projected')
    if not changes or set(tcp) != {ind_slot, opponent}:
        return
    total = sum(tcp.values())
    delta = sum(v['delta'] for v in changes.values()) / total
    return dict(changes=changes, delta=delta, booth_tcp=tcp[ind_slot]/total, pooled=pooled,
                matched=len(current.keys() & history.keys()), ballots=ballots, historical=expected,
                signature=tuple(sorted(current.items())))


def distribution(report, i, slot, opponent, seat, case):
    """Apply the change to the archived conditional distribution as a sensitivity.

    The live simulator blends the booth projection and prior on the log-odds
    scale. Approximate that blend at the archived district confidence, keeping
    pairing and spread fixed. The win frequency is anchored to the measured
    simulation frequency at 50%; interpolation between saved percentiles can
    only estimate the incremental crossing probability.
    """
    def same_pair(row):
        return {row['party_a']['party_index'], row['party_b']['party_index']} == {slot, opponent}
    band = next((r for r in report['seat_tcp_probability_band'][i] if same_pair(r)), None)
    if not band:
        return {}
    levels = np.array(report['probability_bands'])
    values = np.array(band['values'])
    if band['party_a']['party_index'] != slot:
        values, levels = 100 - values[::-1], 100 - levels[::-1]
    row = next(r for r in report['seat_tcp_win_percent'][i] if same_pair(r))
    win = row['value'] if row['party_a']['party_index'] == slot else 100 - row['value']
    confidence = max(seat['node']['tcp_confidence'], seat['node']['tcp_completion'] ** 2)
    weight = (logistic(14 * confidence - .5) - logistic(-.5)) / (logistic(13.5) - logistic(-.5))
    base = case['booth_tcp']
    median = float(values[list(levels).index(50)])
    def shift(strength):
        return weight * (logodds(base + strength * case['delta']) - logodds(base))
    keep = ((values < 50) & (levels < 100 - win)) | ((values > 50) & (levels > 100 - win))
    anchors = sorted(list(zip(values[keep], levels[keep])) + [(50, 100-win)])
    def revised(strength):
        movement = shift(strength)
        cutoff = 100 * logistic(-movement)
        return 100*logistic(logodds(median/100)+movement), 100-np.interp(cutoff, *zip(*anchors))
    half_median, half_win = revised(.5)
    revised_median, revised_win = revised(1.)
    return dict(median=median, half_median=half_median, revised_median=revised_median,
                win=win, half_win=half_win, revised_win=revised_win)


def score_targets(targets, final, ind, opponent):
    """Compare later observed transfer rates and TCPs at still uncounted locations."""
    n = tcp_places = votes = pref = 0
    errors = np.zeros((3, 3))
    for k, change in targets.items():
        if k not in final:
            continue
        fp, tcp = final[k]
        size = sum(tcp.values())
        if size <= 0 or set(tcp) != {ind, opponent}:
            continue
        observed = tcp[ind] / size
        tcp_places += 1
        votes += size
        shares = np.linspace(change['old'], change['new'], 3)
        errors[:,1] += abs(shares - observed)*size
        # Keep a stricter scoring target than the optional near-match inputs.
        # Each held-out transfer rate must be measurable on exactly equal totals.
        actual = transfers(fp, tcp, ind, opponent)
        if not actual:
            continue
        gained, count = actual
        flows = np.linspace(change['pooled'], change['revised'], 3)
        errors[:,0] += abs(flows - gained/count)*count
        errors[:,2] += (flows - gained/count)*count
        n += 1
        pref += count
    scores = dict(tcp_places=tcp_places, tcp_mae=list(100*errors[:,1]/votes)) if tcp_places else {}
    if n:
        scores.update(places=n, flow_mae=list(100*errors[:,0]/pref), flow_bias=list(100*errors[:,2]/pref))
    return scores


def grouped(booths):
    result = defaultdict(dict)
    for b in booths:
        key = (b['name'], b['vote_type'], b['booth_type'])
        seat = result[b['seat_name']]
        if key in seat:
            raise ValueError(f"Duplicate booth identity {b['seat_name']} {key}")
        seat[key] = b
    return result


def trial(election, allow_near):
    run, previous, final_run = RUNS[election]
    old_result, final_result = results(previous), results(election)
    final_archive = analysis_files(ROOT/'live_runs'/final_run)[-1]
    # Project slots can change between replay versions (e.g. Victorian NAT).
    # Stable ballot IDs identify independents; align registered party slots
    # separately by abbreviation when scoring against a different version.
    final_parties = json.loads(main_path_for_analysis(final_archive).read_bytes())['parties']
    final_ids = {p['abbreviation']: p['party_index'] for p in final_parties if p['party_index'] >= 0}
    final_groups = grouped(load_json(final_archive)['booths'])
    final_places = {seat: {k: (vote_map(b['node'], 'fp_votes_current'), vote_map(b['node'], 'tcp_votes_current'))
                           for k,b in places.items()} for seat, places in final_groups.items()}
    del final_groups
    rows, seen = [], {}
    for path in analysis_files(ROOT/'live_runs'/run):
        archive = load_json(path)
        groups = grouped(archive['booths'])
        raw = json.loads(main_path_for_analysis(path).read_bytes())
        report = raw['simulation_report']
        slot = next(p['party_index'] for p in raw['parties']
                    if p['party_index'] >= 0 and p['abbreviation'] == 'IND')
        for seat in archive['seats']:
            name = seat['name']
            tcp = seat['node']['tcp_votes_current']
            ind = seat['live_independent_party_index']
            if (ind != seat['independent_party_index'] or len(tcp) != 2 or
                    not any(r['party_index']==ind and r['value']>0 for r in tcp)):
                continue
            other = next(r for r in tcp if r['party_index'] != ind)
            opponent = other['party_index']
            label = other['abbreviation']
            label = 'LIB' if label == 'LNP' else label
            i = report['seat_name'].index(name)
            person = next((r['value'] for r in report['seat_candidate_names'][i] if r['party_index']==slot), '')
            previous_name = 'Denison' if election == '2019fed' and name == 'Clark' else name
            if not named_pair(old_result.get(previous_name, []), person, label):
                continue
            # Denison was renamed Clark. Its 2016 replay identifies Wilkie
            # as 120881 and its 2019 replay as 133553. Match this verified
            # identity only; another independent's old votes cannot qualify.
            previous_id = 120881 if (election, name, ind) == ('2019fed', 'Clark', 133553) else ind
            case = case_at_capture(groups[name], seat, slot, opponent, allow_near, previous_id)
            if not case:
                continue
            key = name, opponent
            sig = case.pop('signature')
            row = dict(election=election, seat=name, opponent=label, candidate=person, time=path.name.split('_')[1],
                       new_evidence=seen.get(key)!=sig, **distribution(report, i, slot, opponent, seat, case))
            seen[key] = sig
            targets = case.pop('changes')
            final_opponent = final_ids.get(other['abbreviation'])
            row.update(case, targets=len(targets), **score_targets(targets, final_places.get(name, {}), ind, final_opponent))
            outcome = final_result.get(name, [])
            if named_pair(outcome, person, label):
                row['actual'] = 100*next(n for _,p,n in outcome if p=='IND')/sum(n for _,_,n in outcome)
            rows.append(row)
    return rows


def district_scores(rows):
    """Give each distinct local count equal weight within its seat and election."""
    groups = defaultdict(list)
    for row in rows:
        if row['new_evidence']:
            groups[(row['election'], row['seat'])].append(row)
    scores = {}
    for key, captures in sorted(groups.items()):
        metrics = {}
        for field in ('flow_mae', 'tcp_mae'):
            values = [r[field] for r in captures if field in r]
            if values:
                metrics[field] = np.mean(values, axis=0)
        values = [[abs(r[v]-r['actual']) for v in ('median', 'half_median', 'revised_median')]
                  for r in captures if 'actual' in r and 'median' in r]
        if values:
            metrics['district_mae'] = np.mean(values, axis=0)
        scores[key] = metrics
    return scores


def print_summary(rows):
    scores = district_scores(rows)
    for field in ('flow_mae', 'tcp_mae', 'district_mae'):
        values = np.array([metrics[field] for metrics in scores.values() if field in metrics])
        if len(values):
            print(field, 'districts', len(values), 'improved', int((values[:,0] > values[:,-1]).sum()),
                  'mean errors (pp)', np.mean(values, axis=0).round(3))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elections', nargs='+', choices=RUNS)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--allow-near', action='store_true', help='Allow closely matched FP/TCP totals when fitting.')
    args = parser.parse_args()
    observations = [row for election in args.elections for row in trial(election, args.allow_near)]
    args.output.write_text(json.dumps(observations, indent=1), encoding='utf-8')
    print_summary(observations)

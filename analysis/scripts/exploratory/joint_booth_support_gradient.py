"""Test historical booth-strength corrections on the whole FP ballot in saved replays.

Run from the repository root with the analysis Python. Only ballots still to
come can move. Future booth counts and final district results score the estimates.
TCP and win figures are sensitivities to FP changes using archived distributions.
"""
import argparse
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd

from booth_support_gradient import PARTY, ROOT, RUNS, booth_fp, load_booths, prepare, read, replay_packages, slope_fit
from score_booth_fp_update import actual_vector, group_votes, official_results
from score_booth_transfer_trial import conditional_projection


def joint_shares(shares, tilts):
    """Redistribute a booth's outstanding ballots among the candidates together.

    Each supported slope specifies a change in the odds for one candidate
    against all rivals. A single such signal can be applied exactly, sharing
    the offset among the rivals in proportion to their starting shares.
    If A rises while B falls, both slopes partly describe the same difference.
    For their mutual comparison, take the stronger of the two movements. With
    A and B as the only candidates this counts equal, opposite evidence once.
    A third party with no signal still provides a separate comparison for each.

    With several rivals these pairwise comparisons may disagree. For each
    candidate take the average implied log-odds movement against all rivals,
    weighting by their shares in this booth (including zero movement against
    oneself). Apply the resulting relative multipliers together. This trial
    rule preserves the odds between two candidates with no signals and never
    assigns votes to a candidate with zero starting share.
    """
    if not np.any(tilts):
        return shares.copy()
    left, right = tilts[:, :, None], tilts[:, None, :]
    overlap = np.where(left*right < 0, np.sign(left)*np.minimum(abs(left), abs(right)), 0.)
    adjustment = ((left-right-overlap)*shares[:, None, :]).sum(axis=2)
    adjusted = shares*np.exp(adjustment-adjustment.max(axis=1, keepdims=True))
    mass = adjusted.sum(axis=1, keepdims=True)
    return np.divide(adjusted, mass, out=np.zeros_like(adjusted), where=mass>0)


def ballot_projection(places, fitted, keys, candidates, parties):
    """Keep the observed FP ledger; learn geography only from counted ordinary booths.

    The last column pools the remaining ballot candidates. None have a fitted
    slope, so allocating that column back in its original proportions gives
    the same result as carrying every small candidate separately.
    """
    booths = [places[k] for k in keys]
    counted = np.array([group_votes(b['fp_votes_current'], parties, b.get('count_to_projection_ids', {}))
                        for b in booths])
    expected = np.array([group_votes(fitted[k], parties, {}) for k in keys])
    future = expected-counted
    if np.min(future) < -.01:
        raise ValueError('The booth benchmark projected fewer votes than already counted.')
    future = np.maximum(future, 0.)
    remaining = future.sum(axis=1)
    sizes = counted.sum(axis=1)
    ordinary = np.array([b['booth_type']=='Normal' and b['vote_type']=='Ordinary' for b in booths])
    control = np.divide(future, remaining[:, None], out=np.zeros_like(future), where=remaining[:, None]>0)
    tilts = np.zeros_like(control)
    slopes = {}
    for p, (_, _, previous, opening) in candidates.items():
        j = parties.index(p)
        fit = slope_fit(previous, opening, counted[:, j], sizes, ordinary)
        if fit is not None:
            _, _, slope, distance = fit
            slopes[p] = slope
            tilts[:, j] = slope*distance
    joint = joint_shares(control, tilts)
    if not np.allclose(joint.sum(axis=1), remaining>0, atol=1e-10):
        raise ValueError('The proposed outstanding ballot does not add up.')
    return booths, counted, ordinary, remaining, control, joint, slopes


def score_tcp(district, wins, finalists, parties, labels, booths, counted, remaining, control_fp, changes):
    """Keep the dominant forecast pair, matched TCP ballots and transfer rates fixed.

    Test the published conditional median and interval with the joint FP
    change. The comparison uses the same finalist pair as the final count for
    scoring. Rare or unresolved pairings receive no TCP score. Where TCP runs
    ahead of FP, conservatively exclude the fraction of the proposed change
    that overlaps those already allocated ballots.
    """
    key, frequency = max(district['pairing_percent'].items(), key=lambda v:v[1], default=('',0))
    pair = tuple(key.split('/'))
    final_votes = {p:v for p, _, v in finalists}
    if (frequency < 65 or set(pair) != set(final_votes) or key not in wins
            or key not in district['tcp_percentiles'] or not set(pair)<=set(parties)):
        return {}
    uncovered = np.ones(len(booths))
    for i,b in enumerate(booths):
        tcp = b['tcp_votes_current']
        aliases = b.get('count_to_projection_ids', {})
        if remaining[i] and {aliases.get(p,p) for p,v in tcp.items() if v} <= set(pair):
            excess_tcp = max(0., sum(tcp.values())-counted[i].sum())
            uncovered[i] = max(0., 1-excess_tcp/remaining[i])
    total = remaining.sum()+counted.sum()
    delta = 100*(uncovered @ changes)/total
    bands = district['tcp_percentiles'][key]
    levels = sorted(map(float,bands))
    shares = [v for _,v in sorted((float(k),v) for k,v in bands.items())]
    estimate = conditional_projection(pair, shares, levels, wins[key], parties, labels, control_fp,
                                      delta, booths, counted, total)
    if estimate is None:
        return {}
    median, chance = estimate
    return dict(pair=key, first=labels[pair[0]], second=labels[pair[1]], pair_chance=frequency,
                control_tcp=bands['50'], joint_tcp=median, final_tcp=100*final_votes[pair[0]]/sum(final_votes.values()),
                control_conditional_win=wins[key], joint_conditional_win=chance)


def score_capture(seat, base, places, following, summary, outcome, seen):
    """Compare the published and joint FP vectors on exactly the same voters."""
    keys, geography, candidates = base
    district = summary['seats'][seat]
    if places.keys()!=geography.keys():
        return None
    # Keep every named party in the scoring, even without history of its own.
    # A Liberal can gain from an independent's loss without having contested
    # this district last time. Include the pool of small candidates in errors.
    parties = [p for p,c in district['candidates'].items() if int(p)>=0 and
               (c.get('party')!='IND' or c.get('name'))]
    if not candidates.keys()<=set(parties):
        return None
    n = sum(b['booth_type']=='Normal' and b['vote_type']=='Ordinary' and sum(b['fp_votes_current'].values())>0
            for b in places.values())
    size = sum(sum(b['fp_votes_current'].values()) for b in places.values())
    if n<4 or size >= sum(b['expected_remaining_fp'] for b in places.values()):
        return None
    fitted = booth_fp.district_expectations(places, district['fp_mean_percent'], geography)
    if not fitted:
        return None
    booths, counted, ordinary, remaining, control, joint, slopes = ballot_projection(
        places, fitted, keys, candidates, parties)
    if not slopes:
        return None
    labels = {p:PARTY.get(c['party'],c['party']) for p,c in district['candidates'].items()}
    total = counted.sum()+remaining.sum()
    control_fp = 100*(counted.sum(axis=0)+remaining@control)/total
    published = group_votes(district['fp_mean_percent'], parties, {})
    if not np.allclose(control_fp, published, atol=.02, rtol=0):
        raise ValueError(f'{seat}: the booth control does not reproduce the published district FP.')
    changes = remaining[:, None]*(joint-control)
    joint_fp = control_fp+100*changes.sum(axis=0)/total
    if not np.allclose(joint_fp.sum(),100) or np.any(joint_fp < 100*counted.sum(axis=0)/total-1e-8):
        raise ValueError('The district FP projection changed counted votes or failed to conserve turnout.')
    actual, finalists = actual_vector(outcome, parties, labels, district['candidates'])
    info = dict(seat=seat,time=summary['source_time'], n=n, stage='4-7' if n<8 else '8-15' if n<16 else '16+')
    # Half the sum of absolute errors is the minimum percentage of ballots
    # which must change candidate to recover the actual district composition.
    row = dict(info,active=sum(s!=0 for s in slopes.values()),control_error=abs(control_fp-actual).sum()/2,
               joint_error=abs(joint_fp-actual).sum()/2)
    row.update(score_tcp(district,summary['conditional_wins'].get(seat,{}),finalists,parties,labels,booths,
                         counted,remaining,control_fp,changes))
    candidate_rows = [dict(info,party=labels[p] if p else 'Others',slope=slopes.get(p,0.),
                           control=old,joint=new,actual=observed)
                      for p,old,new,observed in zip([*parties,None],control_fp,joint_fp,actual)]
    arrivals = []
    for i,k in enumerate(keys):
        if k in seen or remaining[i]<=0 or k not in following:
            continue
        new = following[k]
        observed = group_votes(new['fp_votes_current'], parties, new.get('count_to_projection_ids', {}))
        votes = observed.sum()
        if votes:
            actual_share = observed/votes
            errors = 100*np.abs(np.array([control[i],joint[i]])-actual_share)
            arrivals.append(dict(info,booth=k,ordinary=ordinary[i],votes=votes,party='Whole ballot',
                                 control=errors[0].sum()/2,joint=errors[1].sum()/2))
            for p,old,revised in zip([*parties,None], *errors):
                arrivals.append(dict(info,booth=k,ordinary=ordinary[i],votes=votes,party=labels[p] if p else 'Others',
                                     control=old,joint=revised))
    return row, candidate_rows, arrivals


def evaluate(run):
    previous, through = RUNS[run]
    election = run.split('-')[0]
    captures = replay_packages(run, through)
    current = load_booths(captures[0][0])
    bases = prepare(captures[0][1],current,read(ROOT/f'analysis/Booth Results/{previous}.json'))
    outcomes = official_results(election)
    records = defaultdict(list)
    seen, stages = set(), set()
    for step, (_,summary) in enumerate(captures):
        following = load_booths(captures[step+1][0]) if step+1<len(captures) else {}
        seen.update(k for places in current.values() for k,b in places.items() if sum(b['fp_votes_current'].values()))
        for seat, base in bases.items():
            if seat not in outcomes:
                continue
            result = score_capture(seat,base,current.get(seat,{}),following.get(seat,{}),summary,outcomes[seat],seen)
            if result is None:
                continue
            seat_row, candidates, arrivals = result
            stage = seat,seat_row['stage']
            for table,rows in [('districts',[seat_row]),('candidates',candidates),('arrivals',arrivals)]:
                records[table].extend(dict(r,election=election,sample=stage not in stages) for r in rows)
            stages.add(stage)
        current = following
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,help='Directory for optional local scoring tables')
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True,exist_ok=True)
    tables = [evaluate(run) for run in RUNS]
    for table in ('districts','candidates','arrivals'):
        frame = pd.DataFrame([row for records in tables for row in records[table]])
        if args.output:
            frame.to_csv(args.output/f'joint-{table}.csv',index=False)
        if table=='districts':
            sample = frame[frame['sample']]
            print('Final FP ballot error in pp, one observation per seat and counting stage')
            print(sample.groupby('election')[['control_error','joint_error']].agg(['size','mean']).round(3))
        if table=='arrivals':
            print('First-return ballot error in pp, weighted by counted votes')
            for group, rows in frame[frame.party=='Whole ballot'].groupby(['election','ordinary']):
                print(group,len(rows),np.average(rows[['control','joint']],axis=0,weights=rows.votes).round(3))


if __name__=='__main__':
    main()

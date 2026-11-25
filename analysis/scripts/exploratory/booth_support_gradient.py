"""Test whether past booth strength predicts which outstanding FP estimates need revising.

Run this file from the repository root. It reads saved replay packages, actual previous
booth results and official district outcomes. The optional output directory receives
local scoring tables. Candidates are tested one at a time against all other voters.
These sensitivities do not change the simulator, its finalist selection or win odds.
"""
import argparse
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.special import expit, logit

from score_booth_fp_update import ROOT, booth_fp, name_key, official_results, read


# Fixed windows make the trial reproducible as the operator advances the replay.
RUNS = {'2022vic-8-initial': ('2018vic', '2022-11-26T20:20:01'),
        '2026sa-changes2-261122': ('2022sa', '2026-03-21T23:59:59')}
PARTY = {'AUSTRALIAN LABOR PARTY': 'ALP', 'AUSTRALIAN LABOR PARTY - VICTORIAN BRANCH': 'ALP',
         'LIBERAL': 'LIB', 'THE NATIONALS': 'NAT',
         'AUSTRALIAN GREENS': 'GRN', 'INDEPENDENT': 'IND', 'ONP': 'ON', 'LNP': 'LIB'}
DECLARATION = {'Early': 'Early Votes', 'Absent': 'Absent Votes', 'Postal': 'Postal Votes',
               'Provisional': 'Provisional Votes', 'Marked as voted': 'Marked As Voted Votes'}


def matched_places(booths, district, party, person):
    """Return formal and candidate ballots at exactly matched places in a booth results file.

    Match the district as well as the booth. A returning independent must be the
    same named person. A missing candidate, renamed place or changed declaration
    category supplies no evidence. Genuine zero support with a positive formal
    total is an observation. SA's former combined declarations cannot stand in
    for its separate present-day categories.
    """
    ids = [i for i, c in district['candidates'].items()
           if PARTY.get(c['party'], c['party']) == party
           and (party != 'IND' or name_key(c['name']) == name_key(person))]
    previous = np.zeros((len(booths), 2))
    if len(ids) == 1:
        for i, b in enumerate(booths):
            place = b['name'] if b['vote_type'] == 'Ordinary' else DECLARATION.get(b['vote_type'])
            fp = district['booths'].get(place, {}).get('fp', {})
            if ids[0] in fp:
                previous[i] = fp[ids[0]], sum(fp.values())
    return previous


def slope_fit(previous, opening, votes, sizes, ordinary):
    """Estimate a change in the geographic pattern, beyond the opening forecast.

    The horizontal scale is the historical fraction voting for this candidate.
    The vertical scale is the observed change from the opening estimate in log
    odds, so applying a line keeps shares between zero and one. The intercept
    absorbs the ordinary seat swing; only the slope is used to revise forecasts.
    Half a vote per side keeps small measured counts of zero usable.
    """
    past, past_size = previous.T
    x = (past + .5) / (past_size + 1)
    use = ordinary & (sizes > 0) & (past_size > 0) & (opening > 0) & (opening < 1)
    n = use.sum()
    if n < 4:
        return None
    xx = x[use]
    q = (votes[use] + .5) / (sizes[use] + 1)
    y = logit(q) - logit(opening[use])
    # A large booth is one place, however many ballots it counts. Allow .30
    # log odds of between-place noise (about 7.4 pp at 50%), as well as sampling
    # noise in both elections. All tuning values here are trial assumptions.
    noise = .30**2 + 1/(sizes[use]*q*(1-q)) + 1/(past_size[use]*xx*(1-xx))
    w = 1/noise
    center = np.average(xx, weights=w)
    dx = xx-center
    dy = y-np.average(y, weights=w)
    spread = w @ dx**2
    if spread < 1e-8:
        return None
    cov = w @ (dx*dy)
    slope = cov/spread
    # Drop each place in turn. If removing one changes the estimated slope a
    # great deal, allow more uncertainty than sampling noise alone implies.
    # An unidentified relationship after one removal gets no guarded correction.
    deleted_weight = w*w.sum()/(w.sum()-w)
    deleted_spread = spread-deleted_weight*dx**2
    guarded = 0.
    if min(deleted_spread) > 1e-8:
        deleted = (cov-deleted_weight*dx*dy)/deleted_spread
        se = max(1/np.sqrt(spread), np.sqrt((n-1)*np.var(deleted)))
        # Remove a 90% normal uncertainty allowance from the apparent slope
        # and pull the rest toward zero with a prior SD of two log odds per
        # unit of historical share. This is a conservative evidence screen,
        # not a calibrated confidence level for an individual seat.
        guarded = np.sign(slope)*max(0., abs(slope)-1.645*se)/(1+(se/2)**2)
    # Project only within the range already measured in this seat. Reported
    # votes stay fixed; an unmatched place retains the control forecast.
    distance = np.where(past_size > 0, np.clip(x, xx.min(), xx.max())-center, 0.)
    return n, slope, guarded, distance


def project(candidate, booths, previous, opening, current, sizes, remaining, ordinary):
    """Tilt the existing forecast for unreported ballots, leaving the counted floor intact.

    The control is the published seat FP mean distributed across booths using
    the same benchmark as the commentary. Fit on election-day booths only.
    Compare transferring their slope to matched declaration services at full
    strength with applying it only to outstanding ordinary booths.
    """
    votes = np.array([sum(v for p,v in b['fp_votes_current'].items()
                          if b.get('count_to_projection_ids', {}).get(p,p) == candidate)
                      for b in booths])
    fit = slope_fit(previous, opening, votes, sizes, ordinary)
    if fit is None:
        return None
    n, raw, guarded, distance = fit
    future = np.clip(np.divide(current-votes, remaining,
                               out=np.zeros_like(remaining), where=remaining>0), 0, 1)
    # Exact zero and one retain their support under an odds multiplier.
    logs = logit(future)
    tilts = np.array([distance*raw, distance*guarded, distance*guarded*ordinary])
    estimates = np.vstack([future, expit(logs+tilts)])
    seat = 100*(votes.sum()+estimates@remaining)/(sizes.sum()+remaining.sum())
    return n, raw, guarded, estimates, seat


def load_booths(path):
    districts = defaultdict(dict)
    for k,b in read(path/'evidence.json.gz')['booths'].items():
        districts[b['seat_name']][k] = b
    return districts


def prepare(opening, initial, historical):
    """Keep the opening party expectations and exact historical matches fixed throughout."""
    seats = {}
    for seat, places in initial.items():
        if seat not in historical or any(sum(b['fp_votes_current'].values()) for b in places.values()):
            continue
        district = opening['seats'][seat]
        shapes = booth_fp.district_expectations(places, district['fp_mean_percent'], {})
        if not shapes:
            continue
        keys = list(places)
        candidates = {}
        for pid, c in district['candidates'].items():
            party = PARTY.get(c['party'], c['party'])
            if int(pid)<0 or party not in ('ALP','LIB','NAT','GRN','IND','ON') or not c['name']:
                continue
            prior = matched_places(places.values(), historical[seat], party, c['name'])
            if not prior[:,1].any():
                continue
            shares = [v.get(pid,0)/sum(v.values()) if sum(v.values()) else 0
                      for v in (shapes.get(k,{}) for k in keys)]
            candidates[pid] = party, c['name'], prior, np.array(shares)
        if candidates:
            seats[seat] = keys, {k:b['fp_votes_projected'] for k,b in places.items()}, candidates
    return seats


def score_outstanding(row, estimates, final, mask):
    """Score every still-unreported place in a group against its eventual FP.

    These optional recheck data measure whether the relationship reaches the
    right places, including services that arrive later than the replay window.
    The final booth sizes weight the scoring only. They never enter the fit.
    """
    votes, sizes = final[mask].T
    if not sizes.sum():
        return
    errors = 100*np.abs(estimates[:,mask]*sizes-votes).sum(axis=1)/sizes.sum()
    row.update(ballots=sizes.sum(), places=len(sizes), control_error=errors[0],
               unguarded_error=errors[1], guarded_error=errors[2])


def replay_packages(run, through):
    """Use each distinct source observation once, up to the fixed trial endpoint."""
    listing = {}
    for path in sorted((ROOT/'live_runs'/run/'packages').glob('f*')):
        if (path/'evidence.json.gz').exists():
            summary = read(path/'summary.json')
            if summary['source_time'] <= through:
                listing[summary['source_time']] = path, summary
    return [item for _,item in sorted(listing.items())]


def score(run, previous_election, through):
    """Forecast with each evening capture, then score against later returns.

    Compare the same candidate, time and voters for all variants. Each new booth
    is scored only on its first positive report. District error is sampled once
    per candidate and booth-count stage below 50% total FP completion, to avoid
    letting slow seats or duplicate captures dominate the result.
    """
    election = run.split('-')[0]
    captures = replay_packages(run, through)
    initial = load_booths(captures[0][0])
    bases = prepare(captures[0][1], initial, read(ROOT/f'analysis/Booth Results/{previous_election}.json'))
    truth = official_results(election)
    current = initial
    recheck_path = ROOT/f'analysis/Booth Results/{election}.json'
    recheck = read(recheck_path) if recheck_path.exists() else {}
    finals = {(seat,p): matched_places([initial[seat][k] for k in keys], recheck[seat], party, person)
              for seat,(keys,_,candidates) in bases.items() if seat in recheck
              for p,(party,person,_,_) in candidates.items()}
    sampled, seen, rows, arrivals, outstanding = set(), set(), [], [], []
    for step, (_, summary) in enumerate(captures):
        later = load_booths(captures[step+1][0]) if step+1<len(captures) else {}
        seen.update(k for booths in current.values() for k,b in booths.items() if sum(b['fp_votes_current'].values()))
        for seat,(keys, geography, candidates) in bases.items():
            places = current.get(seat,{})
            if places.keys() != geography.keys() or seat not in truth:
                continue
            bs = [places[k] for k in keys]
            sizes = np.array([sum(b['fp_votes_current'].values()) for b in bs])
            rem = np.array([b['expected_remaining_fp'] for b in bs])
            ordinary = np.array([b['booth_type']=='Normal' and b['vote_type']=='Ordinary' for b in bs])
            if (sizes[ordinary]>0).sum()<4 or sizes.sum() >= .5*(rem.sum()+sizes.sum()):
                continue
            fitted = booth_fp.district_expectations(places, summary['seats'][seat]['fp_mean_percent'], geography)
            if not fitted:
                continue
            for pid,(party, person, prev, opening) in candidates.items():
                values = np.array([fitted.get(k,{}).get(pid,0.) for k in keys])
                pred = project(pid, bs, prev, opening, values, sizes, rem, ordinary)
                if pred is None:
                    continue
                n, raw, slope, estimates, projection = pred
                stage = '4-7' if n<8 else '8-15' if n<16 else '16+'
                # Final data enter only the scoring. A current candidate need
                # not have the same name as the previous major-party nominee.
                final = truth[seat]['fp']
                target = [v for who,label,v in final if label==party and (party!='IND' or who==name_key(person))]
                if len(target)!=1:
                    continue
                actual = 100*target[0]/sum(v for _,_,v in final)
                ident = (seat,pid,stage)
                row = dict(election=election, seat=seat, party=party, name=person, time=summary['source_time'],
                           n=n, stage=stage, sample=ident not in sampled, raw=raw, slope=slope, actual=actual)
                row.update(zip(('control','unguarded','guarded','ordinary_only'),projection))
                rows.append(row)
                if (seat,pid) in finals:
                    final = finals[seat,pid]
                    unreported = (sizes==0)&(rem>0)&(prev[:,1]>0)&(final[:,1]>0)
                    for category, selected in [('ordinary',ordinary), ('other',~ordinary)]:
                        record = dict(election=election,seat=seat,party=party,time=row['time'],
                                      sample=row['sample'],category=category)
                        score_outstanding(record, estimates, final, unreported&selected)
                        if 'ballots' in record:
                            outstanding.append(record)
                sampled.add(ident)
                for i in np.flatnonzero((sizes==0)&(rem>0)&(prev[:,1]>0)):
                    k = keys[i]
                    b = later.get(seat,{}).get(k,{})
                    counts = b.get('fp_votes_current',{})
                    total = sum(counts.values())
                    if not total or k in seen:
                        continue
                    got = sum(v for p,v in counts.items() if b.get('count_to_projection_ids',{}).get(p,p)==pid)
                    error = abs(100*estimates[:,i]-100*got/total)
                    arrivals.append(dict(election=election, seat=seat, party=party, booth=k, ordinary=ordinary[i],
                                         stage=stage, votes=total, **dict(zip(('control','unguarded','guarded','ordinary_only'),error))))
        current = later
    return rows, arrivals, outstanding


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,help='Optional directory for the district and arrival tables')
    parser.add_argument('--runs',nargs='+',choices=RUNS,default=list(RUNS))
    args = parser.parse_args()
    districts, arrivals, outstanding = [], [], []
    for run in args.runs:
        rows, booths, rechecks = score(run, *RUNS[run])
        districts.extend(rows)
        arrivals.extend(booths)
        outstanding.extend(rechecks)
    districts, arrivals = pd.DataFrame(districts), pd.DataFrame(arrivals)
    methods = ['control','unguarded','guarded','ordinary_only']
    sample = districts[districts['sample']].copy()
    sample[methods] = sample[methods].sub(sample.actual,axis=0).abs()
    print('Mean absolute final district FP error (pp), one observation per candidate/stage')
    print(sample.groupby('election')[methods].mean().round(3))
    print('Mean absolute first-return FP error (pp), weighted by observed ballots')
    for group, frame in arrivals.groupby(['election','ordinary']):
        print(group,len(frame),np.average(frame[methods],axis=0,weights=frame.votes).round(3))
    if args.output:
        args.output.mkdir(parents=True,exist_ok=True)
        districts.to_csv(args.output/'districts.csv',index=False)
        arrivals.to_csv(args.output/'arrivals.csv',index=False)
        pd.DataFrame(outstanding).to_csv(args.output/'outstanding.csv',index=False)


if __name__=='__main__':
    main()

"""Compare frozen district FP forecasts and the booth-update prototype against later counts.

Run from the repository root with the native analysis Python. The defaults use
three nine-capture diagnostic sequences. Package replays can also be supplied. The
reference results are read only for scoring; every prediction uses its own capture
and the uncounted opening for the same district and run.
"""
import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path
import sys
import unicodedata

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'analysis'))
from scripts.exploratory.booth_fp_update import adjusted_shares, booth_fp, fit_shift, group_votes, read
from live_pipeline import comparisons

SA_FINAL = ROOT / 'downloads/turnout/2026sa/version-912-b1bb7181a5f8'
STRENGTHS = (250, 500, 1000, 3000, 6000)


def name_key(name):
    if ',' in name:
        last, first = name.split(',', 1)
        name = first + ' ' + last
    return ''.join(ch for ch in unicodedata.normalize('NFKD', name.lower()) if ch.isalnum())


def official_results(election):
    """Candidate FP counts and the two actual finalists, on full district totals.
    SA's FP summaries require adding the separately reported absent ordinary votes.
    Use the distribution only to identify the final pair and their final totals.
    """
    truth = defaultdict(lambda: {'fp': [], 'tcp': []})
    if election in ('2018vic', '2022vic', '2022fed', '2025fed'):
        codes = {'Labor':'ALP','Liberal':'LIB','National':'NAT','Greens':'GRN','Independent':'IND',
                 'One Nation':'ON','Centre Alliance':'CA',"Katter's Australian":'KAP'}
        with (ROOT/f'analysis/elections/results_{election}.csv').open(encoding='utf-8-sig', newline='') as f:
            kind = None
            for row in csv.reader(f):
                if not row:
                    continue
                if row[0] == 'Seat':
                    seat = row[1]
                    kind = None
                elif row[0] in ('fp','tcp'):
                    kind = row[0]
                elif len(row) >= 4 and kind in ('fp','tcp'):
                    truth[seat][kind].append((name_key(row[0]), codes.get(row[1], row[1]), int(row[2])))
        return truth
    if election != '2026sa':
        raise ValueError(f'No outcome reader for {election}.')
    static = {d['districtName']: d for d in read(SA_FINAL/'static.json')['districts']}
    for d in read(SA_FINAL/'results.json')['districts']:
        seat = d['districtId']
        people = {c['candidateId']: (name_key(c['candidateName']), c['partyId'].replace('ONP','ON'))
                  for c in static[seat]['candidates']}
        counts = {c['candidateId']: c['ordinaryVotes'] + c['declarationVotes'] for c in d['candidates']}
        for batch in d['absentOrdinary']:
            for c in batch['candidateVotes']:
                counts[c['candidateId']] += c['votes']
        truth[seat]['fp'] = [(*people[k], n) for k, n in counts.items()]
        parties = dict(people.values())
        for c in d['finalDistribution'][-1]['candidateResults']:
            if c['progressiveTotal']:
                name = name_key(c['candidateName'])
                truth[seat]['tcp'].append((name, parties[name], c['progressiveTotal']))
    assert sum(n for t in truth.values() for _,_,n in t['fp']) == 1115864
    return truth


def snapshot(path):
    """Read the same projection fields from either a website package or a diagnostic.
    For independents the commentary exporter already maps commission IDs to the
    modelled candidate. Keep the other independents in the residual group."""
    if path.is_dir():
        summary = read(path/'summary.json')
        evidence = read(path/'evidence.json.gz')
    else:
        raw = read(path)
        report = raw['simulation_report']
        parties = {str(p['party_index']): p['abbreviation'] for p in raw['parties']}
        evidence = comparisons.evidence({'diagnostic_file':str(path), 'report':{'partyAbbr':parties}})
        seats = {}
        for i, seat in enumerate(report['seat_name']):
            names = {str(r['party_index']): {'name':r['value']} for r in report['seat_candidate_names'][i]}
            pair_keys = lambda r: '/'.join(str(r[p]['party_index']) for p in ('party_a','party_b'))
            seats[seat] = dict(candidates=names,
                fp_mean_percent={str(r['party_index']): r['value'] for r in report['seat_party_mean_fp_share'][i]},
                tcp_percentiles={pair_keys(r): {'50': r['values'][report['probability_bands'].index(50)]}
                                 for r in report['seat_tcp_probability_band'][i]},
                pairing_percent={pair_keys(r):100*r['value'] for r in report['seat_tcp_scenario_percent'][i]})
        summary = dict(parties=parties, seats=seats, source_time=path.name.split('_')[1])
    for seat,district in summary['seats'].items():
        integration = evidence['seats'].get(seat,{}).get('fp_integration') or {}
        shared = integration.get('shared_deviation')
        district['shared'] = {str(r['party_index']):r['value']/25 for r in shared} if shared is not None else None
    places = defaultdict(dict)
    for k,b in evidence['booths'].items():
        places[b['seat_name']][k] = b
    return summary, places


def actual_vector(outcome, parties, labels, names):
    """Align party totals and, for IND, only the named person's votes."""
    total = sum(n for _,_,n in outcome['fp'])
    def find(person, label):
        return next((p for p in parties if labels[p] == label and
                     (label != 'IND' or person == name_key(names.get(p, {}).get('name') or ''))), None)
    votes = Counter()
    for person, label, n in outcome['fp']:
        votes[find(person,label)] += n
    finalists = [(find(person,label), label, n) for person,label,n in outcome['tcp']]
    return 100*group_votes(votes, parties, {})/total, finalists


def shared_background(prior, shapes, districts, parties):
    """Sensitivity using only the exported statewide and regional log-odds
    movements available by the prediction time. Apply each party's change
    since opening, then normalise the shares at each venue. The full simulator
    reconciles draws separately. This is an approximation to that background."""
    old, new = [d.get('shared') for d in districts]
    if old is None or new is None:
        raise ValueError('This series did not export the shared FP movement.')
    delta = np.array([new.get(p,0)-old.get(p,0) for p in parties] + [0.])
    multiplier = np.exp(delta)
    def revise(mix):
        values = mix*multiplier/(1+mix*(multiplier-1))
        return values/values.sum(axis=-1,keepdims=True)
    return revise(prior), revise(shapes)


def opening_backgrounds(opening, initial, truth, skipped):
    """Select the separately modelled parties before counting and retain the
    opening booth compositions. Newly recognised candidates stay in the pooled
    remainder for this comparison, including on the existing forecast side."""
    labels = {p:('LIB' if v=='LNP' else v) for p,v in opening['parties'].items()}
    backgrounds = {}
    for seat, places in initial.items():
        if seat not in truth or seat == 'Narracan' or any(sum(b['fp_votes_current'].values()) for b in places.values()):
            continue
        means = opening['seats'][seat]['fp_mean_percent']
        parties = sorted(p for p in means if int(p)>=0 and means[p]>0 and labels.get(p) in
                         ('ALP','LIB','GRN','IND','NAT','ON','KAP','CA'))
        fitted = booth_fp.district_expectations(places, means, {})
        if not fitted:
            skipped['no opening booth fit'] += 1
            continue
        mix = {k: group_votes(v, parties, {}) for k,v in fitted.items()}
        backgrounds[seat] = (parties, {k:v/v.sum() for k,v in mix.items() if v.sum()},
                             group_votes(means,parties,{})/sum(means.values()))
    return backgrounds, labels


def fp_error(prediction, actual):
    """Percentage of ballots that must move between FP groups to reproduce the result."""
    return np.abs(prediction-actual).sum()/2


def score_district(record, published, estimates, true, parties, labels, pair, district):
    """Measure error in the final district primary and in the gap between the
    actual finalists. Both use all counted and projected ballots in the seat."""
    record('district FP',fp_error(published,true),{s:fp_error(v,true) for s,v in estimates.items()})
    for j,p in enumerate(parties):
        record('candidate '+labels[p],abs(published[j]-true[j]),{s:abs(v[j]-true[j]) for s,v in estimates.items()})
    if len(pair)!=2 or any(p not in parties for p,_,_ in pair):
        return
    (p,_,n),(q,_,m) = pair
    j,k = parties.index(p),parties.index(q)
    def gap_error(values):
        return abs((values[j]-values[k])-(true[j]-true[k]))
    record('finalists FP gap',gap_error(published),{s:gap_error(v) for s,v in estimates.items()})
    # A sensitivity only: keep the published forecast's implied share of all
    # other FP going to p fixed. Require a dominant pair so unconditional FP
    # and conditional TCP are comparable. Final flows never enter predictions.
    key = p+'/'+q if p+'/'+q in district['tcp_percentiles'] else q+'/'+p
    if district['pairing_percent'].get(key,0)<65 or key not in district['tcp_percentiles']:
        return
    old = district['tcp_percentiles'][key]['50']
    old = old if key==p+'/'+q else 100-old
    pool = 100-published[j]-published[k]
    flow = (old-published[j])/pool if pool>0 else -1
    if 0<=flow<=1:
        target = 100*n/(n+m)
        record('fixed-flow TCP',abs(old-target),{s:abs(v[j]+flow*(100-v[j]-v[k])-target)
                                                 for s,v in estimates.items()})


def score_arrivals(record, local, later, opening_places, district, parties, keys, fresh, predicted, skipped):
    """Compare both earlier estimates with the same first observed batch.
    A declaration category may initially report just part of its final count."""
    opening_shapes = {k:b['fp_votes_projected'] for k,b in opening_places.items()}
    baseline = booth_fp.district_expectations(local,district['fp_mean_percent'],opening_shapes)
    for i in fresh:
        key = keys[i]
        if key not in baseline or not sum(baseline[key].values()):
            skipped['unavailable published booth comparison'] += 1
            continue
        b = later[key]
        actual = group_votes(b['fp_votes_current'],parties,b.get('count_to_projection_ids',{}))
        observed = 100*actual/actual.sum()
        old = 100*group_votes(baseline[key],parties,{})/sum(baseline[key].values())
        if b['vote_type'] != 'Ordinary':
            metric = 'next declaration category'
        elif b['booth_type']=='Normal':
            metric = 'next ordinary booth'
        else:
            metric = 'next other physical booth'
        record(metric,fp_error(old,observed),{s:fp_error(100*v[i],observed) for s,v in predicted.items()},
               actual.sum(),booth=key)


def median_adjustment(opening, counts):
    """Difference between the middle booth departure and the average departure.

    Give each reported formal ballot one vote in selecting the middle booth,
    separately for each FP group. At an exact halfway tie average the two
    middle observations. Return share differences on the 0–1 scale.
    """
    sizes = counts.sum(axis=1)
    departures = counts/sizes[:, None] - opening
    order = np.argsort(departures, axis=0)
    sorted_shares = np.take_along_axis(departures, order, axis=0)
    cumulative = np.cumsum(sizes[order], axis=0)
    half = sizes.sum()/2
    indices = np.arange(counts.shape[1])
    lower = sorted_shares[(cumulative >= half).argmax(axis=0), indices]
    upper = sorted_shares[(cumulative > half).argmax(axis=0), indices]
    return (lower+upper)/2 - np.average(departures, weights=sizes, axis=0)


def apply_median(fitted, opening, counts, fraction, prior_votes):
    """Adjust only projected party shares; the caller adds real ballots in full.

    Use the same effective ballot total as the ordinary fit to taper early
    evidence. Medians for several parties need not add to zero: remove any
    negative share and renormalise within each booth on its supported parties.
    """
    sizes = counts.sum(axis=1)
    effective = sum(sizes/(1+sizes/500))
    shift = fraction*effective/(effective+prior_votes)*median_adjustment(opening,counts)
    moved = np.where(fitted>0,np.maximum(0, fitted+shift),0)
    totals = moved.sum(axis=1,keepdims=True)
    return np.divide(moved, totals, out=fitted.copy(), where=totals>0)


def score_run(folder, truth, use_shared=False, ordinary_only=False, other_prior=None, median_fraction=0):
    packages = folder/'packages'
    paths = sorted(packages.glob('f*')) if packages.is_dir() else sorted(folder.glob('snapshot_*.json'))
    opening, initial = snapshot(paths[0])
    rows, skipped = [], Counter()
    backgrounds, labels = opening_backgrounds(opening, initial, truth, skipped)
    # Score next appearances exactly once. A district endpoint is eligible once
    # it has local ordinary evidence. Repeated endpoints are averaged within seat
    # in the report so that frequent captures do not make one district dominate.
    current, places = opening, initial
    seen = set()
    for step in range(len(paths)):
        following = snapshot(paths[step+1]) if step+1 < len(paths) else None
        # A restoration after a withdrawal is not a previously unseen booth.
        # Track every reported location, even at endpoints excluded from scoring.
        seen.update(k for venues in places.values() for k,b in venues.items() if sum(b['fp_votes_current'].values()))
        for seat,(parties, base, prior) in backgrounds.items():
            district = current['seats'][seat]
            local = places[seat]
            keys = [k for k,b in local.items() if sum(b['fp_votes_current'].values()) or b['expected_remaining_fp']>0]
            if not keys or any(k not in base for k in keys):
                skipped['missing opening location'] += 1
                continue
            counts = np.array([group_votes(local[k]['fp_votes_current'],parties,
                                           local[k].get('count_to_projection_ids',{})) for k in keys])
            sizes = counts.sum(axis=1)
            ordinary = np.array([local[k]['booth_type']=='Normal' and local[k]['vote_type']=='Ordinary' for k in keys])
            use = ordinary & (sizes>0)
            if not use.any():
                continue
            shapes = np.array([base[k] for k in keys])
            if use_shared:
                prior, shapes = shared_background(prior, shapes, (opening['seats'][seat],district), parties)
            remaining = np.array([max(0,b['expected_remaining_fp']) for b in (local[k] for k in keys)])
            if np.any((shapes[use]==0) & (counts[use]>0)):
                skipped['missing opening support'] += 1
                continue
            published = group_votes(district['fp_mean_percent'],parties,{})
            true, pair = actual_vector(truth[seat], parties, labels, opening['seats'][seat]['candidates'])
            matchup = '/'.join(sorted(label for _,label,_ in pair))
            coverage = sizes.sum()/(sizes.sum()+remaining.sum())
            stage = '1 booth' if use.sum()==1 else '2-4 booths' if use.sum()<5 else '5+ booths'
            modal = max(district['pairing_percent'].values(), default=0)
            common = dict(seat=seat, matchup=matchup, step=step, stage=stage, coverage=coverage,
                          modal_pair_percent=modal)
            predicted = {}
            for strength in STRENGTHS:
                shift = fit_shift(shapes[use],counts[use],prior,strength,500)
                predicted[strength] = adjusted_shares(shapes,shift)
                if median_fraction and use.sum()>=3:
                    opening_shapes = np.array([base[k] for k in keys])
                    predicted[strength][ordinary] = apply_median(
                        predicted[strength][ordinary],opening_shapes[use],counts[use],median_fraction,strength)
            if ordinary_only or other_prior is not None:
                # Keep the ordinary forecast for comparable places. Other types
                # can retain their background or learn more cautiously from the
                # ordinary reports. Only outstanding ballots use these estimates.
                other = (shapes if ordinary_only else predicted[other_prior])[~ordinary]
                for mix in predicted.values():
                    mix[~ordinary] = other
            def record(metric, old, new, weight=1, **extra):
                rows.append(dict(**common, metric=metric, existing=old, weight=weight, **extra,
                                 **{str(s):v for s,v in new.items()}))
            estimates = {s:100*(counts.sum(axis=0)+remaining @ mix)/(sizes.sum()+remaining.sum())
                         for s,mix in predicted.items()}
            score_district(record,published,estimates,true,parties,labels,pair,district)
            if not following:
                continue
            fresh = [i for i,k in enumerate(keys) if not sizes[i] and k not in seen
                     and sum(following[1][seat].get(k,{}).get('fp_votes_current',{}).values())>0]
            if not fresh:
                continue
            score_arrivals(record,local,following[1][seat],initial[seat],district,parties,keys,fresh,predicted,skipped)
        if following:
            current, places = following
    print(folder.name, 'captures',len(paths), 'eligible opening fits',len(backgrounds), 'omissions', dict(skipped))
    return pd.DataFrame(rows)


def scores(frame, by):
    cols = ['existing',*map(str,STRENGTHS)]
    weights = frame.weight
    total = frame[cols].multiply(weights,axis=0).groupby([frame[k] for k in ['seat',*by]]).sum()
    means = total.div(frame.groupby(['seat',*by]).weight.sum(),axis=0)
    return means.groupby(by).mean().assign(districts=means.groupby(by).size()).round(3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs',nargs='*', default=['2022vic-testing5','2026sa-testing1','2025fed-testing2'])
    parser.add_argument('--output',type=Path, help='Optional paired errors for further analysis')
    parser.add_argument('--shared',action='store_true',help='Use regional and statewide FP movements since opening')
    other = parser.add_mutually_exclusive_group()
    other.add_argument('--ordinary-only',action='store_true',
                        help='Extend the ordinary-booth movement only to other ordinary election-day booths')
    other.add_argument('--other-prior',type=int,choices=STRENGTHS,
                       help='Use this starting weight when extrapolating to other vote types')
    parser.add_argument('--median-fraction',type=float,choices=(0,.25,.5,1),default=0,
                        help='Move outstanding ordinary FP towards the vote-weighted median departure')
    args = parser.parse_args()
    frames = []
    for run in args.runs:
        frame = score_run(ROOT/'live_runs'/run,official_results(run.split('-')[0]), args.shared,
                          args.ordinary_only,args.other_prior,args.median_fraction)
        print(scores(frame,['metric']).to_string())
        print(scores(frame[frame.metric.isin(['district FP','next ordinary booth'])],['metric','matchup']).to_string())
        frames.append(frame.assign(run=run))
    if args.output:
        pd.concat(frames).to_csv(args.output,index=False)

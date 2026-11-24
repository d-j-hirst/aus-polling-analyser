"""Test whether disagreement between counted ordinary booths predicts FP uncertainty.

Each candidate appears once per counting stage. The returns available at the
time define disagreement. Later booth returns and the final district result
only score the forecasts. All shares and errors in the output are percentages
or percentage points.
"""
import argparse
from collections import Counter

import numpy as np
import pandas as pd

from nonmajor_booth_response import projection_inputs
from score_booth_fp_update import (
    ROOT, actual_vector, adjusted_shares, apply_median, booth_fp, fit_shift, group_votes,
    median_adjustment, official_results, opening_backgrounds, read, shared_background, snapshot,
)


RUNS = ('2022vic-testing5','2026sa-testing1','2025fed-testing2')


def mildura_trace(path, background, opening, current, local):
    parties, base, prior = background
    _, shapes, counts, ordinary, remaining = projection_inputs(local, base, parties)
    j = next(i for i,p in enumerate(parties) if opening['parties'][p]=='IND')
    reported = ordinary & (counts.sum(axis=1)>0)
    sizes = counts[reported].sum(axis=1)
    effective = sum(sizes/(1+sizes/500))
    start = shapes[reported]
    prior, shapes = shared_background(prior,shapes,(opening['seats']['Mildura'],current['seats']['Mildura']),parties)
    fitted = adjusted_shares(shapes,fit_shift(shapes[reported],counts[reported],prior,1000,500))
    new = fitted.copy()
    new[ordinary] = apply_median(fitted[ordinary],start,counts[reported],1,1000)
    delta = 100*median_adjustment(start,counts[reported])[j]
    whole = counts.sum()+remaining.sum()
    print('Mildura',path.name,dict(effective=effective,weight=effective/(effective+1000),
          raw_shift=delta,initial_shift=delta*effective/(effective+1000),
          outstanding_ordinary=remaining[ordinary].sum(),district_turnout=whole,
          ordinary_shift=100*np.average((new-fitted)[ordinary,j],weights=remaining[ordinary]),
          district_before=100*(counts[:,j].sum()+remaining@fitted[:,j])/whole,
          district_after=100*(counts[:,j].sum()+remaining@new[:,j])/whole),flush=True)


def new_booth_scores(local, later, initial, means, parties, seen):
    """Test the FP forecast available before the next ordinary booths arrived.

    Weight errors by the formal ballots actually reported at those booths. A
    first batch and an entire booth are scored at their observed sizes.
    """
    fresh = [k for k,b in later.items() if k in local and k not in seen and b['booth_type']=='Normal'
             and b['vote_type']=='Ordinary' and sum(b['fp_votes_current'].values())>0]
    baseline = booth_fp.district_expectations(local,means,{k:b['fp_votes_projected'] for k,b in initial.items()}) if fresh else {}
    errors, weights = [], []
    for k in fresh:
        size = sum(baseline.get(k,{}).values())
        if not size:
            continue
        b = later[k]
        actual = group_votes(b['fp_votes_current'],parties,b.get('count_to_projection_ids',{}))
        errors.append(100*abs(actual/actual.sum()-group_votes(baseline[k],parties,{})/size))
        weights.append(actual.sum())
    return np.average(errors,weights=weights,axis=0) if errors else np.full(len(parties)+1,np.nan)


def observations(run):
    files = sorted((ROOT/'live_runs'/run).glob('snapshot_*.json'))
    election = run.split('-')[0]
    truth = official_results(election)
    opening, initial = snapshot(files[0])
    backgrounds, labels = opening_backgrounds(opening, initial, truth, Counter())
    seen, sampled, rows, tcp_rows = set(), set(), [], []
    now, booths = opening, initial
    for step,path in enumerate(files):
        following = snapshot(files[step+1]) if step+1<len(files) else None
        seen.update(k for local in booths.values() for k,b in local.items() if sum(b['fp_votes_current'].values()))
        report = read(path)['simulation_report']
        bands = [report['probability_bands'].index(x) for x in (10,50,90)]
        for i,seat in enumerate(report['seat_name']):
            if seat not in backgrounds:
                continue
            parties, base, prior = backgrounds[seat]
            local = booths[seat]
            if any(k not in base for k,b in local.items() if sum(b['fp_votes_current'].values()) or b['expected_remaining_fp']):
                continue
            if seat=='Mildura' and path.name.startswith('snapshot_20221126191504'):
                mildura_trace(path,backgrounds[seat],opening,now,local)
            _, shapes, counts, ordinary, remaining = projection_inputs(local,base,parties)
            sizes = counts.sum(axis=1)
            used = ordinary & (sizes>0)
            n = int(used.sum())
            coverage = sizes.sum()/(sizes.sum()+remaining.sum())
            if n<3 or coverage>.5 or not remaining[ordinary].sum():
                continue
            stage = '3-5' if n<=5 else '6-10' if n<=10 else '11+'
            if (seat,stage) in sampled:
                continue
            sampled.add((seat,stage))
            # Measure variability around the average booth surprise. Booths
            # can agree closely on either a large swing or a small one.
            rates = 100*(counts[used]/sizes[used,None]-shapes[used])
            mean = np.average(rates,weights=sizes[used],axis=0)
            sd = np.sqrt(np.average((rates-mean)**2,weights=sizes[used],axis=0))
            gap = 100*abs(median_adjustment(shapes[used],counts[used]))
            actual,finalists = actual_vector(truth[seat],parties,labels,opening['seats'][seat]['candidates'])
            later_error = new_booth_scores(local,following[1][seat] if following else {},initial[seat],
                                          now['seats'][seat]['fp_mean_percent'],parties,seen)
            for entry in report['seat_fp_probability_band'][i]:
                pid = str(entry['party_index'])
                if pid not in parties:
                    continue
                j = parties.index(pid)
                if prior[j]<.05 or actual[j]<=0:
                    continue
                lo,med,hi = (entry['values'][b] for b in bands)
                if hi<=med or med<=lo:
                    continue
                rows.append(dict(election=election,time=path.name.split('_')[1],seat=seat,party=labels[pid],
                                 stage=stage,coverage=coverage,booths=n,sd=sd[j],gap=gap[j],low=lo,median=med,
                                 high=hi,actual=actual[j],next_error=later_error[j]))
            # Also test the common early situation where only primaries have
            # arrived. Does disagreement about the FP gap between the likely
            # finalists identify unreliable *TCP* intervals? Select the likely
            # pair at forecast time, then require that it can actually be scored.
            district = now['seats'][seat]
            pair,probability = max(district['pairing_percent'].items(),key=lambda r:r[1],default=('',0))
            final = {p:n for p,_,n in finalists}
            if probability<65 or set(pair.split('/'))!=set(final) or not set(final)<=set(parties):
                continue
            p,q = pair.split('/')
            tcp_band = next((r for r in report['seat_tcp_probability_band'][i]
                             if str(r['party_a']['party_index'])==p and str(r['party_b']['party_index'])==q),None)
            if not tcp_band:
                continue
            lo,med,hi = (tcp_band['values'][b] for b in bands)
            if not lo<med<hi:
                continue
            gap = rates[:,parties.index(p)]-rates[:,parties.index(q)]
            spread = np.sqrt(np.average((gap-np.average(gap,weights=sizes[used]))**2,weights=sizes[used]))
            classic = 'ALP' in (labels[p],labels[q]) and ('LIB' in (labels[p],labels[q]) or 'NAT' in (labels[p],labels[q]))
            tcp_rows.append(dict(election=election,time=path.name.split('_')[1],seat=seat,
                                 party='classic' if classic else 'nonclassic',stage=stage,booths=n,sd=spread,
                                 low=lo,median=med,high=hi,actual=100*final[p]/sum(final.values()),next_error=np.nan))
        if following:
            now,booths = following
    return pd.DataFrame(rows),pd.DataFrame(tcp_rows)


def interval_score(frame, scale):
    # The central 80% interval earns a penalty for width and an additional
    # ten points per point by which the result misses either end. This checks
    # whether the extra coverage pays for publishing a wider interval.
    lo = (frame['median'] - scale*(frame['median']-frame.low)).clip(lower=0)
    hi = (frame['median'] + scale*(frame.high-frame['median'])).clip(upper=100)
    miss = (lo-frame.actual).clip(lower=0)+(frame.actual-hi).clip(lower=0)
    return (miss==0)*100, hi-lo+10*miss


def held_out_checks(df, signal, quantile=.5):
    """Judge a simple widening rule on an election excluded from its threshold.

    Classify high disagreement relative to typical readings for the same
    party group and counting stage in the other two elections. Group the two
    major sides together and keep IND, GRN and other parties separate. Omit
    groups with fewer than ten training examples.
    """
    parts = []
    df = df.assign(family=df.party.replace({'NAT':'major','LIB':'major','ALP':'major','CA':'other','KAP':'other'}))
    distance = abs(df.actual-df['median'])
    df = df.assign(scaled_error=distance/np.where(df.actual>=df['median'],df.high-df['median'],df['median']-df.low))
    for election,test in df.groupby('election'):
        training = df[df.election!=election]
        cells = ['family','stage']
        train = training.groupby(cells)[signal]
        cuts = train.quantile(quantile).where(train.count()>=10).rename('cutoff')
        test = test.join(cuts,on=['family','stage']).dropna(subset=['cutoff']).copy()
        test['large'] = test[signal]>test.cutoff
        test['fp_error'] = abs(test.actual-test['median'])
        test['coverage80'],test['score'] = interval_score(test,1)
        test['wide80'],test['wide_score'] = interval_score(test,np.where(test.large,1.20,1.0))
        # Is disagreement informative once the overall width is calibrated?
        # The 80th percentile of scaled errors contains 80% of training
        # outcomes. Compare this common scale against one also conditioned
        # on disagreement. Pull sparse subgroup scales towards the common
        # scale using 40 comparison cases as a trial starting weight.
        training = training.join(cuts,on=cells).dropna(subset=['cutoff']).copy()
        training['large'] = training[signal]>training.cutoff
        common = training.groupby(cells).scaled_error.quantile(.8).rename('common')
        within = training.groupby(cells+['large']).scaled_error
        scales = pd.concat([within.quantile(.8).rename('specific'),within.count().rename('n')],axis=1)
        test = test.join(common,on=cells).join(scales,on=cells+['large'])
        weight = test.n.fillna(0)/(test.n.fillna(0)+40)
        split = test.common+weight*(test.specific.fillna(test.common)-test.common)
        test['common_coverage'],test['common_score'] = interval_score(test,test.common)
        test['split_coverage'],test['split_score'] = interval_score(test,split)
        parts.append(test)
    return pd.concat(parts)


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',help='Optional output for the candidate and stage comparisons')
    args = parser.parse_args()
    samples = [observations(run) for run in RUNS]
    df,tcp = (pd.concat(tables,ignore_index=True) for tables in zip(*samples))
    if args.output:
        df.to_csv(args.output,index=False)
        tcp.to_csv(str(args.output)+'.tcp.csv',index=False)
    for signal,table,measure in [('sd',df,'FP'),('gap',df,'FP'),('sd',tcp,'TCP from finalist FP gap')]:
        checks = held_out_checks(table,signal)
        print(measure,signal,'held-out comparisons')
        print(checks.groupby(['election','large']).agg(cases=('actual','size'),next=('next_error','mean'),
              district=('fp_error','mean'),relative=('scaled_error','median'),coverage=('coverage80','mean'),
              widened=('wide80','mean'),score=('score','mean'),widened_score=('wide_score','mean')).round(2).to_string())
        print('Common width versus width conditional on disagreement')
        print(checks.groupby('election')[['common_coverage','split_coverage','common_score','split_score']].mean().round(2))

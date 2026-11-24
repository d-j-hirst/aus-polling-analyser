"""Compare the accuracy and successive revisions of ordinary-booth FP updates.

The principal comparison keeps the counted election-day booths as evidence and
varies which outstanding voters receive their correction. A separate option
also includes early and declaration returns in the middle-booth calculation.
"""
import argparse
from collections import Counter
import hashlib
from pathlib import Path
import sys

import numpy as np
import pandas as pd

if not __package__:
    sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from scripts.exploratory.nonmajor_booth_response import projection_inputs, dominant_pair_summary, outstanding_mixes
from scripts.exploratory.score_booth_fp_update import (
    ROOT, actual_vector, apply_median, fp_error, group_votes,
    official_results, opening_backgrounds, read, shared_background, snapshot,
)


def trial_centres(opening, shapes, counts, ordinary, remaining, prior, all_sources=False):
    counted = counts.sum(axis=1)>0
    reported = ordinary & counted
    total = counts.sum()+remaining.sum()
    mixes = outstanding_mixes(shapes,counts,ordinary,prior)
    fixed = counts.sum(axis=0)
    estimates = {'split/0/none': 100*(fixed+remaining@mixes['by_type'])/total}
    for strength,fit in ((250,mixes['strong']),(1000,mixes['cautious'])):
        variants = [(0,'none'),(.5,'ordinary'),(.5,'all'),(1,'ordinary'),(1,'all')]
        if all_sources:
            variants.extend([(.5,'all_sources'),(1,'all_sources')])
        for fraction,scope in variants:
            mix = fit
            evidence = counted if scope=='all_sources' else reported
            if fraction and evidence.sum()>=3:
                changed = apply_median(fit,opening[evidence],counts[evidence],fraction,strength)
                mix = np.where(ordinary[:,None],changed,fit) if scope=='ordinary' else changed
            estimates[f'{strength}/{fraction}/{scope}'] = 100*(fixed+remaining@mix)/total
    return estimates


def local_ballots(places):
    """Identify changes to reported candidates' FP or TCP, including rechecks
    that leave district turnout unchanged. Empty feed rows add no ballots."""
    ballots = sorted((key,kind,party,n) for key,b in places.items()
                     for kind in ('fp_votes_current','tcp_votes_current')
                     for party,n in (b.get(kind) or {}).items() if n)
    return hashlib.blake2b(repr(ballots).encode(),digest_size=12).hexdigest()


def capture_series(run, all_sources=False, include_safe=False):
    paths = sorted((ROOT/'live_runs'/run).glob('snapshot_*.json'))
    if len(paths)<2:
        raise ValueError(f'{run} needs an opening forecast and at least one further snapshot')
    truth = official_results(run.split('-')[0])
    opening,initial = snapshot(paths[0])
    backgrounds,labels = opening_backgrounds(opening,initial,truth,Counter())
    rows = []
    for step,path in enumerate(paths):
        current,places = snapshot(path) if step else (opening,initial)
        export = read(path)
        report = export['simulation_report']
        source = export.get('run',{}).get('input_snapshot_file')
        if step==0:
            competitive = {seat for seat, odds in zip(report['seat_name'],report['seat_party_win_percent'])
                           if max((p['value'] for p in odds), default=100)<95}
        for i,seat in enumerate(report['seat_name']):
            if seat not in backgrounds:
                continue
            parties,base,prior = backgrounds[seat]
            local = places[seat]
            local_state = local_ballots(local)
            if any(k not in base for k,b in local.items() if sum(b['fp_votes_current'].values()) or b['expected_remaining_fp']):
                continue
            booths,shapes,counts,ordinary,remaining = projection_inputs(local,base,parties)
            if not len(booths) or not counts.sum()+remaining.sum():
                continue
            district = current['seats'][seat]
            shared_shapes = shapes
            if district['shared'] is not None and opening['seats'][seat]['shared'] is not None:
                prior,shared_shapes = shared_background(prior,shapes,(opening['seats'][seat],district),parties)
            estimates = trial_centres(shapes,shared_shapes,counts,ordinary,remaining,prior,all_sources)
            estimates['replay'] = group_votes(district['fp_mean_percent'],parties,{})
            final,final_pair = actual_vector(truth[seat],parties,labels,opening['seats'][seat]['candidates'])
            pair = max(district['pairing_percent'],key=district['pairing_percent'].get,default='')
            focal = pair.split('/')[0]
            winner = max(final_pair,key=lambda r:r[2])[0] if final_pair else None
            odds = (dominant_pair_summary(report,i,focal,district,booths,counts,counts.sum()+remaining.sum(),
                                          parties,estimates,bound_tails=True) if include_safe or seat in competitive else {})
            for name,v in estimates.items():
                win = odds.get('win_'+name,np.nan)
                rows.append(dict(step=step,feed=source,seat=seat,pair=pair,model=name,fp=fp_error(v,final),
                                 local_ballots=local_state,
                                 local=bool(counts[ordinary].sum()),win=win,
                                 won=float(focal==winner) if winner is not None or
                                     any(p==focal for p,_,_ in final_pair) else np.nan))
    return pd.DataFrame(rows)


def forecast_revisions(df):
    # Compare the same named candidate and same pairing across consecutive
    # captures. Missing intermediate estimates break a sequence.
    df = df.sort_values(['seat','pair','model','step']).copy()
    group = df.groupby(['seat','pair','model'])
    # 0.05% bounds give finite log scores even for archived unanimous draws.
    # The same bound and nearest archived percentile tail apply to every method.
    p = df.win.clip(.05,99.95)/100
    q = group.win.shift().clip(.05,99.95)/100
    adjacent = df.step-group.step.shift()==1
    # Use percentage points for significance and the slope. Only the log
    # score needs the endpoint bound, so 95% to 100% is a full 5 pp move.
    win = df.win.clip(0,100)
    df['delta'] = (win-group.win.shift().clip(0,100)).where(adjacent)
    df['jump'] = (p*np.log(p/q)+(1-p)*np.log((1-p)/(1-q))).where(adjacent)
    df['previous_move'] = group.delta.shift().where(adjacent)
    df['local_change'] = adjacent & df.local_ballots.ne(group.local_ballots.shift())
    df['previous_local_change'] = group.local_change.shift().eq(True) & adjacent
    df['brier'] = (p-df.won)**2
    df.loc[~df.local,'fp'] = np.nan
    return df


def score_series(df, min_move_pp=5., local_only=False, trigger_model=None):
    """Ask if the next forecast movement is predictable after a material move.

    Choose qualifying moves from information available at that capture. A
    quiet next update is a genuine outcome and remains in the regression.
    The optional common trigger selects the same seat and time across methods.
    Accuracy and average revision size always use their full eligible samples.
    """
    if not np.isfinite(min_move_pp) or min_move_pp<0:
        raise ValueError('The minimum win-chance movement must be finite and nonnegative')
    df = forecast_revisions(df)
    previous = df.previous_move
    if trigger_model is not None:
        keys = ['seat','pair','step']
        reference = df[df.model==trigger_model].set_index(keys).previous_move
        if reference.empty:
            raise ValueError(f'Unknown trigger model: {trigger_model}')
        trigger = reference.reindex(pd.MultiIndex.from_frame(df[keys])).to_numpy()
    else:
        trigger = previous
    qualifies = (abs(trigger)>=min_move_pp) & df.delta.notna() & previous.notna()
    if local_only:
        qualifies &= df.previous_local_change
    df['lag_product'] = (previous*df.delta).where(qualifies)
    df['lag_square'] = previous.pow(2).where(qualifies)
    seats = df.groupby(['model','seat'])[['fp','jump','lag_product','lag_square','brier']].mean()
    result = seats.groupby('model').mean()
    # The original trial averaged observations inside each district first.
    # Retain that statistic for comparison. For the main slope, give each
    # qualifying revision one observation. Adding quiet triples elsewhere in
    # the same seat must not reduce the weight of an existing large revision.
    result['equal_seat_lag'] = result.lag_product/result.lag_square
    totals = df.groupby(['model','seat'])[['lag_product','lag_square']].sum(min_count=1)
    pooled = totals.groupby('model').sum(min_count=1)
    result['lag'] = pooled.lag_product/pooled.lag_square
    result[['lag_low','lag_high']] = np.nan
    # Resample whole seat histories. Endpoints from the same electorate are
    # related and a single large reversal can dominate a short replay.
    for model,group in totals.dropna(subset=['lag_product']).groupby('model'):
        data = group[['lag_product','lag_square']].to_numpy()
        indices = np.random.default_rng(20221126).integers(len(data),size=(2000,len(data)))
        sums = data[indices].sum(axis=1)
        usable = sums[:,1]>0
        if usable.any():
            result.loc[model,['lag_low','lag_high']] = np.quantile(sums[usable,0]/sums[usable,1],[.025,.975])
    result['jump'] *= 1000
    result['pairs'] = df.groupby('model').jump.count()
    result['triples'] = df.groupby('model').lag_product.count()
    result['districts'] = seats.groupby('model').lag_product.count()
    return result[['fp','brier','jump','lag','lag_low','lag_high','pairs','triples','districts','equal_seat_lag']]


def match_feed_series(df, reference_run, keep_intermediate=False):
    """Compare the very same XML deliveries in two runs. The simulator's
    snapshot code can refer to a different clock in an automated replay."""
    reference = sorted((ROOT/'live_runs'/reference_run).glob('snapshot_*.json'))
    feeds = [read(path)['run']['input_snapshot_file'] for path in reference]
    order = df[['step','feed']].drop_duplicates().sort_values('step')
    if len(feeds)<3 or len(set(feeds))!=len(feeds) or order.feed.isna().any() or order.feed.duplicated().any():
        raise ValueError('Matching needs at least three unique reference feeds and unique run feeds')
    matched = order[order.feed.isin(feeds)]
    if matched.feed.tolist()!=feeds:
        raise ValueError('Reference feeds are missing or appear in a different order')
    if keep_intermediate:
        return df[df.step.between(matched.step.iloc[0],matched.step.iloc[-1])].copy()
    selected = df[df.feed.isin(feeds)].copy()
    selected['step'] = selected.feed.map({feed:i for i,feed in enumerate(feeds)})
    return selected


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs',nargs='+')
    parser.add_argument('--output-prefix',help='Optionally save underlying district results for each run')
    parser.add_argument('--all-sources',action='store_true',help='Also test letting all reported vote categories supply the median')
    parser.add_argument('--include-safe',action='store_true',help='Include initially safe seats in revision and win scores')
    parser.add_argument('--min-move-pp',type=float,default=5.,help='Minimum preceding win-chance change in pp for the slope (default 5, use 0 for all)')
    parser.add_argument('--local-only',action='store_true',help='Also require changed local FP or TCP on the preceding move')
    parser.add_argument('--trigger-model',help='Use moves from this one model to select common seat-captures, e.g. replay')
    parser.add_argument('--match-feed-run',help='Score only the XML deliveries saved in this other run, in order')
    parser.add_argument('--keep-intermediate',action='store_true',help='Keep every capture between the first and last matched feeds')
    args = parser.parse_args()
    if args.keep_intermediate and not args.match_feed_run:
        parser.error('--keep-intermediate requires --match-feed-run')
    for run in args.runs:
        frame = capture_series(run,args.all_sources,args.include_safe)
        if args.match_feed_run:
            frame = match_feed_series(frame,args.match_feed_run,args.keep_intermediate)
        if args.output_prefix:
            frame.to_csv(f'{args.output_prefix}-{run}.csv',index=False)
        print(run,score_series(frame,args.min_move_pp,args.local_only,args.trigger_model).round(4).to_string(),sep='\n',flush=True)

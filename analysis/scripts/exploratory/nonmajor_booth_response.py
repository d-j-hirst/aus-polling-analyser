"""Assess the ordinary-booth prototype for competitive named non-major candidates.

Choose candidates using the opening forecast. Later official results are used
only to score them. Alongside final FP error, estimate how a change of FP mix
would affect the archived dominant matchup with its preferences and spread held
fixed. The latter is a sensitivity calculation, not a new simulation draw.
"""
import argparse
from collections import Counter
from pathlib import Path
import sys

import numpy as np
import pandas as pd

if not __package__:
    sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from scripts.exploratory.score_booth_fp_update import (
    ROOT, actual_vector, adjusted_shares, fit_shift, group_votes, official_results,
    opening_backgrounds, read, shared_background, snapshot,
)


MODELS = ('replay', 'strong', 'by_type', 'cautious')


def projection_inputs(places, base, parties):
    keys = [k for k,b in places.items() if sum(b['fp_votes_current'].values()) or b['expected_remaining_fp']]
    if any(k not in base for k in keys):
        raise ValueError('Missing opening booth')
    booths = [places[k] for k in keys]
    counts = np.array([group_votes(b['fp_votes_current'], parties, b.get('count_to_projection_ids', {}))
                       for b in booths])
    ordinary = np.array([b['booth_type']=='Normal' and b['vote_type']=='Ordinary' for b in booths])
    remaining = np.array([b['expected_remaining_fp'] for b in booths])
    return booths, np.array([base[k] for k in keys]), counts, ordinary, remaining


def outstanding_mixes(shapes, counts, ordinary, prior):
    """Learn from ordinary returns in both fits. The split rule applies the
    stronger fit to uncounted ordinary voters and the cautious fit elsewhere."""
    reported = ordinary & (counts.sum(axis=1)>0)
    fits = [adjusted_shares(shapes, fit_shift(shapes[reported], counts[reported], prior, strength, 500))
            for strength in (250,1000)]
    strong, cautious = fits
    blended = np.where(ordinary[:,None], strong, cautious)
    return dict(zip(MODELS[1:], (strong,blended,cautious)))


def projections(shapes, counts, ordinary, remaining, prior):
    total = counts.sum() + remaining.sum()
    return {label: 100*(counts.sum(axis=0) + remaining @ mix)/total
            for label,mix in outstanding_mixes(shapes,counts,ordinary,prior).items()}


def dominant_pair_summary(report, i, pid, seat, booths, counts, total, parties, forecasts, bound_tails=False):
    """Attribute only still-unallocated ballots to the implied preference pool.

    In a booth with both counts, set aside the measured final-pair votes and
    the contributing FP ballots. If their totals differ, match the smaller
    total pro rata. Keep the observed share fixed in every alternative. A
    common transfer rate for remaining sources reproduces the archived median.
    This cannot reconstruct exclusions or preference differences between those
    sources. Use it only where this matchup dominates the archived draws.
    """
    pair, probability = max(seat['pairing_percent'].items(),key=lambda item:item[1], default=('',0))
    if probability<65 or pid not in pair.split('/') or not set(pair.split('/'))<=set(parties):
        return {}
    p,q = pair.split('/')
    opponent = q if pid==p else p
    j,k = parties.index(pid), parties.index(opponent)
    counted_tcp = matched = 0
    covered = np.zeros(len(parties)+1)
    for b,fp in zip(booths,counts):
        tcp = Counter()
        for raw,v in b['tcp_votes_current'].items():
            tcp[b.get('count_to_projection_ids',{}).get(raw,raw)] += v
        if {p,q} != set(tcp) or min(sum(tcp.values()),fp.sum())<=0:
            continue
        weight = min(sum(tcp.values()),fp.sum())
        counted_tcp += weight*tcp[pid]/sum(tcp.values())
        covered += weight*fp/fp.sum()
        matched += weight

    def pair_row(row):
        return str(row['party_a']['party_index'])+'/'+str(row['party_b']['party_index']) == pair
    bands = next(r['values'] for r in report['seat_tcp_probability_band'][i] if pair_row(r))
    conditional = next(r['value'] for r in report['seat_tcp_win_percent'][i] if pair_row(r))
    levels = np.array(report['probability_bands'])
    bands = np.array(bands)
    if pid!=p:
        levels, bands, conditional = 100-levels[::-1], 100-bands[::-1], 100-conditional
    median = bands[np.argmin(abs(levels-50))]
    original = forecasts['replay']
    pool = 100 - original[j] - original[k] - 100*(matched-covered[j]-covered[k])/total
    if pool<=0:
        return {}
    rate = (median - original[j] - 100*(counted_tcp-covered[j])/total)/pool
    if not 0<=rate<=1:
        return {}
    # Use the archive's win frequency at 50 and interpolate inside its bands.
    # Tiny rounding differences at 50 can be omitted on the conflicting side.
    loss = 100-conditional
    keep = ((bands<50)&(levels<loss))|((bands>50)&(levels>loss))
    order = np.argsort(np.r_[bands[keep],50])
    x,y = np.r_[bands[keep],50][order],np.r_[levels[keep],loss][order]
    win = next((r['value'] for r in report['seat_party_win_percent'][i] if str(r['party_index'])==pid),0)
    outcome = dict(opponent=opponent,pair_pc=probability,measured=matched,
                   raw_tcp=100*counted_tcp/matched if matched else np.nan)
    for label,alt in forecasts.items():
        delta = alt-original
        move = delta[j]-rate*(delta[j]+delta[k])
        outcome['tcp_'+label] = median+move
        # Censor outside the saved percentile range. A central estimate can
        # still be given there, but the corresponding tail is poorly resolved.
        threshold = 50-move
        outcome['win_'+label] = (win+probability/100*(loss-np.interp(threshold,x,y))
                                  if bound_tails or x[0]<=threshold<=x[-1] else np.nan)
    return outcome


def evaluate(run, focused=None, shared=False):
    paths = sorted((ROOT/'live_runs'/run).glob('snapshot_*.json'))
    opening, initial = snapshot(paths[0])
    truth = official_results(run.split('-')[0])
    backgrounds, labels = opening_backgrounds(opening, initial, truth, Counter())
    first_report = read(paths[0])['simulation_report']
    candidates = {}
    for i,seat in enumerate(first_report['seat_name']):
        if seat not in backgrounds or (focused and seat!=focused):
            continue
        candidates[seat] = [str(r['party_index']) for r in first_report['seat_party_win_percent'][i]
                            if str(r['party_index']) in backgrounds[seat][0]
                            and r['abbreviation'] in ('IND','GRN','ON') and 5<=r['value']<=95]
    rows = []
    for path in paths:
        time = path.name.split('_')[1]
        # Focused traces of older runs need only election night.
        if focused and time[:8]!=paths[0].name.split('_')[1][:8]:
            continue
        summary, places = snapshot(path)
        report = read(path)['simulation_report']
        for i,seat in enumerate(report['seat_name']):
            if not candidates.get(seat):
                continue
            parties,base,prior = backgrounds[seat]
            booths,shapes,counts,ordinary,remaining = projection_inputs(places[seat],base,parties)
            district = summary['seats'][seat]
            if shared and district['shared'] is not None and opening['seats'][seat]['shared'] is not None:
                prior,shapes = shared_background(prior,shapes,(opening['seats'][seat],district),parties)
            n = int((ordinary & (counts.sum(axis=1)>0)).sum())
            if not n and path!=paths[0]:
                continue
            estimates = projections(shapes,counts,ordinary,remaining,prior)
            estimates['replay'] = group_votes(district['fp_mean_percent'],parties,{})
            actual,final = actual_vector(truth[seat],parties,labels,opening['seats'][seat]['candidates'])
            for pid in candidates[seat]:
                j = parties.index(pid)
                if actual[j]==0:
                    continue  # An unaligned independent name cannot be scored as zero votes.
                win = next((r['value'] for r in report['seat_party_win_percent'][i]
                            if str(r['party_index'])==pid),0)
                outcomes = dominant_pair_summary(report,i,pid,district,booths,counts,counts.sum()+remaining.sum(),
                                                 parties,estimates)
                rows.append(dict(run=run,time=time,seat=seat,party=labels[pid],booths=n,
                                 coverage=report['seat_fp_completion'][i]*100,actual=actual[j],published_win=win,
                                 won=bool(final and max(final,key=lambda f:f[2])[0]==pid),
                                 **{label:values[j] for label,values in estimates.items()},**outcomes))
    return pd.DataFrame(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run')
    parser.add_argument('--seat')
    parser.add_argument('--shared', action='store_true', help='Include shared movements where archived')
    parser.add_argument('--output',help='Optional destination for the individual estimates')
    args = parser.parse_args()
    df = evaluate(args.run,args.seat,args.shared)
    if args.output:
        df.to_csv(args.output,index=False)
    if args.seat:
        print(df.round(2).to_string(index=False))
        return
    for stage,mask in [('1-4 ordinary booths',df.booths.between(1,4)),('all counted',df.booths>0)]:
        subset = df[mask].copy()
        for m in MODELS:
            subset[m] = (subset[m]-subset.actual).abs()
        seats = subset.groupby(['party','seat'])[list(MODELS)].mean()
        print(stage)
        print(seats.groupby('party').mean().assign(districts=seats.groupby('party').size()).round(2))


if __name__ == '__main__':
    main()

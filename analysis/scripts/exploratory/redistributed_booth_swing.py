"""Test whether swings in transferred ordinary booths predict their former neighbours.

Uses the 2025 federal replay and the AEC's stable polling-place IDs. Each
observation tests remaining ordinary booths from one named previous division
with one specific TCP pair. The final capture supplies scoring outcomes only.
"""
import argparse
from collections import Counter, defaultdict
import xml.etree.ElementTree as ET

import numpy as np
import pandas as pd

from score_booth_fp_update import ROOT, official_results, snapshot
from live_pipeline import booth_tcp
from lib.live_analysis_archive import load_json


def source_divisions(previous_feed, current_preload):
    """Recover the historical parent of each current booth, ignoring ambiguous IDs."""
    previous = defaultdict(set)
    current = defaultdict(set)
    for path, historic in ((previous_feed,True),(current_preload,False)):
        for contest in ET.parse(path).findall('.//{*}House/{*}Contests/{*}Contest'):
            seat = contest.findtext('{*}ContestIdentifier/{*}ContestName')
            for place in contest.findall('.//{*}PollingPlace/{*}PollingPlaceIdentifier'):
                ident = place.get('Id')
                if historic:
                    previous[ident].add(seat)
                else:
                    current[seat,place.get('Name')].update(previous.get(ident,()))
    return {key:next(iter(names)) for key,names in current.items() if len(names)==1}


def share(booth, field):
    ballots = {r['party_index']:r['value'] for r in booth['node'][field]}
    n = sum(ballots.values())
    return (tuple(sorted(ballots)), n, ballots.get(0,0)/n if n else None)


def capture_rows(path, sources):
    """Require an actual historical ALP–Coalition or ALP–Greens booth TCP.

    Keeping the exact partner ID prevents a Nationals history standing in for a
    Liberal count. A renamed district recognised by the simulator is retained
    territory. Unmatched premises, early centres and declarations sit outside
    this diagnostic: a postal batch cannot be assigned to one former division.
    """
    output = []
    for b in load_json(path)['booths']:
        key = b['seat_name'],b['name']
        if b['booth_type']!='Normal' or b['vote_type']!='Ordinary' or key not in sources:
            continue
        pair,old_n,old = share(b,'tcp_votes_previous')
        if not old_n or pair not in ((0,1),(0,2),(0,10)):
            continue
        current_pair,n,value = share(b,'tcp_votes_current')
        # Classic booths often export their outstanding projection in the TPP
        # account. Use projected formal ballots to identify an active venue
        # when the matching named TCP projection is unavailable.
        projected_pair,turnout,_ = share(b,'tcp_votes_projected')
        if not turnout or projected_pair!=pair:
            turnout = share(b,'fp_votes_projected')[1]
        output.append(dict(seat=key[0],booth=key[1],source=key[0] if b['same_seat'] else sources[key],
                           pair=str(pair),prior=old, n=n if current_pair==pair else 0,
                           other_count=n>0 and current_pair!=pair, observed=value if current_pair==pair else None,
                           remaining=turnout))
    return pd.DataFrame(output)


def forecast_locations(seat,pair,summary,booths,opening):
    """Allocate the published median to booths, preserving any counted preferences."""
    p,q = map(str,pair.strip('()').split(', '))
    key = p+'/'+q
    result = summary['seats'][seat]
    if result['pairing_percent'].get(key,0)<65:
        return {}, 0
    target = result['tcp_percentiles'][key]['50']
    # For classic seats the commentary benchmark can use TPP only if there
    # is a single Coalition partner on the ballot.
    coalition = set(result['fp_mean_percent']) & {'1','10'}
    local = booths[seat]
    fitted = booth_tcp.fit(local,opening[seat],[p,q],target,coalition,{})
    out = {}
    for k,v in fitted.items():
        n = sum(v.values())
        if n<=0 or local[k]['booth_type']!='Normal' or local[k]['vote_type']!='Ordinary':
            continue
        primaries = Counter()
        for raw, votes in local[k]['fp_votes_current'].items():
            primaries[local[k].get('count_to_projection_ids',{}).get(raw,raw)] += votes
        out[local[k]['name']] = (v[p]/n,n,primaries.get(p,0)/n,1-primaries.get(q,0)/n)
    return out, sum(sum(v.values()) for v in fitted.values())


def assess(frame, final, time, summary, booths, opening):
    rows = []
    for (seat,pair),district in frame.groupby(['seat','pair']):
        reported = district[district.n>0]
        if reported.empty:
            continue
        swing = np.average(reported.observed-reported.prior,weights=reported.n)
        bench = None
        for source,area in district[district.source!=seat].groupby('source'):
            counted = area[area.n>0]
            pending = area[(area.n==0)&~area.other_count&(area.remaining>0)]
            if counted.empty or pending.empty:
                continue
            local = np.average(counted.observed-counted.prior,weights=counted.n)
            # One large booth counts as at most one independent location. A
            # small batch contributes less. Both constants are trial settings.
            locations = sum(counted.n/(counted.n+250))
            adaptive = locations/(1+locations)
            # Set the predictions using this capture alone. The adjustment is
            # the difference between swings, so the shared seat swing enters once.
            pending = pending.copy()
            for fraction in (0,.25,.5,1):
                pending[str(fraction)] = (pending.prior+swing+fraction*(local-swing)).clip(0,1)
            pending['adaptive'] = (pending.prior+swing+adaptive*(local-swing)).clip(0,1)
            if bench is None:
                bench, district_turnout = forecast_locations(seat,pair,summary,booths,opening)
            existing_swing = np.nan
            guarded_change = fixed_share = np.nan
            if all(b in bench for b in pending.booth):
                projected,n,lower,upper = np.array([bench[b] for b in pending.booth]).T
                existing_swing = np.average(projected-pending.prior,weights=n)
                pending['published'] = projected
                # Correct the movement still missing in the outstanding area,
                # preserving differences the forecast already sees between
                # booths and FP ballots already held by either finalist.
                adjustment = adaptive*(local-existing_swing)
                pending['overlay'] = np.clip(projected+adjustment,lower,upper)
                whole_change = 100*np.dot(pending.overlay-projected,n)/district_turnout
                # Alternative for an FP-only booth: both finalists' counted
                # primary voters are already allocated. Apply the source-area
                # change to the fraction of ballots still assignable. With
                # no primaries yet, the two alternatives are identical.
                assignable = upper-lower
                pending['guarded'] = np.clip(projected+adjustment*assignable,lower,upper)
                guarded_change = 100*np.dot(pending.guarded-projected,n)/district_turnout
                fixed_share = 100*np.average(1-assignable,weights=n)
            else:
                whole_change = np.nan
            scored = pending.merge(final,on=['seat','booth','pair'])
            results = {}
            for model in ['0','0.25','0.5','1','adaptive','published','overlay','guarded']:
                if model in scored and not scored.empty:
                    error = scored[model]-scored.final_share
                    results[model] = 100*np.average(abs(error),weights=scored.final_n)
            rows.append(dict(time=time,seat=seat,pair=pair,source=source,in_booths=len(counted),
                             pending=len(pending),scored=len(scored),swing=100*swing,local=100*local,
                             implied=100*existing_swing,
                             truth=100*np.average(scored.final_share-scored.prior,weights=scored.final_n) if len(scored) else np.nan,
                             area_remaining=pending.remaining.sum(),adaptive_weight=adaptive,
                             seat_alp_change=whole_change,guarded_change=guarded_change,fixed_share=fixed_share,**results))
    return rows


def replay_results(run):
    sources = source_divisions(ROOT/'downloads/aec-mediafeed-results-detailed-light-27966.xml',
                               ROOT/'downloads/2025fed_preload.xml')
    files = sorted((ROOT/'live_runs'/run).glob('snapshot_*.analysis.json.gz'))
    def diagnostic(path):
        return path.parent/path.name.replace('.analysis.json.gz','.json')
    _,opening = snapshot(diagnostic(files[0]))
    final_path = max((ROOT/'live_runs/2025fed-preferences7').glob('snapshot_*.analysis.json.gz'))
    final = capture_rows(final_path,sources)
    final = final.loc[final.n>0,['seat','booth','pair','n','observed']].rename(
        columns={'n':'final_n','observed':'final_share'})
    rows, districts = [], []
    official = official_results('2025fed')
    for path in files:
        time = path.name.split('_')[1]
        if time[:8]!=files[0].name.split('_')[1][:8]:
            continue
        summary,booths = snapshot(diagnostic(path))
        adjustments = assess(capture_rows(path,sources), final, time, summary, booths, opening)
        rows.extend(adjustments)
        if not adjustments:
            continue
        # Add the changes from disjoint source areas before scoring the whole
        # seat. No final booth count is required for an area to contribute.
        for (seat,pair), group in pd.DataFrame(adjustments).groupby(['seat','pair']):
            changes = group.seat_alp_change.dropna()
            if changes.empty:
                continue
            labels = {1:'LIB', 2:'GRN', 10:'NAT'}
            opponent = int(pair.strip('()').split(', ')[1])
            ballots = {label:n for _,label,n in official[seat]['tcp']}
            if set(ballots)!={'ALP',labels[opponent]}:
                continue
            actual = 100*ballots['ALP']/sum(ballots.values())
            old = summary['seats'][seat]['tcp_percentiles'][f'0/{opponent}']['50']
            new = old + changes.sum()
            guarded = old + group.loc[changes.index,'guarded_change'].sum()
            districts.append(dict(seat=seat,time=time,pair=pair,sources='/'.join(group.loc[changes.index,'source']),
                                  old=old,new=new,actual=actual,error_old=abs(old-actual),
                                  error_new=abs(new-actual),worsening=abs(new-actual)-abs(old-actual),
                                  guarded=guarded,error_guarded=abs(guarded-actual)))
    return pd.DataFrame(rows), pd.DataFrame(districts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', default='2025fed-preferences7')
    parser.add_argument('--output-prefix', help='Optional prefix for the area and district CSV files')
    args = parser.parse_args()
    df, districts = replay_results(args.run)
    if args.output_prefix:
        df.to_csv(args.output_prefix+'-areas.csv',index=False)
        districts.to_csv(args.output_prefix+'-districts.csv',index=False)
    groups = ['seat','source','pair']
    metrics = ['0','0.25','0.5','1','adaptive']
    for label,subset in [('all',df),('outside Wills',df[df.seat!='Wills']),
                         ('one booth',df[df.in_booths==1]),('several booths',df[df.in_booths>1])]:
        subset = subset[subset.scored>0]
        areas = subset.groupby(groups)[metrics].mean()
        print(label,len(areas),'areas',len(subset),'area-captures',areas.mean().round(2).to_dict())
    comparisons = df.dropna(subset=['published','overlay'])
    for label,subset in [('all',comparisons),('outside Wills',comparisons[comparisons.seat!='Wills'])]:
        areas = subset.groupby(groups)[['published','overlay','guarded']].mean()
        print('Forecast comparison',label,len(areas),'areas',len(subset),'area-captures',
              areas.mean().round(2).to_dict(), 'improved areas',sum(areas.overlay<areas.published))
    areas = comparisons.groupby(groups)[['published','overlay']].mean()
    print('Largest average area regressions')
    print(areas.assign(worsening=areas.overlay-areas.published).nlargest(8,'worsening').round(2).to_string())
    seats = districts.groupby('seat')[['error_old','error_new','error_guarded','worsening']].mean()
    print('District TCP',len(seats),'seats',len(districts),'captures',seats.mean().round(3).to_dict(),
          'improved',sum(seats.worsening<0))
    print(districts.nlargest(8,'worsening').round(2).to_string(index=False))


if __name__=='__main__':
    main()

"""Test how pre-election preference evidence changes Victorian district estimates.

Run from the repository root with the analysis Python and --help. The inputs
are the same saved zero-count 2022vic replay, VEC nominations and VEC result
workbooks as vic_district_preference_history, plus the project's federal
preference catalogue, 2022 booth crosswalk and 2016–22 federal polling places.
Predictions are built before opening the Victorian scoring results. Percentages
inside the calculations are fractions of formal votes or of one donor's FP.
This trial compares district centres and does not run the simulation.
"""

import argparse
import csv
import gzip
import json
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import mean

from scipy.optimize import minimize

if __package__:
    from . import vic_district_preference_history as history
else:
    import vic_district_preference_history as history

ROOT = history.ROOT
YEARS = (2022, 2019, 2016)
DONORS = {'GRN': 'Greens', 'VNS': 'Victorian Socialists', 'AJP': 'Animal Justice',
          'REAS': 'Reason', 'LDP': 'Liberal Democrats', 'ON': 'One Nation', 'DHJP': 'Justice',
          'DLP': 'Democratic Labour', 'SPP': 'Sustainable Australia', 'FFP': 'Family First',
          'ASP': 'Shooters, Fishers, Farmers'}
FP_KEYS = {'0': 'Labor', '1': 'Liberal', '2': 'Greens', '3': 'IND principal', '4': 'National'}


def federal_evidence():
    """Select published, original FP-to-finalist counts. A later publication of
    a non-classic district's alternative TPP is ineligible even if the ballots
    were cast before election day. A 2019 classic-only Greens relationship
    avoids using that retrospective source indirectly through a fitted slope.
    """
    with gzip.open(ROOT/'analysis/Preferences/evidence.json.gz', 'rt', encoding='utf8') as f:
        contests = json.load(f)['contests']
    records = {}
    training = defaultdict(list)
    for c in contests:
        if c['election'] not in {f'{y}fed' for y in YEARS} or c['election_type'] != 'general':
            continue
        year = int(c['election'][:4])
        candidates = {p['id']: p for p in c['candidates']}
        for obs in c['original_source_observations']:
            if obs['status'] != 'accepted' or f'aec-{year}-tcp-flow.csv' not in obs['source_refs']:
                continue
            ends = {candidates[d['candidate']]['party_group']: d['votes'] for d in obs['destinations']}
            if set(ends) not in ({'ALP', 'LIB'}, {'ALP', 'NAT'}):
                continue
            cand = candidates[obs['source_candidate']]
            party = DONORS.get(cand['party_group'])
            if not party:
                continue
            n = obs['denominator_votes']
            assert n == sum(ends.values()) and not obs['exhausted_votes']
            odds = history.log_odds((ends['ALP']+.5)/(n+1))
            strength = history.log_odds(cand['fp_fraction'])
            if year == 2019 and party == 'Greens':
                training[c['state']].append((strength, odds))
            if c['state'] == 'VIC':
                opponent = 'Liberal' if 'LIB' in ends else 'National'
                records[year, c['seat'].casefold(), opponent, party] = (n, odds, strength)
    # Equal districts, fitting the slope within states so a difference between
    # states' overall preference levels cannot stand in for local Greens strength.
    xy = xx = 0.
    for points in training.values():
        mx, my = map(mean, zip(*points))
        xy += sum((x-mx)*(y-my) for x,y in points)
        xx += sum((x-mx)**2 for x,_ in points)
    slope = xy/xx
    # Greens geography enters as a departure from the federal relationship,
    # since the state forecast already adjusts for the district's Greens FP.
    green_base = defaultdict(list)
    for (year, _, opp, party), (_, odds, strength) in records.items():
        if party == 'Greens':
            green_base[year, opp].append(odds-slope*strength)
    return records, slope, {k: mean(v) for k,v in green_base.items()}


def aec_csv(path):
    with path.open(encoding='utf-8-sig', newline='') as f:
        if 'FinalResults' not in next(f):
            raise ValueError(f'Expected a final federal publication in {path}')
        yield from csv.DictReader(f)


def booth_geography():
    """The crosswalk assigns 2022 federal election-day booths to state seats.
    Use the number voting there for geographical weights. Trace older sites by
    polling-place ID and postcode, allowing the division to change. An ambiguous
    or missing older site supplies no evidence for that part of the district.
    """
    base = ROOT/'downloads/turnout/federal-prepoll'
    old_divisions = defaultdict(set)
    for year in YEARS:
        folder = base/f'{year}fed'
        places = list(aec_csv(next(folder.rglob('General*.csv'))))
        sites = {(p['DivisionID'],p['PollingPlaceID']): p for p in places
                 if p['State']=='VIC' and p['PollingPlaceTypeID']=='1'}
        for (_, pid), p in sites.items():
            old_divisions[year, pid, p['PremisesPostCode']].add(p['DivisionNm'].casefold())
        if year == 2022:
            current = defaultdict(Counter)
            for r in aec_csv(next(folder.rglob('*-VIC.csv'))):
                site = sites.get((r['DivisionID'],r['PollingPlaceID']))
                if site and r['PartyNm'] != 'Informal':
                    key = (r['DivisionNm'].casefold(),r['PollingPlace'].casefold())
                    current[key][(r['PollingPlaceID'],site['PremisesPostCode'])] += int(r['OrdinaryVotes'])
    mapping = defaultdict(set)
    for line in (ROOT/'analysis/Federal-State/booths-2022vic.txt').read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('#'):
            seat = line[1:].strip()
        elif line.strip():
            mapping[seat].add(tuple(token.strip().casefold() for token in line.split(',',1)))
    result = defaultdict(Counter)
    for seat, matches in mapping.items():
        for division, name in matches:
            for (pid, postcode), votes in current.get((division,name),{}).items():
                areas = [division]
                for year in YEARS[1:]:
                    choices = old_divisions.get((year,pid,postcode),set())
                    areas.append(next(iter(choices)) if len(choices)==1 else None)
                result[seat][tuple(areas)] += votes
    return result


def local_differences(areas, opponent, records, slope, green_base):
    """For each source, borrow the newest same-party, same-opponent observation
    for each mapped area. Shrink small samples using 500 prior ballots, reduce
    older elections' influence by one third per cycle and leave uncovered areas
    at the starting rate. These discounts are trial assumptions. Zero local
    strength therefore exactly reproduces the statewide donor settings.
    """
    shifts = Counter()
    total_votes = sum(areas.values())
    for divisions, votes in areas.items():
        for party in DONORS.values():
            for age, (year, division) in enumerate(zip(YEARS, divisions)):
                r = records.get((year, division, opponent, party))
                if not r:
                    continue
                n, odds, strength = r
                if party == 'Greens':
                    diff = odds - slope*strength - green_base[year,opponent]
                else:
                    diff = odds - history.log_odds(history.NAMED[party])
                shifts[party] += votes/total_votes * (1-age/3) * n/(n+500)*diff
                break
    return shifts


@dataclass
class SeatModel:
    starting: dict
    tpp: float
    prob: float
    scales: dict
    context: tuple
    past: object
    pair_centre: object


def seat_models(replay, nominations, previous_tpp_file):
    before = json.loads(sorted(replay.glob('snapshot_*__run_*.json'))[0].read_bytes())['simulation_report']
    assert not any(before['seat_fp_completion']) and not any(before['seat_tpp_completion'])
    origin = json.loads((replay/'packages/origin/summary.json').read_bytes())['seats']
    roster = {s:r['candidates']['3']['name'].casefold() for s,r in origin.items() if r['candidates'].get('3',{}).get('name')}
    ballots = history.nominated_fields(nominations, roster)
    settings = history.read_seats()
    old = history.old_fields()
    previous_tpp = history.tpp_results(previous_tpp_file, previous=True)
    reference = mean(history.log_odds(fp['Greens']) for fp,_,classic in old.values() if classic and 'Greens' in fp)
    models = {}
    for i,s in enumerate(before['seat_name']):
        if s == 'Narracan':  # its poll was postponed following a candidate's death
            continue
        r, cfg = origin[s], settings[s]
        prior = (before['seat_party_one_margin_average'][i]+50)/100
        fp = history.normalized({FP_KEYS.get(k,k):v for k,v in r['fp_mean_percent'].items()})
        field = history.precount_field(fp, ballots[s])
        predecessor = cfg.get('sPreviousName',s)
        past = None
        if 'sUseFpResults' not in cfg and predecessor in old and predecessor in previous_tpp:
            past_fp, past_opponent, _ = old[predecessor]
            past = (past_fp, past_opponent, previous_tpp[predecessor][0])
        scales = {}
        # Convert the archived middle half of each forecast distribution to
        # an approximate spread (a normal distribution's IQR is 1.349 SDs).
        # A 0.5-point floor keeps a rounded or very narrow band usable.
        for key in ('0','1','2','4'):
            if key in r['fp_percentiles']:
                band = r['fp_percentiles'][key]
                scales[FP_KEYS[key]] = max(.005, (band['75']-band['25'])/134.9)
        context = (prior,float(cfg['fTppMargin']),float(cfg.get('fPreviousTppSwing',0)),reference)
        pair_centre = r['tcp_percentiles'].get('0/2',{}).get('50')
        models[s] = SeatModel(field,prior,r['pairing_percent'].get('0/2',0)/100,scales,context,past,pair_centre)
    return models, roster


def preference_account(seat, opponent, shifts, slope, local_weight, historical_weight=.5):
    """Apply the same source rates when reconstructing 2018 as for the new
    field. Carry half the remaining historical discrepancy on log odds. This
    avoids adding a local tendency twice where it also explains the old count.
    """
    def raw(fp):
        rates = history.rates_for(fp,opponent,(False,True,False),seat.context)
        for p in fp:
            if p == 'Greens':
                rates[p] = history.shift(history.GREEN,slope*(history.log_odds(fp[p])-seat.context[-1]))
            if p in shifts:
                rates[p] = history.shift(rates[p],local_weight*shifts[p])
        return rates
    offset = 0.
    if seat.past and seat.past[1] == opponent:
        old_fp, _, target = seat.past
        fitted = history.historical_offset(old_fp,raw(old_fp),target)
        if fitted is None:
            raise ValueError('Unreachable historical TPP')
        offset = fitted*historical_weight
    def effective(fp):
        return {p:(r if p in history.COALITION|{'Labor'} else history.shift(r,offset))
                for p,r in raw(fp).items()}
    return effective


def pairing_weight(probability, threshold):
    """Fraction of the adjustment to use at this final-pair probability.
    Below the trial threshold the pair supplies no constraint. Above it each
    additional probability point counts equally, reaching full weight at certainty.
    """
    if not 0 <= threshold < 1 or not 0 <= probability <= 1:
        raise ValueError('Invalid pairing probability or taper threshold')
    return max(0., (probability-threshold)/(1.-threshold))


def reconcile(seat, opponent, account, pairing, q, tpp_scale, pair_scale, threshold=.5):
    """Move the three primaries as little as their saved spreads permit while
    reconciling their implied Labor TPP with the saved estimate. The TPP can
    move too. The 3-point default scales control willingness to revise expected
    TPP and the expected ALP–Greens margin (measured as Labor's TCP share).
    They are sensitivity settings, not independently observed standard errors.

    When the Labor–Greens probability exceeds the trial threshold, allow the
    Greens primary to participate progressively. Adding one Liberal FP point
    then adds q points to Labor's vote against the Greens via preferences.
    Penalising the net change in that matchup preserves some information from
    the original ranking count. No conditional median is treated as an
    unconditional statewide TPP or as an independent observation.
    """
    start = seat.starting
    credibility = pairing_weight(seat.prob, threshold) if opponent=='Liberal' and pairing!='fixed' else 0.
    def evaluate(x):
        # Changes here are in percentage points for numerical accuracy.
        da,dg = x[0]/100, x[1]/100
        dl = -da-dg
        fp = {**start, 'Labor':start['Labor']+da,opponent:start[opponent]+dl,'Greens':start['Greens']+dg}
        tpp = history.estimate(fp,account(fp))
        cost = (da/seat.scales['Labor'])**2+(dl/seat.scales[opponent])**2+((tpp-seat.tpp)/tpp_scale)**2
        if credibility:
            cost += (dg/seat.scales['Greens'])**2/credibility
            if pairing=='soft':
                cost += credibility*((da+q*dl)/pair_scale)**2
        return cost,fp,tpp
    bounds = [(-100*start['Labor'],100),(-100*start['Greens'],100) if credibility else (0,0)]
    sol = minimize(lambda x:evaluate(x)[0], [0.,0.], method='SLSQP', bounds=bounds,
                   constraints=[dict(type='ineq',fun=lambda x:100*start[opponent]-sum(x))],
                   options={'ftol':1e-11,'maxiter':100})
    if not sol.success:
        raise ValueError(sol.message)
    _, fp, tpp = evaluate(sol.x)
    assert min(fp.values())>=-1e-8 and abs(sum(fp.values())-1)<1e-7
    return fp,tpp


def scenarios(models, records, slope, green_base, geography):
    # The original Victorian statewide HTV prior predates voting. Test it at
    # its configured centre; final state-election preferences never set q.
    cfg = json.loads((ROOT/'analysis/Preferences/2022vic.json').read_bytes())
    q = cfg['profiles']['liberal_greens_first']['primary_points'][0]['means'][0]
    trials = [('fixed',0,'fixed'),('flexible',0,'free'),('soft',0,'soft'),
              ('local',.75,'fixed'),('combined',.75,'soft'),('half_local',.5,'soft'),
              ('all_local',1,'soft'),('looser_pair',.75,'soft'),('tighter_pair',.75,'soft'),
              ('looser_tpp',.75,'soft'),('tighter_tpp',.75,'soft'),
              ('more_2018_history',.75,'soft'),('full_2018_history',.75,'soft')]
    predictions = {}
    for seat,m in models.items():
        opponents = history.COALITION & m.starting.keys()
        for opp in opponents:
            shifts = local_differences(geography[seat],opp,records,slope,green_base)
            for label,w,pair in trials:
                historical_weight = {'more_2018_history':.75,'full_2018_history':1.}.get(label,.5)
                account = preference_account(m,opp,shifts,slope,w,historical_weight)
                tpp_scale = {'looser_tpp':.05,'tighter_tpp':.02}.get(label,.03)
                pair_scale = {'looser_pair':.06,'tighter_pair':.015}.get(label,.03)
                if len(opponents)==1:
                    fp,tpp = reconcile(m,opp,account,pair,q,tpp_scale,pair_scale)
                else:
                    # One saved TPP cannot identify the starting level of both
                    # matchups. Leave that seat forecast alone. Freeze a separate
                    # set of donor rates for each opponent for the flow-only check.
                    fp,tpp = m.starting,m.tpp
                pair_move = fp['Labor']-m.starting['Labor'] + q*(fp.get('Liberal',0)-m.starting.get('Liberal',0))
                predictions[seat,opp,label] = (fp,tpp,account(m.starting),pair_move)
    return predictions


def score(models, roster, predictions, primary_file, outcome_file):
    """Only here open the 2022 state counts. The flow-only score multiplies
    frozen pre-election rates by actual primary votes to isolate conversion
    from primary prediction error. These counts never feed back into estimates.
    """
    counts = history.counted_primaries(primary_file, roster)
    outcomes = history.tpp_results(outcome_file)
    with (ROOT/'analysis/Archived/elections/2022vic_results.pkl').open('rb') as f:
        finals = {s.name:{c.party:c.votes for c in s.tcp} for s in history._ElectionCacheUnpickler(f).load().results}
    rows = []
    for (s,opp,label),(fp,tpp,rates,pair_move) in predictions.items():
        observed,formal,actual_opponent = outcomes[s]
        if opp != actual_opponent:
            continue
        field = history.normalized(counts[s])
        # A few workbook labels differ from nominations (including New Democrats
        # recorded as independent). Both use the district's unknown-party rate.
        unknown = next((rates[p] for p in ('Independent','New Democrats','Freedom Party of Victoria','Emerging')
                        if p in rates),None)
        weighted = 0.
        for p,n in field.items():
            k = 'IND principal' if p.startswith('IND ') else {'Freedom Party':'Freedom Party of Victoria'}.get(p,p)
            if k not in rates and unknown is None:
                raise ValueError(f'{s}: no pre-election rate for {k}')
            weighted += n*(rates[k] if k in rates else unknown)
        row = dict(seat=s,opponent=opp,method=label,formal=formal,observed_tpp=100*observed,
                   tpp=100*tpp,archive_tpp=100*models[s].tpp,flow_only=100*weighted,
                   pair_move=100*pair_move,prob=100*models[s].prob,
                   single_opponent=len(history.COALITION & models[s].starting.keys())==1,
                   observed_alp_vs_green=(100*finals[s]['Labor']/sum(finals[s].values())
                                          if set(finals[s])=={'Labor','Greens'} else None),
                   saved_alp_vs_green=models[s].pair_centre)
        for p in ('Labor',opp,'Greens'):
            key = 'Coalition' if p==opp else p
            row[key] = 100*fp[p]
            row['actual_'+key] = 100*field[p]
            row['archive_'+key] = 100*models[s].starting[p]
        rows.append(row)
    assert len({r['seat'] for r in rows})==87
    return rows


def report(rows):
    print('Mean absolute error per district (pp). FP and pre-count TPP use single-opponent seats;')
    print('the final column weights frozen pre-election rates by observed FPs in all 87 districts.')
    print('Method: ALP FP, Coalition FP, GRN FP, ALP TPP, flow-only TPP')
    for label in ['archive', *dict.fromkeys(r['method'] for r in rows)]:
        archived = label == 'archive'
        selected = [r for r in rows if r['method'] == ('fixed' if archived else label)]
        single = [r for r in selected if r['single_opponent']]
        prefix = 'archive_' if archived else ''
        errors = [mean(abs(r[prefix+p]-r['actual_'+p]) for r in single) for p in ('Labor','Coalition','Greens')]
        errors.append(mean(abs(r[prefix+'tpp']-r['observed_tpp']) for r in single))
        if not archived:
            errors.append(mean(abs(r['flow_only']-r['observed_tpp']) for r in selected))
        print(label,*(round(n,3) for n in errors))
    print('Focal districts. ALP FP, Coalition FP, GRN FP, ALP TPP, flow-only TPP')
    for r in rows:
        if r['seat'] in ('Richmond','St Albans','Greenvale') and r['method'] in ('fixed','flexible','soft','combined'):
            print(r['seat'],r['method'],*(round(r[k],2) for k in ('Labor','Coalition','Greens','tpp','flow_only')))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replay',type=Path,default=ROOT/'live_runs/2022vic-8-initial')
    parser.add_argument('--nominations',type=Path,default=ROOT/'downloads/2022vic_candidates.xml')
    parser.add_argument('--previous-tpp',type=Path,default=ROOT/'tmp/vec-2018-tpp.xlsx')
    parser.add_argument('--primary',type=Path,default=ROOT/'tmp/vec-2022-final-replay-fp.xlsx')
    parser.add_argument('--outcome',type=Path,default=ROOT/'tmp/vec-2022-final-replay-tpp.xlsx')
    parser.add_argument('--csv',type=Path,help='Write district estimates and held-out outcomes in percentage points')
    args=parser.parse_args()
    records,slope,green_base=federal_evidence()
    geography=booth_geography()
    models,roster=seat_models(args.replay,args.nominations,args.previous_tpp)
    predictions=scenarios(models,records,slope,green_base,geography)
    rows=score(models,roster,predictions,args.primary,args.outcome)
    print(f'2019 public classic-only Greens strength slope {slope:.3f}')
    report(rows)
    if args.csv:
        with args.csv.open('w',newline='',encoding='utf8') as f:
            writer=csv.DictWriter(f,fieldnames=rows[0])
            writer.writeheader()
            writer.writerows(rows)


if __name__=='__main__':
    main()

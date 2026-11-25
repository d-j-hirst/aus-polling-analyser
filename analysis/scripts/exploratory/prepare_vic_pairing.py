"""Prepare pre-election preference evidence for the Victorian ALP–Greens pairing constraint.

From the repository root supply the VEC nominations XML for the forecast election
and the VEC all-district TPP workbook for the preceding general election. No
current state result or forecast is needed. The simulator estimates the chance
of the final pair separately for each run.
"""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path
from statistics import mean
import xml.etree.ElementTree as ET

if __package__:
    from . import vic_precount_pairing_trial as trial
else:
    import vic_precount_pairing_trial as trial

h = trial.history


def seat_settings(term):
    seats = {}
    for line in (h.ROOT / 'analysis/seats' / (term+'.txt')).read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('#'):
            current = seats.setdefault(line[1:].strip(), {})
        elif '=' in line:
            key, value = line.split('=', 1)
            current[key] = value
    return seats


def party_fields(year):
    """Use original affiliations in the historical FP account. The strongest
    independent represents the principal independent in this reconstruction."""
    with (h.ROOT / f'analysis/Archived/elections/{year}vic_results.pkl').open('rb') as f:
        historical = h._ElectionCacheUnpickler(f).load().results
    fields, strengths = {}, []
    for seat in historical:
        principal = max((c for c in seat.fp if c.party=='Independent'), key=lambda c:c.votes, default=None)
        counts = Counter()
        for candidate in seat.fp:
            party = h.VEC_NAMES.get(candidate.party,candidate.party)
            counts['IND principal' if candidate is principal else party] += candidate.votes
        field = h.normalized(counts)
        fields[seat.name] = field
        pair = {c.party for c in seat.tcp}
        if 'Labor' in pair and pair & h.COALITION and 'Greens' in field:
            strengths.append(h.log_odds(field['Greens']))
    return fields, mean(strengths)


def prepare(term, nominations, previous_tpp):
    event = ET.parse(nominations).findtext('.//{*}EventIdentifier/{*}EventName')
    if event != f'State Election {term[:4]}':
        raise ValueError(f'Expected {term} nominations, found {event!r}')
    year = int(term[:4])-4
    fields, reference = party_fields(year)
    targets = h.tpp_results(previous_tpp, previous=year==2018)
    settings = seat_settings(term)
    roster = {s:name.casefold() for s,cfg in settings.items()
              for entry in cfg.get('sCandidateNames','').split(';') if ',' in entry
              for name,code in [entry.rsplit(',',1)] if code=='IND'}
    ballots = h.nominated_fields(nominations,roster)
    if set(ballots)!=set(settings):
        raise ValueError('Nomination districts do not match the seat file')
    _, slope, _ = trial.federal_evidence()  # 2019 classic federal divisions only for this coefficient.
    params = dict(h.NAMED)
    # Reconstruct each previous election using the statewide source rates
    # for that year. A changed 2026 ON preference assumption should affect
    # the 2026 forecast, not the reconstruction of a 2022 district count.
    with (h.ROOT/'analysis/Data/preference-estimates.csv').open(encoding='utf-8-sig', newline='') as stream:
        rates_by_year = {(row[0], row[2]): float(row[3])/100 for row in csv.reader(stream)
                         if len(row) >= 4 and row[1] == 'vic' and row[2] in {'ONP FP', 'GRN FP', 'OTH FP'}}
    params['One Nation'] = rates_by_year[term[:4], 'ONP FP']
    old_params = {**params, 'One Nation': rates_by_year[str(year), 'ONP FP']}
    green_rate = rates_by_year[term[:4], 'GRN FP']
    other_rate = rates_by_year[term[:4], 'OTH FP']
    rows = {}
    aliases = {'Australian Greens Victoria':'Greens','One Nation Victoria':'One Nation',
               'Libertarian Party (formerly Liberal Democrats)':'Liberal Democrats'}
    for name, ballot in ballots.items():
        ballot = {aliases.get(p,p):n for p,n in ballot.items()}
        if not {'Labor','Liberal','Greens'}<=ballot.keys() or 'National' in ballot:
            continue
        cfg = settings[name]
        pred = cfg.get('sPreviousName',name)
        margin = float(cfg['fTppMargin'])
        offset = 0.
        if ('sUseFpResults' not in cfg and pred in fields and pred in targets
                and targets[pred][2]=='Liberal' and 'Liberal' in fields[pred]):
            old = fields[pred]
            rates = h.rates_for(old,'Liberal',(False,True,False),
                                (targets[pred][0],margin,float(cfg.get('fPreviousTppSwing',0)),reference))
            rates.update({p:old_params[p] for p in old if p in old_params})
            if 'Greens' in old:
                rates['Greens'] = h.shift(rates_by_year[str(year), 'GRN FP'], slope*(h.log_odds(old['Greens'])-reference))
            fitted = h.historical_offset(old,rates,targets[pred][0])
            if fitted is not None:
                offset = .5*fitted
        others = {p:n for p,n in ballot.items() if p not in {'Labor','Liberal','Greens','IND principal'}
                  and not (p=='One Nation' and term=='2026vic')}
        grouped = (sum(n*h.shift(params.get(p,other_rate),offset) for p,n in others.items())/sum(others.values())
                   if others else h.shift(other_rate,offset))
        rows[name] = dict(grouped_rate=round(grouped,8), history_log_odds=round(offset,8),
                          named_independent='IND principal' in ballot)
    return dict(election=term, previous_election=f'{year}vic', greens_strength_slope=slope,
                greens_reference_log_odds=reference, greens_rate=green_rate, other_rate=other_rate,
                one_nation_rate=params['One Nation'], districts=rows)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('term',choices=['2022vic','2026vic'])
    parser.add_argument('nominations',type=Path)
    parser.add_argument('previous_tpp',type=Path)
    args=parser.parse_args()
    data=prepare(args.term,args.nominations,args.previous_tpp)
    output=h.ROOT/'analysis/Preferences/pairing'/f'{args.term}.json'
    output.parent.mkdir(exist_ok=True)
    output.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf8')

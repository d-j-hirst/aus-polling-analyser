"""Measure voting-type differences that were observable before the 2026 SA count.

Each row is an electorate where One Nation stood. Its in-person result sets
its own reference level. This prevents a higher share of absent votes in a
strong electorate from being mistaken for an absent-voter effect. Historical
estimates and their spread use formal first preferences only.
"""
from collections import defaultdict
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import numpy as np
import pandas as pd
from scipy.optimize import brentq
from scipy.special import expit, logit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'analysis'))
from scripts.diagnostics.category_sources import fetch
from scripts.turnout.turnout_federal_prepoll import csv_rows
from scripts.turnout.turnout_qld import ELECTIONS, classify_booth
from scripts.turnout.turnout_sa_final import BOOTH_CATEGORIES

MODES = ('Absent', 'PrePoll', 'Postal')
REFERENCE = 'In-person'
EVENTS = {2016: 20499, 2019: 24310, 2022: 27966, 2025: 31496}
FINAL = ROOT / 'downloads/turnout/2026sa/version-912-b1bb7181a5f8'


def vote_row(seat, mode, on, formal):
    return dict(seat=seat, mode=mode, on=on, formal=formal)


def federal(year):
    """Read candidate and formal votes by division from the cached final AEC CSV."""
    event = EVENTS[year]
    url = f'https://results.aec.gov.au/{event}/Website/Downloads/HouseFirstPrefsByCandidateByVoteTypeDownload-{event}.csv'
    divisions = defaultdict(list)
    for r in csv_rows(fetch(url)):
        if r['PartyNm'] != 'Informal':
            divisions[r['StateAb'], r['DivisionNm']].append(r)
    states = defaultdict(list)
    for (state, seat), rows in divisions.items():
        candidates = [r for r in rows if r['PartyAb'] == 'ON']
        if not candidates:
            continue
        party, = candidates
        for col, mode in zip(('Ordinary', *MODES), (REFERENCE, *MODES)):
            field = col+'Votes'
            states[state].append(vote_row(seat, mode, int(party[field]), sum(int(r[field]) for r in rows)))
    return {s: pd.DataFrame(v) for s, v in states.items()}


def queensland(year):
    """Use ECQ's official primary count as a separate federal-to-state transfer test."""
    election = ELECTIONS[f'{year}qld']
    path = ROOT / 'downloads/turnout/qld' / election.archive_url.split('/')[-1]
    with ZipFile(path) as archive:
        doc = ET.fromstring(archive.read('publicResults.xml'))
    election_node, = [e for e in doc.findall('election') if e.get('electionName') == election.election_name]
    modes = dict(election_day_ordinary=REFERENCE, early_in_person=REFERENCE, absent='Absent',
                 declaration_early='PrePoll', postal='Postal')
    data = []
    for district in election_node.findall('districts/district'):
        seat = district.get('districtName')
        party = [c.get('ballotOrderNumber') for c in district.findall('candidates/candidate')
                 if c.get('partyCode') in ('One Nation', "Pauline Hanson's One Nation")]
        if not party:
            continue
        code, = party
        for booth in district.findall("countRound[@countName='Official First Preference Count']/booths/booth"):
            mode = modes.get(classify_booth(booth, 'current')[1])
            if mode is None:
                continue
            formal = int(booth.findtext('formalVotes'))
            candidates = booth.findall('primaryVoteResults/candidate')
            if sum(int(c.findtext('count')) for c in candidates) != formal:
                raise ValueError(f'Unreconciled ECQ results at {seat}/{booth.get("name")}')
            votes = sum(int(c.findtext('count')) for c in candidates if c.get('ballotOrderNumber') == code)
            data.append(vote_row(seat, mode, votes, formal))
    return pd.DataFrame(data).groupby(['seat', 'mode'], as_index=False).sum()


def reviewed_sa():
    """Reconcile ECSA's reviewed venue and batch results to its published state totals.

    Early votes cast in the home district are physical centres. Early votes
    cast away from home belong to the separate early absent account (PrePoll
    in the replay). Provisional declarations have a separate source section.
    Only evaluation reads these post-election outcomes.
    """
    static = json.loads((FINAL/'static.json').read_bytes())
    party = {d['districtName']: next(c['candidateId'] for c in d['candidates'] if c['partyId'] == 'ONP')
             for d in static['districts']}
    final = json.loads((FINAL/'results.json').read_bytes())
    modes = {'early_in_person': 'Early centre', 'election_day_ordinary': 'Ordinary', 'mobile_or_institution': 'Mobile'}
    declarations = {'Polling Day': 'Provisional', 'Early Voting': 'Early Provisional',
                    'Early Voting Declaration Votes': 'Early Provisional', 'Postal': 'Postal',
                    'Electoral Visitor & Mobile Polling': 'EVM', 'Telephone/Interstate/Overseas Declaration Votes': 'TIO'}
    data = []
    for d in final['districts']:
        seat = d['districtId']
        seat_rows = []
        for place in d['pollingPlaces']:
            mode = modes[BOOTH_CATEGORIES[place['pollingPlaceTypeIdName']]]
            counts = {c['candidateId']: c['formalVotes'] for c in place['pollingCandidates']}
            seat_rows.append(vote_row(seat, mode, counts[party[seat]], sum(counts.values())))
        for section, field, lookup in [('declarations', 'declarationType', declarations),
                ('absentOrdinary', 'absentOrdinaryType', {'Early Voting': 'PrePoll', 'Polling Day': 'Absent'})]:
            for batch in d[section]:
                mode = lookup[batch[field].split(' - ')[0]]
                counts = {c['candidateId']: c['votes'] for c in batch['candidateVotes']}
                if sum(counts.values()) != batch['formalVotes']:
                    raise ValueError(f'Unreconciled ECSA declaration at {seat}/{mode}')
                seat_rows.append(vote_row(seat, mode, counts[party[seat]], batch['formalVotes']))
        # ECSA also publishes ordinary and declaration totals by candidate.
        # Only its separate absent-ordinary batches must be added to that
        # summary. Reconcile the two readings within all 47 districts.
        totals = {c['candidateId']: c['ordinaryVotes'] + c['declarationVotes'] for c in d['candidates']}
        for batch in d['absentOrdinary']:
            for candidate in batch['candidateVotes']:
                totals[candidate['candidateId']] += candidate['votes']
        if (sum(r['on'] for r in seat_rows), sum(r['formal'] for r in seat_rows)) != (totals[party[seat]], sum(totals.values())):
            raise ValueError(f'ECSA locations and candidate summaries disagree in {seat}')
        data.extend(seat_rows)
    frame = pd.DataFrame(data).groupby(['seat', 'mode'], as_index=False).sum()
    if tuple(frame[['on','formal']].sum()) != (255264, 1115864) or frame.seat.nunique() != 47:
        raise ValueError('The ECSA voting-type breakdown differs from the reviewed House result.')
    return frame


def old_sa_combined():
    """The 2022 state history separates election-day booths and one pooled declaration account."""
    history = json.loads((ROOT/'analysis/Booth Results/2022sa.json').read_bytes())
    rows = []
    for seat, d in history.items():
        candidates = [i for i, c in d['candidates'].items() if c['party'] == 'ONP']
        if not candidates:
            continue
        party, = candidates
        for name, place in d['booths'].items():
            votes = place['fp']
            rows.append(vote_row(seat, 'Combined' if name == 'Declaration Votes' else REFERENCE,
                                 votes.get(party, 0), sum(votes.values())))
    return pd.DataFrame(rows).groupby(['seat','mode'], as_index=False).sum()


def contrast(frame, mode):
    """Fit one change in log odds across electorates, controlling for each electorate's own level.

    A change of zero predicts exactly the same share in the category as among
    in-person voters in that seat. The fitted change matches the party's total
    category ballots at the actual category volumes. Dispersion is measured
    across seats, each counted once. A half ballot keeps observed zeros finite.
    """
    def select(name):
        return frame[frame['mode'] == name].set_index('seat')[['formal','on']]
    seats = select(mode).join(select(REFERENCE), rsuffix='_ref', how='inner')
    seats = seats[(seats.formal > 0) & (seats.formal_ref > 0)]
    base = logit((seats.on_ref.to_numpy()+.5)/(seats.formal_ref.to_numpy()+1))
    n, a = seats.formal.to_numpy(), seats.on.to_numpy()
    shift = brentq(lambda d: n @ expit(base+d) - a.sum(), -25, 25)
    local = logit((a+.5)/(n+1))-base
    return dict(seats=len(n), shift=shift, dispersion=np.std(local, ddof=1) if len(n)>1 else np.nan)


def score_transfer(frame, prior, year, state):
    """Test how far a federal pattern transfers when category sizes and in-person results are given.

    Final volumes and the in-person shares are supplied equally to every
    method. This isolates voting-type composition in pre-SA Queensland tests
    and the later SA comparison. Error is predicted minus observed One Nation
    percentage across *only* the three large absent and postal groups.
    """
    formal = frame.pivot(index='seat',columns='mode',values='formal').fillna(0)
    on = frame.pivot(index='seat',columns='mode',values='on').fillna(0)
    base = logit((on[REFERENCE]+.5)/(formal[REFERENCE]+1)).to_numpy()[:,None]
    n = formal[list(MODES)].to_numpy()
    offsets = prior.set_index('mode').loc[list(MODES),'shift'].to_numpy()
    actual = 100*on[list(MODES)].sum().sum()/n.sum()
    row = dict(year=year, state=state, districts=len(n), observed_pct=actual)
    for name, strength in [('neutral',0),('half',.5),('full',1)]:
        row[name+'_error_pp'] = 100*(n*expit(base+strength*offsets)).sum()/n.sum()-actual
    return row


def historical_evidence():
    """Build the SA prior and an independent estimate of how much voting modes vary together.

    For the spread, compare successive federal elections in the same state
    when the party stood in at least five divisions in each. This retains
    statewide errors shared across seats and the association between the three
    voting types. The two Queensland state elections separately test whether a
    pattern can transfer between tiers of government. Neither uses SA 2026.
    """
    records = []
    for year in EVENTS:
        for state, frame in federal(year).items():
            for mode in MODES:
                records.append(dict(year=year, state=state, kind='federal', mode=mode, **contrast(frame, mode)))
    table = pd.DataFrame(records)
    comparable = table[table.seats >= 5].pivot(index=['state','year'],columns='mode',values='shift')[list(MODES)]
    differences = [row-comparable.loc[(s,y-3)] for (s,y),row in comparable.iterrows() if (s,y-3) in comparable.index]
    prior = table[(table.state == 'SA') & (table.year == 2025)].set_index('mode').loc[list(MODES)].copy()
    # Both elections covered every SA federal division. Giving each election
    # one vote tests sensitivity to a single federal contest, particularly
    # for postals, whose direction changed between 2022 and 2025.
    two_elections = table[(table.state == 'SA') & table.year.isin((2022, 2025))]
    prior['two_election_shift'] = two_elections.groupby('mode')['shift'].mean()
    transfer_scores = []
    for year, preceding in [(2020,2019),(2024,2022)]:
        qld = queensland(year)
        prior_qld = table[(table.year == preceding) & (table.state == 'QLD')]
        transfer_scores.append(score_transfer(qld, prior_qld, year, 'QLD'))
        for mode in MODES:
            records.append(dict(year=year, state='QLD', kind='state', mode=mode, **contrast(qld, mode)))
    records.append(dict(year=2022,state='SA',kind='state',mode='Combined',**contrast(old_sa_combined(),'Combined')))
    return pd.DataFrame(records), prior, np.array(differences), transfer_scores

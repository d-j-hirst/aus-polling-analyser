"""Test district preference estimates by withholding the 2022 Victorian TPP.

Run from any directory with --help for inputs. The primary-only test supplies
each method with the same reported candidate FPs and asks what Labor would
receive against the named Coalition opponent. Only after estimating the TPP
does it score against the VEC's 2022 count. This isolates conversion errors
from FP projection errors and the benefit of counting the actual TPP.

Supply the "2PP results - all districts" workbook from the VEC's historical
2018 summary and the two statewide district workbooks linked under the 2022
results. The saved replay supplies pre-count FP/TPP and a separate benchmark
from counting in other districts. No simulation, download or upload is run.
https://itsitecoreblobvecprd01.blob.core.windows.net/public-files/historical-results/state2018/summary.html
https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results
"""

import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
from statistics import mean
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'analysis'))
from lib.elections.election_data import _ElectionCacheUnpickler
from lib.live_analysis_archive import load_json
from scripts.turnout.turnout_qld_operational import read_xlsx_rows


GREEN = .8194
OTHER = .4925
FOCUS = {'Richmond', 'Melbourne', 'Northcote', 'Brunswick'}
COALITION = {'Liberal', 'National'}
# Victorian House TPP-by-party tables available before 26 November 2022:
# results.aec.gov.au/{event}/Website/HouseStateTppFlow-{event}-VIC.htm
# May 2022 (27966) where available, otherwise 2019 (24310). Family First
# (2016, 20499) is an analogy to the re-founded party. Unmatched parties use
# the existing Victorian Others prior, including Freedom and Legalise Cannabis.
NAMED = {
    'Animal Justice': .6708, 'Victorian Socialists': .8345, 'Reason': .8073,
    'Liberal Democrats': .2643, 'One Nation': .3553, 'Justice': .5183,
    'Sustainable Australia': .5056, 'Democratic Labour': .5868,
    'Shooters, Fishers, Farmers': .3507, 'Family First': .4141,
}
VEC_NAMES = {
    'Australian Labor Party - Victorian Branch': 'Labor', 'The Greens': 'Greens',
    'The Nationals': 'National', 'Libertarian': 'Liberal Democrats',
    "Pauline Hanson's One Nation": 'One Nation', "Fiona Patten's Reason Party": 'Reason',
    'Animal Justice Party': 'Animal Justice', 'Family First Victoria': 'Family First',
    'Labour DLP': 'Democratic Labour', 'Democratic Labor': 'Democratic Labour',
    'Shooters, Fishers & Farmers Vic': 'Shooters, Fishers, Farmers',
    "Derryn Hinch's Justice Party": 'Justice',
    'The Australian Greens - Victoria': 'Greens', 'National Party of Australia - Victoria': 'National',
    'Liberal Party of Australia - Victorian Division': 'Liberal', 'Liberal Democratic Party': 'Liberal Democrats',
    'Democratic Labour Party (DLP)': 'Democratic Labour',
    'Shooters, Fishers and Farmers Party Victoria': 'Shooters, Fishers, Farmers',
}


def read_seats():
    result = {}
    for line in (ROOT / 'analysis/seats/2022vic.txt').read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('#'):
            fields = result.setdefault(line[1:].strip(), {})
        elif '=' in line:
            key, value = line.split('=', 1)
            fields[key] = value
    return result


def normalized(counts):
    total = sum(counts.values())
    return {party: n / total for party, n in counts.items() if n > 0}


def workbook(path):
    return read_xlsx_rows(path.read_bytes(), str(path))


def tpp_results(path, previous=False):
    rows = workbook(path)
    assert rows[1]['B'] == ('ALP votes' if previous else 'Method')
    share_col, formal_col = ('C', 'F') if previous else ('D', 'G')
    results = {}
    for row in list(rows.values())[1:]:
        label = row.get('A', '')
        if not label or label.upper() == 'TOTAL' or not row.get(share_col):
            continue
        name = label.partition(' (')[0]
        results[name] = (float(row[share_col]), int(row[formal_col]),
                         'National' if 'v Nat' in label else 'Liberal')
    return results


def old_fields():
    # The archive retains original affiliations, even where the simulator's
    # simplified historical CSV groups several parties under "Centre-left".
    path = ROOT / 'analysis/Archived/elections/2018vic_results.pkl'
    with path.open('rb') as stream:
        seats = _ElectionCacheUnpickler(stream).load().results
    fields = {}
    for seat in seats:
        counts = Counter()
        inds = [c for c in seat.fp if c.party == 'Independent']
        principal = max(inds, key=lambda c: c.votes).name if inds else None
        for c in seat.fp:
            party = VEC_NAMES.get(c.party, c.party)
            if party == 'Independent' and c.name == principal:
                party = 'IND ' + c.name.casefold()
            counts[party] += c.votes
        pair = {c.party for c in seat.tcp}
        coalition = COALITION & counts.keys()
        # A three-cornered historical race requires an identified TPP opponent.
        # A non-classic final pair supplies no such identification on its own.
        named = coalition if len(coalition) == 1 else coalition & pair if 'Labor' in pair else set()
        fields[seat.name] = (normalized(counts), next(iter(named)) if len(named) == 1 else None,
                             bool('Labor' in pair and COALITION & pair))
    return fields


def counted_primaries(path, roster):
    rows = workbook(path)
    assert rows[1]['I'] == 'Votes'
    counts = defaultdict(Counter)
    for row in list(rows.values())[1:]:
        if row.get('B') != 'Recheck' or row.get('C') != 'First Preference' or not row.get('G'):
            continue
        seat, party = row['A'], row['G']
        if party in ('Total Votes', 'Informal Votes'):
            continue
        party = VEC_NAMES.get(party, party)
        if party == 'Independent' and row['E'].casefold() == roster.get(seat):
            party = 'IND ' + roster[seat]
        counts[seat][party] += int(row['I'])
    return counts


def nominated_fields(path, roster):
    result = {}
    for contest in ET.parse(path).findall('.//{*}Election/{*}Contest'):
        label = contest.findtext('{*}ContestIdentifier/{*}ContestName')
        if not label.endswith(' District'):
            continue
        seat = label[:-len(' District')]
        parties = Counter()
        for c in contest.findall('{*}Candidate'):
            surname, _, given = c.findtext('{*}CandidateIdentifier/{*}CandidateName').partition(',')
            name = (given.strip() + ' ' + surname.strip()).strip().casefold()
            party = c.findtext('{*}Affiliation/{*}AffiliationIdentifier/{*}RegisteredName', 'Independent').strip()
            party = VEC_NAMES.get(party, party)
            if party == 'Independent' and name == roster.get(seat):
                party = 'IND principal'
            parties[party] += 1
        result[seat] = parties
    return result


def precount_field(starting, ballot):
    # No preferences or primaries from 2022 counting enter the starting test.
    # Keep separately projected candidates. Divide the unresolved pool equally
    # across the remaining nominees, the trial's simple nomination-day prior.
    fixed = {p: n for p, n in starting.items() if p in COALITION | {'Labor', 'Greens'}
             or (p == 'IND principal' and p in ballot)}
    remaining = {p: n for p, n in ballot.items() if p not in fixed and p not in COALITION | {'Labor', 'Greens'}}
    unresolved = 1 - sum(fixed.values())
    if remaining:
        fixed.update({p: n/sum(remaining.values())*unresolved for p, n in remaining.items()})
    elif unresolved > 0:
        fixed['Emerging'] = unresolved
    return fixed


def log_odds(p):
    p = min(max(p, 1e-7), 1-1e-7)
    return math.log(p / (1-p))


def shift(p, offset):
    return 1 / (1 + math.exp(-log_odds(p) - offset))


def tactical(share, prior_tpp, previous_margin, previous_swing):
    """Central version of allocateMajorPartyFp's individual tactical rate.

    Inputs are proportions except the previous margin and swing in points.
    The control also sends the whole grouped Others share here, allowing the
    size of several unrelated candidates together to trigger tactical voting.
    """
    size, tpp = 100*share, 100*prior_tpp
    if size <= 5:
        return OTHER
    cap = (4.503 - .268*size + 1.344*tpp if tpp <= 50
           else 100 - (24.318 - .788*size + 1.0781*(100-tpp)))
    cap = min(max(cap, 5), 95)
    past = previous_margin - previous_swing/2
    upper = max(50-6*past, 100-cap) if past > 0 else min(50-6*past, cap)
    transition = min(size-5, 10)*.05
    middle = (1-transition)*OTHER + transition*upper/100
    return (5*OTHER + min(size-5, 10)*middle + max(size-15, 0)*upper/100) / size


def rates_for(fp, opponent, options, context):
    """Estimate each donor's Labor fraction. Only one identifiable candidate
    can attract tactical voters in the proposed method. Rate unknown small
    candidates at the existing state prior, regardless of their combined size.
    Greens strength is measured relative to typical 2018 classic districts.
    The slope 0.432 was estimated from 2019 federal ballots alone.
    """
    local_green, named_minors, grouped_tactical = options
    tpp, margin, swing, reference_strength = context
    grouped = sum(n for p, n in fp.items() if p not in COALITION | {'Labor', 'Greens', 'Emerging'}
                  and not p.startswith('IND '))
    rates = {}
    for party, share in fp.items():
        if party == 'Labor':
            rate = 1.
        elif party == opponent:
            rate = 0.
        elif party in COALITION:
            rate = .15  # Existing partner-to-Labor leakage, fixed in this comparison.
        elif party == 'Greens':
            rate = shift(GREEN, .432*(log_odds(share)-reference_strength)) if local_green else GREEN
        elif party.startswith('IND '):
            rate = tactical(share, tpp, margin, swing)
        elif grouped_tactical and party != 'Emerging':
            rate = tactical(grouped, tpp, margin, swing)
        else:
            rate = NAMED.get(party, OTHER) if named_minors else OTHER
        rates[party] = rate
    return rates


def estimate(fp, rates, offset=0):
    # The old total identifies a local tendency across donors. Keep the two
    # finalists' primaries and the separate Coalition leakage account fixed.
    return sum(n * (rates[p] if p in COALITION | {'Labor'} else shift(rates[p], offset))
               for p, n in fp.items())


def historical_offset(fp, rates, target):
    lo, hi = -18., 18.
    if not estimate(fp, rates, lo) < target < estimate(fp, rates, hi):
        return None  # Incompatible counts cannot identify a historical flow.
    for _ in range(50):
        mid = (lo + hi)/2
        if estimate(fp, rates, mid) < target:
            lo = mid
        else:
            hi = mid
    return (lo + hi)/2


def trial_estimates(fp, opponent, previous, context):
    # Half and full retention are sensitivity tests, fixed across districts.
    # Keep proxy districts for renamed seats with explicit continuity. Seats
    # only borrowing another district's FP baseline get no local residual.
    models = {}
    methods = {'tactical': (False, False, True), 'plain': (False, False, False),
               'green_only': (True, False, False), 'minors_only': (False, True, False),
               'named': (True, True, False)}
    for label, options in methods.items():
        rates = rates_for(fp, opponent, options, context)
        offset = 0.
        if previous:
            old_fp, old_opponent, target = previous
            old_rates = rates_for(old_fp, old_opponent, options, context)
            fitted = historical_offset(old_fp, old_rates, target)
            if fitted is None:
                raise ValueError('Previous TPP outside reachable range')
            offset = fitted
        models[label] = rates, .5*offset
        if label == 'named':
            models['no_history'] = rates, 0.
            models['full_history'] = rates, offset
    return models


def reported_classic_counts(path):
    # For a second, late-count experiment, learn a common change from districts
    # that had already reported an actual Labor-Coalition count by this capture.
    # Their reported totals supply the teaching signal. The VEC's more recent
    # official TPPs remain solely the scoring targets.
    result = {}
    for seat in load_json(path)['seats']:
        counts = {r['party_index']: r['value'] for r in seat['node']['tcp_votes_current'] if r['value'] > 0}
        if set(counts) in ({0, 1}, {0, 4}):
            result[seat['name']] = counts[0], sum(counts.values()), 'National' if 4 in counts else 'Liberal'
    return result


def apply_broad_learning(rows, models, teachers):
    # Use completed FPs as in the primary-only experiment. For each teacher,
    # match the aggregate predicted Labor share to its *reported* TPP. A common
    # change in donor odds preserves the relative source rates. Leave the
    # target out, even if it has counted a TPP of its own. No donor destinations
    # within the mixed count are assumed known.
    from scipy.optimize import brentq
    import numpy as np
    from scipy.special import expit

    for key in ('plain', 'named', 'archive_prior'):
        ids, weights, odds = [], [], []
        required = {}
        for seat, (labor, formal) in teachers.items():
            fp, variants = models[seat]
            if key not in variants:
                continue
            rates, local_offset = variants[key]
            fixed = sum(fp[p]*rates[p] for p in COALITION | {'Labor'} if p in fp)
            required[seat] = labor - formal*fixed
            for p, share in fp.items():
                if p in COALITION | {'Labor'}:
                    continue
                ids.append(seat)
                weights.append(formal*share)
                odds.append(log_odds(rates[p]) + local_offset)
        ids, weights, odds = np.array(ids), np.array(weights), np.array(odds)
        for row in rows:
            seat = row['seat']
            fp, variants = models[seat]
            if key not in variants:
                row[key + '_learned'] = None
                continue
            use = ids != seat
            target = sum(required.values()) - required.get(seat, 0)
            delta = brentq(lambda x: weights[use] @ expit(odds[use]+x) - target, -18, 18)
            rates, local_offset = variants[key]
            row[key + '_learned'] = 100*estimate(fp, rates, local_offset+delta)


def evaluate(args):
    settings = read_seats()
    old = old_fields()
    history = tpp_results(args.previous_tpp, previous=True)
    reference = mean(log_odds(fp['Greens']) for fp, _, classic in old.values()
                     if classic and 'Greens' in fp)
    snapshots = sorted(args.replay.glob('snapshot_*__run_*.json'))
    before, after = [json.loads(p.read_bytes())['simulation_report'] for p in (snapshots[0], snapshots[-1])]
    assert not any(before['seat_fp_completion']) and before['seat_name'] == after['seat_name']
    origin = json.loads((args.replay/'packages/origin/summary.json').read_bytes())['seats']
    roster = {s: data['candidates']['3']['name'].casefold() for s, data in origin.items()
              if data['candidates'].get('3', {}).get('name')}
    counted = counted_primaries(args.primary, roster)
    ballots = nominated_fields(args.nominations, roster)
    # All methods receive the matchup being evaluated. No party destination
    # or share of 2022 preferences is used to estimate its source rates.
    outcomes = tpp_results(args.outcome)
    reported = reported_classic_counts(max(args.replay.glob('snapshot_*.analysis.json.gz')))
    rows, models, teachers = [], {}, {}
    assert len(counted) == 87 and sum(sum(c.values()) for c in counted.values()) == 3617000
    for i, seat in enumerate(before['seat_name']):
        if seat == 'Narracan':  # Poll postponed to the supplementary election.
            continue
        observed, formal, opponent = outcomes[seat]
        fp = normalized(counted[seat])
        assert 'Labor' in fp and opponent in fp and .95 < sum(counted[seat].values())/formal < 1.05
        prior = (before['seat_party_one_margin_average'][i]+50)/100
        cfg = settings[seat]
        predecessor = cfg.get('sPreviousName', seat)
        previous = None
        if predecessor in history and predecessor in old and 'sUseFpResults' not in cfg:
            old_fp, old_opponent, _ = old[predecessor]
            if old_opponent and old_opponent == opponent:
                previous = old_fp, old_opponent, history[predecessor][0]
        context = prior, float(cfg['fTppMargin']), float(cfg.get('fPreviousTppSwing', 0)), reference
        variants = trial_estimates(fp, opponent, previous, context)
        # Reconstruct the archived preference level at mean primaries. The
        # frozen prior has one pooled balance; fitting it is an approximation
        # to the simulator's separate fit within each draw. Split-Coalition
        # seats can have another starting opponent, so omit that comparator.
        archived_fp = before['seat_party_mean_fp_share'][i]
        keys = {0: 'Labor', 1: 'Liberal', 2: 'Greens', 3: 'IND principal',
                4: 'National', -2: 'IND emerging', -1: 'Grouped', -3: 'Emerging'}
        starting = normalized({keys[r['party_index']]: r['value'] for r in archived_fp})
        initial_alp_change = None
        if len(COALITION & ballots[seat].keys()) == 1:
            # At a fixed pre-count TPP, more minor preferences for Labor imply
            # fewer Labor primary votes and an equal increase for its opponent.
            # Keep this diagnostic unconstrained so any impossible FP is visible.
            field = precount_field(starting, ballots[seat])
            initial_rates, initial_offset = trial_estimates(field, opponent, previous, context)['named']
            initial_alp_change = 100*(prior - estimate(field, initial_rates, initial_offset))
            rates = rates_for(starting, opponent, (False, False, True), context)
            offset = historical_offset(starting, rates, prior)
            if offset is not None:
                # The live FP update keeps the starting source rates as the
                # primary mix changes. Do not recalculate tactical voting using
                # a candidate's eventual primary. New small parties inherit
                # the starting grouped rate, as in the live account.
                frozen = {}
                for party in fp:
                    if party in COALITION | {'Labor', 'Greens'}:
                        frozen[party] = rates.get(party, {'Labor': 1., opponent: 0., 'Greens': GREEN}.get(party, .15))
                    elif party.startswith('IND '):
                        frozen[party] = rates.get('IND principal', rates.get('IND emerging', OTHER))
                    else:
                        frozen[party] = rates.get('Grouped', OTHER)
                variants['archive_prior'] = frozen, offset
        models[seat] = fp, variants
        if seat in reported:
            labor, votes, counted_opponent = reported[seat]
            # Avoid attributing a partial count's geography to preferences.
            # Allow ordinary recheck differences between FP and TCP coverage.
            if counted_opponent == opponent and abs(votes/sum(counted[seat].values()) - 1) <= .02:
                teachers[seat] = labor, votes
        predictions = {key: 100*estimate(fp, rates, offset) for key, (rates, offset) in variants.items()}
        predictions.setdefault('archive_prior', None)
        rows.append(dict(seat=seat, formal=formal, observed=100*observed, history=bool(previous),
                         counted_tpp=after['seat_tpp_completion'][i],
                         replay=50+after['seat_party_one_margin_average'][i],
                         origin_tpp=100*prior, origin_alp=100*starting['Labor'],
                         origin_coalition=100*starting.get(opponent, 0), origin_alp_change=initial_alp_change,
                         observed_alp=100*fp['Labor'], observed_coalition=100*fp[opponent],
                         **predictions))
    assert len(rows) == 87 and len({r['seat'] for r in rows}) == 87
    print('Comparable counted districts for broad learning:', len(teachers))
    apply_broad_learning(rows, models, teachers)
    return rows


def report(rows):
    for label, group in [('All', rows), ('Four focal seats', [r for r in rows if r['seat'] in FOCUS]),
                         ('Other seats', [r for r in rows if r['seat'] not in FOCUS]),
                         ('No local TPP', [r for r in rows if r['counted_tpp'] == 0]),
                         ('Local TPP', [r for r in rows if r['counted_tpp'] > 0])]:
        print(label, len(group), 'districts. Mean absolute TPP error in points:')
        for key in ('tactical', 'plain', 'green_only', 'minors_only', 'no_history', 'named',
                    'full_history', 'plain_learned', 'named_learned'):
            print(' ', key, round(mean(abs(r[key]-r['observed']) for r in group), 3))
        comparable = [r for r in group if r['archive_prior'] is not None]
        if comparable:
            print('  archived starting rates / named on same', len(comparable), 'seats:',
                  *(round(mean(abs(r[k]-r['observed']) for r in comparable), 3)
                    for k in ('archive_prior', 'named')))
    # Accepting the new local flow creates a discrepancy between the archived
    # pre-count primaries and TPP. Test both endpoints and a midpoint without
    # allowing vote information to determine how to resolve it in any seat.
    single = [r for r in rows if r['origin_alp_change'] is not None]
    print('Pre-count mean absolute error (ALP FP, Coalition FP, TPP),', len(single), 'single-Coalition seats')
    for label, fp_weight, tpp_weight in [('Saved', 0, 0), ('Retain TPP', 1, 0),
                                         ('Share equally', .5, .5), ('Retain FP', 0, 1)]:
        errors = [(r['origin_alp'] + fp_weight*r['origin_alp_change'] - r['observed_alp'],
                   r['origin_coalition'] - fp_weight*r['origin_alp_change'] - r['observed_coalition'],
                   r['origin_tpp'] - tpp_weight*r['origin_alp_change'] - r['observed']) for r in single]
        print(label, *(round(mean(map(abs, column)), 3) for column in zip(*errors)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous-tpp', type=Path, default=ROOT/'tmp/vec-2018-tpp.xlsx',
                        help='VEC 2018 statewide 2PP results workbook')
    parser.add_argument('--primary', type=Path, default=ROOT/'tmp/vec-2022-final-replay-fp.xlsx',
                        help='VEC 2022 statewide final recheck FP workbook')
    parser.add_argument('--outcome', type=Path, default=ROOT/'tmp/vec-2022-final-replay-tpp.xlsx',
                        help='VEC 2022 statewide 2PP results workbook (evaluation)')
    parser.add_argument('--replay', type=Path, default=ROOT/'live_runs/2022vic-8-initial')
    parser.add_argument('--nominations', type=Path, default=ROOT/'downloads/2022vic_candidates.xml')
    parser.add_argument('--csv', type=Path, help='Optional district-level estimates, percentages of formal votes')
    args = parser.parse_args()
    rows = evaluate(args)
    report(rows)
    if args.csv:
        with args.csv.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)


if __name__ == '__main__':
    main()

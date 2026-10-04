"""Examine evidence about late declaration returns without changing the model.

Exact cumulative formal counts from retained feeds are compared with reviewed
final counts. Elapsed-time summaries and recent-growth diagnostics distinguish
categories that have not started from categories that have already slowed.
Final counts score those observations only; they never define a live status.
The JSON contains analysis and provenance, with findings explained in chat.
"""

import argparse
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from ftplib import FTP
import hashlib
import io
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import numpy as np

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.shared import turnout_data
from lib.turnout import aec_live, category_policy, live, prior
from scripts.turnout import turnout_live_prototype as sa


FEDERAL = {'2019fed': ('24310', '2019-05-18'),
           '2022fed': ('27966', '2022-05-21'), '2025fed': ('31496', '2025-05-03')}
SA_DECLARATIONS = {
    'Polling Day Absent Ordinary Votes': 'Absent',
    'Early Voting Absent Ordinary Votes': 'PrePoll',
    'Polling Day Declaration Votes': 'Provisional',
    'Early Voting Declaration Votes': 'Early Provisional',
    'Postal Votes': 'Postal',
    'Electoral Visitor/Mobile Declaration Votes': 'EVM',
    'Telephone/Interstate/Overseas Declaration Votes': 'TIO'}
FINAL_CATEGORIES = {'absent': 'Absent', 'declaration_early': 'PrePoll',
                    'provisional': 'Provisional', 'postal': 'Postal',
                    'mobile_or_institution': 'EVM', 'other': 'TIO'}
DIRECTORY = REPOSITORY_DIRECTORY/'docs/turnout-federal-live-prototype'


def fetch_late_checkpoints(code, refresh=False):
    """Retain a few official late checkpoints, preserving source revisions.

    Earlier local sources already cover the first week. A handful of later
    observations fills the gap without acquiring the entire feed archive.
    Selection records actual timestamps, not assumed midnight counts.
    """
    event, date = FEDERAL[code]
    directory = REPOSITORY_DIRECTORY/'downloads/turnout/declaration-progress'/code
    manifest_path = directory/'latest.json'
    previous = sa.read_json(manifest_path) if manifest_path.exists() else {}
    with FTP('mediafeedarchive.aec.gov.au', timeout=30) as ftp:
        ftp.login()
        ftp.cwd('/'+event+'/Detailed/Light')
        candidates = {}
        for name in ftp.nlst():
            match = re.fullmatch(r'aec-mediafeed-Detailed-Light-'+event+r'-(\d{14})\.zip', name)
            if match:
                candidates[datetime.strptime(match[1], '%Y%m%d%H%M%S')] = name
        start = datetime.fromisoformat(date)
        targets = [start+timedelta(days=d, hours=21) for d in (10, 12, 14, 16, 18, 21, 24, 28)]
        names = sorted({candidates[min(candidates, key=lambda t: abs((t-target).total_seconds()))]
                        for target in targets})
        manifest = {}
        for name in names:
            if name in previous and not refresh:
                manifest[name] = previous[name]
                continue
            buffer = io.BytesIO()
            ftp.retrbinary('RETR '+name, buffer.write)
            data = buffer.getvalue()
            fingerprint = hashlib.sha256(data).hexdigest()
            path = directory/fingerprint/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            manifest[name] = dict(sha256=fingerprint,
                url='ftp://mediafeedarchive.aec.gov.au/'+event+'/Detailed/Light/'+name,
                retrieved_at=datetime.now(timezone.utc).isoformat())
        manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')


def federal_paths(code, archive, downloads):
    """Choose one late-day snapshot per available day; keep gaps explicit."""
    event, date = FEDERAL[code]
    candidates, manifests = {}, []
    for directory in (archive, downloads):
        for path in directory.glob('*'+event+'*'):
            match = re.search(r'Detailed-Light-'+event+r'-(\d{14})', path.name)
            if match:
                candidates[datetime.strptime(match[1], '%Y%m%d%H%M%S')] = path
    for root in ('federal-ppvc-reporting', 'declaration-progress'):
        directory = REPOSITORY_DIRECTORY/'downloads/turnout'/root/code
        manifest_path = directory/'latest.json'
        if not manifest_path.exists():
            continue
        manifests.append(manifest_path)
        for name, entry in sa.read_json(manifest_path).items():
            path = directory/entry['sha256']/name
            if sa.digest(path) != entry['sha256']:
                raise ValueError('Checkpoint source revision differs: '+str(path))
            stamp = re.search(r'-(\d{14})\.zip$', name)[1]
            candidates[datetime.strptime(stamp, '%Y%m%d%H%M%S')] = path
    grouped = defaultdict(list)
    start = datetime.fromisoformat(date)
    for stamp in candidates:
        age = (stamp.date()-start.date()).days
        if 0 <= age <= 28:
            grouped[age].append(stamp)
    # Later days with no update are absent. A gap is not a zero daily increment.
    return [candidates[max(times)] for _, times in sorted(grouped.items())], manifests


def final_counts(code):
    """Read supported final category targets, excluding reviewed missing splits."""
    path = ANALYSIS_DIRECTORY/'Data/Turnout'/f'{code}.json'
    data = sa.read_json(path)
    excluded = {r['seat'] for r in category_policy.missing_count_review(turnout_data.load_dataset(path))}
    totals = {r['seat_name']: r['formal_votes'] for r in data['seat_totals']}
    categories = defaultdict(lambda: defaultdict(int))
    for row in data['vote_types']:
        seat, category = row['seat_name'], row['canonical_category']
        if seat in excluded or row['formal_votes'] is None:
            continue
        name = FINAL_CATEGORIES.get(category)
        if code == '2026sa':
            # The website's early_in_person includes early absent batches,
            # while the XML calls them declaration PrePoll. Match the explicit
            # service labels here, rather than using that broader canonical
            # grouping. Mobile teams remain ordinary, not EVM declarations.
            label = row['source_category']
            prefixes = {'Early Voting - Absent Declaration': 'PrePoll',
                        'Early Voting - Declaration': 'Early Provisional',
                        'Polling Day - Absent Declaration': 'Absent',
                        'Polling Day - Declaration': 'Provisional',
                        'Postal - Declaration': 'Postal',
                        'Electoral Visitor & Mobile Polling - Declaration': 'EVM',
                        'Telephone/Interstate/Overseas Declaration Votes - Declaration': 'TIO'}
            name = next((n for prefix,n in prefixes.items() if label.startswith(prefix)),None)
        if category == 'ordinary_combined':
            name = 'Ordinary'
        if name:
            categories[seat][name] += row['formal_votes']
    return totals, dict(categories), excluded, path


def read_sa(path):
    """Aggregate exact ECSA candidate entries into booths and declarations.

    Declaration labels mirror the existing C++ loader. Individual place rows
    combine into categories before analysing progress. An absent booth count
    remains absent. The old explicit Narungga correction is retained separately
    so its accounting agrees with the previous prototype.
    """
    root = ET.parse(path).getroot()
    ns = root.tag.split('}')[0]+'}'
    seats = {}
    for district in root.findall(ns+'districts/'+ns+'district'):
        name = district.findtext(ns+'district_name')
        candidates = district.findall(ns+'first_preferences/'+ns+'candidate')
        booth_counts = defaultdict(int)
        parties, party_groups = {}, {}
        for candidate in candidates:
            identifier = candidate.findtext(ns+'candidate_id')
            parties[identifier] = candidate.findtext(ns+'affiliation')
            counts = defaultdict(int)
            for place in candidate.findall(ns+'polling_places/'+ns+'polling_place'):
                booth_name = place.findtext(ns+'polling_place_name')
                value = int(place.findtext(ns+'ballot_papers'))
                booth_counts[booth_name] += value
                counts[SA_DECLARATIONS.get(booth_name, 'Ordinary')] += value
            party_groups[identifier] = dict(counts)
        typed = defaultdict(int)
        for booth, value in booth_counts.items():
            typed[SA_DECLARATIONS.get(booth, 'Ordinary')] += value
        correction = sa.archived_narungga_correction(name, candidates, ns)
        if correction:
            typed['PrePoll'] += correction
            booth_counts['Early Voting Absent Ordinary Votes'] += correction
        seats[name] = dict(counted=sum(typed.values()), vote_types=dict(typed),
            booths=dict(booth_counts), parties=parties, candidate_groups=party_groups,
            correction=correction,
            finalised=district.findtext(ns+'first_preferences_finalised') == 'true')
    stamp = root.findtext('.//'+ns+'last_updated')
    if stamp is None:
        raise ValueError('SA source has no timestamp.')
    return dict(source_time=stamp.replace(' ', 'T'), seats=seats)


def quiet_cases(snapshots, categories, final, state=None):
    """Test slowing counts without using final counts to define quietness.

    Require an already-started category and observations spanning at least
    three days. Compare the largest count movement in that window with 0.1%
    of its current count, allowing a ten-vote recheck tolerance. Missing days
    are gaps, not observations of no movement. The retrospective final target
    measures net additions only; arrivals and corrections can offset each other.
    """
    rows = []
    for i, current in enumerate(snapshots):
        time = datetime.fromisoformat(current['source_time'])
        previous = [(j, s) for j, s in enumerate(snapshots[:i])
                    if (time-datetime.fromisoformat(s['source_time'])).total_seconds() >= 72*3600]
        if not previous:
            continue
        start, first = previous[-1]
        span = (time-datetime.fromisoformat(first['source_time'])).total_seconds()/3600
        # Very sparse late sources cannot support a three-day quietness claim.
        if span > 120:
            continue
        for category in categories:
            shared = []
            for seat, data in current['seats'].items():
                if state is not None and data.get('state') != state:
                    continue
                value = data['vote_types'].get(category)
                history = [s['seats'][seat]['vote_types'].get(category) for s in snapshots[start:i+1]]
                if value is None or any(v is None for v in history):
                    continue
                tolerance = max(10, .001*value)
                shared.append((seat, value, history, max(history)-min(history) <= tolerance))
            active_fraction = sum(not quiet for _, _, _, quiet in shared)/len(shared) if shared else None
            started_fraction = sum(value > 0 for _,value,_,_ in shared)/len(shared) if shared else None
            counted = sum(value for _,value,_,_ in shared)
            activity_percent = 100*sum(max(history)-min(history) for _,_,history,_ in shared)/counted if counted else None
            for seat, value, history, quiet in shared:
                target = final.get(seat, {}).get(category)
                if target is None or value < 100 or not quiet:
                    continue
                future = target-value
                rows.append(dict(seat=seat, category=category, source_timestamp=current['source_time'],
                    day=current['day'], current=value, final=target, net_remaining=future,
                    pooling_scope=state or 'whole_election', pooling_districts=len(shared),
                    window_hours=span, observed_window_change=max(history)-min(history),
                    district_activity_fraction=active_fraction, started_district_fraction=started_fraction,
                    aggregate_activity_percent=activity_percent,
                    substantial_later_net_addition=future > max(100, .01*value)))
    return rows


def summarise_progress(snapshots, final, categories, state=None):
    """Describe completed fractions and remaining tails in votes, not fitted rates."""
    rows = []
    for current in snapshots:
        for category in categories:
            cases = [(name, s['vote_types'].get(category), final.get(name, {}).get(category))
                     for name, s in current['seats'].items() if state is None or s.get('state') == state]
            cases = [(name, value, target) for name, value, target in cases
                     if value is not None and target is not None and target > 0]
            if not cases:
                continue
            remaining = np.array([target-value for _, value, target in cases])
            rows.append(dict(source_timestamp=current['source_time'], day=current['day'], category=category,
                districts=len(cases), counted=sum(v for _, v, _ in cases), final=sum(t for _, _, t in cases),
                counted_percent_of_final=100*sum(v for _, v, _ in cases)/sum(t for _, _, t in cases),
                districts_without_a_positive_count=sum(v == 0 for _, v, _ in cases),
                median_net_remaining=float(np.median(remaining)), p90_net_remaining=float(np.quantile(remaining,.9)),
                max_net_remaining=int(remaining.max()), net_remaining=int(remaining.sum()),
                negative_recheck_difference=int(-remaining[remaining < 0].sum())))
    return rows


def analyse_history(code, archive, downloads):
    """Keep within-election count timing separate from the live prior estimates."""
    totals, final, excluded, final_path = final_counts(code)
    if code == '2026sa':
        date = '2026-03-21'
        indexed = sa.source_index(downloads)
        grouped = defaultdict(list)
        for stamp in indexed:
            if stamp[:8] >= '20260321':
                grouped[stamp[:8]].append(stamp)
        paths = [indexed[max(times)][-1] for _, times in sorted(grouped.items())]
        files = [final_path]
        reader = read_sa
        categories = ['Absent', 'PrePoll', 'Provisional', 'Early Provisional', 'Postal', 'EVM', 'TIO']
    else:
        event, date = FEDERAL[code]
        paths, files = federal_paths(code, archive, downloads)
        files.append(final_path)
        preload_paths = list(archive.glob('*'+event+'*Preload*')) or list((REPOSITORY_DIRECTORY/'downloads').glob('*Preload*'+event+'*.xml'))
        preload_paths = [p for p in preload_paths if 'polling_places' not in p.name]
        if len(preload_paths) != 1:
            raise ValueError('Expected one electorate/candidate preload for '+code)
        preload = aec_live.xml_root(preload_paths[0])
        states = {c.findtext('e:ContestIdentifier/e:ContestName',namespaces=aec_live.NS):
                  c.find('a:PollingDistrictIdentifier/a:StateIdentifier',aec_live.NS).get('Id')
                  for c in preload.findall('.//a:House/a:Contests/a:Contest',aec_live.NS)}
        files.append(preload_paths[0])
        reader = lambda path: aec_live.read_house(path, event_id=event)
        categories = list(aec_live.DECLARATIONS)
    snapshots = []
    for path in paths:
        current = reader(path)
        if code != '2026sa':
            for name, data in current['seats'].items():
                data['state'] = states[name]
        time = datetime.fromisoformat(current['source_time'])
        current['day'] = (time.date()-datetime.fromisoformat(date).date()).days
        snapshots.append(current)
        files.append(path)
    progress = summarise_progress(snapshots, final, categories)
    quiet = quiet_cases(snapshots, categories, final)
    by_state = {}
    if code != '2026sa':
        for state in sorted(set(states.values())):
            by_state[state] = dict(progress=summarise_progress(snapshots,final,categories,state),
                                   quiet_cases=quiet_cases(snapshots,categories,final,state))
    return dict(election=code, categories=categories, omitted_category_districts=sorted(excluded),
        actual_total=sum(totals.values()), progress=progress, quiet_cases=quiet, by_state=by_state,
        total_progress=[dict(source_timestamp=s['source_time'], day=s['day'],
            counted=sum(d['counted'] for d in s['seats'].values()),
            net_remaining=sum(totals.values())-sum(d['counted'] for d in s['seats'].values()),
            fp_finalised=sum(d.get('finalised', False) for d in s['seats'].values())) for s in snapshots]), snapshots, files


def sa_late_predictions(snapshots, source_path, fixture_path):
    """Extend frozen SA inputs to later raw counts without GUI simulation.

    Existing exported no-results unit identities and weights supply the prior
    account. Raw current candidate entries supply observations. Previously
    absent XML measurements are not replaced with zero, and an unexpected
    current identity stops the comparison rather than inventing its weight.
    """
    source = sa.read_json(source_path)
    fixture = sa.load_fixture(fixture_path)
    inputs, parameters = fixture['inputs'], fixture['parameters']
    draws = prior.draw(inputs, parameters, 1024, 20261002)
    _, central = prior.central_counts(inputs, parameters)
    truth = sa.scoring_data(ANALYSIS_DIRECTORY/'Data/Turnout/2026sa.json', inputs['seat_names'])
    template = source['snapshots'][0]['units']
    rows = []
    for current in snapshots:
        if current['day'] < 6:
            continue
        units = []
        for original in template:
            unit = dict(original)
            seat = current['seats'][unit['seat_name']]
            name = next((k for k, v in SA_DECLARATIONS.items() if v == unit['name']), unit['name'])
            value = seat['booths'].get(name)
            if value is None:
                if unit.get('closed_reason'):
                    value = 0
                else:
                    raise ValueError('Raw SA source omits a required count: '+unit['seat_name']+'/'+name)
            unit['counted'] = value
            units.append(unit)
        observed = np.bincount([u['seat_index'] for u in units], weights=[u['counted'] for u in units], minlength=47)
        if any(observed[i] != current['seats'][name]['counted'] for i,name in enumerate(inputs['seat_names'])):
            raise ValueError('Raw SA prediction account does not reconcile.')
        result = live.update(draws, inputs, units)
        case_rows = []
        for i, name in enumerate(inputs['seat_names']):
            addition = result['totals'][:,i]-observed[i]
            low, high = np.quantile(addition,[.025,.975])
            case_rows.append(dict(seat=name, counted=int(observed[i]), actual=int(truth['totals'][i]),
                actual_net_remaining=int(truth['totals'][i]-observed[i]), mean_remaining=float(addition.mean()),
                low_remaining=float(low), high_remaining=float(high),
                category_additions=[float(result['counts'][:,i,g].mean()-sum(u['counted'] for u in units
                    if u['seat_index']==i and u['group_index']==g)) for g in range(len(inputs['categories']))]))
        rows.append(dict(source_timestamp=current['source_time'], day=current['day'],
            mean_remaining=float((result['totals']-observed).mean()),
            total_score=sa.interval_summary(result['totals'],truth['totals']), districts=case_rows))
    return rows


def federal_share_effect(source_path):
    """Isolate the mechanical FP effect of the prototype's remaining amounts.

    Weight each outstanding category with its currently reported candidate
    mix, then compare with the current district share. This is an illustration
    of category-size influence, not the C++ party forecast or a TCP/winner
    prediction. Omit districts with an unsupported category mix rather than
    inventing candidate shares for a category that has not reported.
    """
    source = sa.read_json(source_path)
    rows, omitted, files = [], [], [source_path]
    preload_path = next((REPOSITORY_DIRECTORY/'downloads').glob('*Preload*31496*.xml'))
    preload = aec_live.xml_root(preload_path)
    identities = {}
    for candidate in preload.findall('.//a:Candidate',aec_live.NS):
        identifier = candidate.find('e:CandidateIdentifier',aec_live.NS)
        affiliation = candidate.find('e:AffiliationIdentifier',aec_live.NS)
        if identifier.findtext('e:CandidateName',namespaces=aec_live.NS) is None:
            continue
        identities[identifier.get('Id')] = dict(
            name=identifier.findtext('e:CandidateName',namespaces=aec_live.NS),
            party=affiliation.get('ShortCode') if affiliation is not None else None)
    files.append(preload_path)
    for snapshot in source['snapshots']:
        if snapshot['source_timestamp'] < '2025-05-14':
            continue
        path = next(Path(p) for p in source['provenance']['snapshots'] if Path(p).name == snapshot['archive'])
        if sa.digest(path) != source['provenance']['snapshots'][str(path)]:
            raise ValueError('FP illustration source differs from its replay revision.')
        files.append(path)
        root = aec_live.xml_root(path)
        house = next(e for e in root.findall('a:Results/a:Election', aec_live.NS)
                     if e.find('e:ElectionIdentifier',aec_live.NS).get('Id') == 'H')
        districts = {d['seat']:d for d in snapshot['districts']}
        for contest in house.findall('a:House/a:Contests/a:Contest',aec_live.NS):
            name = contest.findtext('e:ContestIdentifier/e:ContestName',namespaces=aec_live.NS)
            district = districts[name]
            current = defaultdict(int)
            for unit in snapshot['units']:
                if unit['seat_name'] == name:
                    current['Ordinary' if unit['kind'] != 'declaration' else unit['vote_type']] += unit['counted']
            remaining = defaultdict(float)
            for category, final in zip(source['categories'],district['mean_groups']):
                kind = next((k for k,v in aec_live.DECLARATIONS.items() if v == category),'Ordinary')
                remaining[kind] += final
            for kind in remaining:
                remaining[kind] -= current[kind]
                if abs(remaining[kind]) < 1e-8:
                    remaining[kind] = 0.
                if remaining[kind] < 0:
                    raise ValueError('The FP illustration encountered negative additions.')
            unsupported = [k for k,r in remaining.items() if r > 0 and current[k] == 0]
            if unsupported:
                omitted.append(dict(seat=name,source_timestamp=snapshot['source_timestamp'],categories=unsupported))
                continue
            counted = sum(current.values())
            total = counted+sum(remaining.values())
            for candidate in contest.findall('a:FirstPreferences/a:Candidate',aec_live.NS):
                typed = {v.get('Type'):int(v.text) for v in candidate.findall('a:VotesByType/a:Votes',aec_live.NS)}
                votes = sum(typed.values())
                predicted = votes+sum(r*typed[k]/current[k] for k,r in remaining.items() if current[k] > 0)
                identifier = candidate.find('e:CandidateIdentifier',aec_live.NS).get('Id')
                identity = identities[identifier]
                rows.append(dict(seat=name,source_timestamp=snapshot['source_timestamp'],
                    candidate_id=identifier, candidate=identity['name'], party=identity['party'],
                    current_percent=100*votes/counted,weighted_percent=100*predicted/total,
                    shift_percentage_points=100*(predicted/total-votes/counted),
                    expected_additions=sum(remaining.values())))
    return dict(interpretation='FP reweighting at currently reported category mixes; not a party-model or winner forecast.',
                candidates=rows,omitted_district_updates=omitted), files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=Path('F:/Election Data/AEC media feed archive'))
    parser.add_argument('--downloads', type=Path, default=Path.home()/'Downloads')
    parser.add_argument('--fetch', action='store_true', help='Acquire selected later 2019/2022 FTP snapshots.')
    parser.add_argument('--refresh', action='store_true')
    parser.add_argument('--output', type=Path, default=DIRECTORY/'declaration-progress.json')
    args = parser.parse_args()
    histories, files = [], [Path(__file__),Path(aec_live.__file__),Path(live.__file__),Path(prior.__file__),
        Path(category_policy.__file__),Path(sa.__file__),Path(turnout_data.__file__)]
    sa_snapshots = None
    for code in (*FEDERAL,'2026sa'):
        if (args.fetch or args.refresh) and code in ('2019fed','2022fed'):
            fetch_late_checkpoints(code, args.refresh)
        result, snapshots, inputs = analyse_history(code, args.archive, args.downloads)
        histories.append(result)
        files.extend(inputs)
        if code == '2026sa':
            sa_snapshots = snapshots
        print(code+' complete', flush=True)
    source_path = REPOSITORY_DIRECTORY/'docs/turnout-live-prototype/analysis.json'
    fixture_path = REPOSITORY_DIRECTORY/'docs/turnout-prior-prototype/fixtures-v3.json'
    late = sa_late_predictions(sa_snapshots, source_path, fixture_path)
    files.extend([source_path,fixture_path])
    effect, effect_files = federal_share_effect(DIRECTORY/'analysis.json')
    files.extend(effect_files)
    result = dict(model_version=live.MODEL_VERSION,prior_model=prior.MODEL_VERSION,
        histories=histories, sa_late_predictions=late, federal_fp_weighting_illustration=effect,
        definitions=dict(remaining='Final formal count minus current formal count; net arrivals and rechecks, not gross received ballots.',
            quiet='At least 100 current votes and <=max(10 votes,0.1% current count) range over observations spanning 72–120 hours.',
            substantial='Later net addition exceeding both 100 votes and 1% of current category count.',
            geography='District final counts are compared with district cumulative returns; no cross-parent rate fitting.',
            status='Neither elapsed time nor quietness is treated as authoritative completion.'),
        provenance={str(p):sa.digest(p) for p in dict.fromkeys(files)})
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(histories=len(histories),sa_late_updates=len(late),output=str(args.output))))


if __name__ == '__main__':
    main()

"""Replay the smooth late-count prototype against retained SA and AEC snapshots.

Frozen no-results weights and prior parameters supply the broad prediction.
Current source history supplies progress evidence; reviewed finals score the
paired predictions. The SA replay additionally applies explicitly identified
retrospective category repairs, recorded separately from the forecast algorithm.
JSON retains parameters, conditional components and source hashes. The separate
report renderer explains the results publicly.
"""

import argparse
from collections import defaultdict
import copy
from datetime import datetime
import json
from pathlib import Path
import re
import time
import zipfile

import numpy as np

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.shared import turnout_data
from lib.turnout import aec_live, allocation, category_policy, late_counts, live, ppvc_sizes, prior, sa_hindcast
from scripts.turnout import turnout_declaration_progress as progress
from scripts.turnout import turnout_late_batch_report as report
from scripts.turnout import turnout_live_prototype as sa
from scripts.turnout import turnout_federal_live as federal


DIRECTORIES = {'2026sa':'turnout-live-prototype', '2025fed':'turnout-federal-live-prototype'}
# This is the published last receipt time, not a predicted completion time.
# Other jurisdictions require their own verified dates; no federal rule is
# silently applied to them. The evidence discount remains smooth around it.
POSTAL_DEADLINES = {'2025fed':'2025-05-16T18:00:00'}
POSTAL_DEADLINE_SOURCE = 'https://www.aec.gov.au/Elections/federal_elections/2025/timetable.htm'


def source_paths(code, archive, downloads, origin):
    """Retain daily late returns plus the existing election-night checkpoints."""
    if code == '2025fed':
        daily, files = progress.federal_paths(code,archive,downloads)
        replay = [Path(p) for p in origin['provenance']['snapshots']]
    else:
        indexed = sa.source_index(downloads)
        grouped = defaultdict(list)
        for stamp in indexed:
            if '20260321' <= stamp[:8] <= '20260402':
                grouped[stamp[:8]].append(stamp)
        daily = [indexed[max(stamps)][-1] for stamps in grouped.values()]
        replay = [Path(p) for paths in origin['provenance']['snapshots'].values() for p in paths
                  if Path(p).suffix == '.xml']
        files = []
    return list(dict.fromkeys([*daily,*replay])), files


def attach_counts(template, current, code):
    """Reuse frozen sizes and identities, attaching only current exact counts."""
    units = []
    for original in template:
        unit = dict(original)
        seat = current['seats'][unit['seat_name']]
        if code == '2025fed':
            value = seat['vote_types'][unit['vote_type']] if unit['kind'] == 'declaration' else current['booths'][unit['id']]['counted']
        else:
            name = next((k for k,v in progress.SA_DECLARATIONS.items() if v == unit['name']),unit['name'])
            value = seat['booths'].get(name)
            if value is None:
                if unit['name'] in seat.get('missing_categories', ()):
                    # The source cannot support this detailed category. Omit
                    # its unit rather than presenting the missing count as a
                    # genuine zero or declaring the category complete. The
                    # broad group estimate and other counted units stay intact.
                    continue
                if unit.get('closed_reason'):
                    value = 0
                else:
                    raise ValueError('Missing current SA unit count: '+unit['seat_name']+'/'+name)
        unit['counted'] = value
        units.append(unit)
    for name, seat in current['seats'].items():
        if sum(u['counted'] for u in units if u['seat_name'] == name) != seat['counted']:
            raise ValueError('Current unit and district totals differ: '+name)
    return units


def candidate_counts(path, current, code):
    """Read observed candidate mixes for a mechanical share illustration only."""
    if code == '2026sa':
        result = {name:copy.deepcopy(s['candidate_groups']) for name,s in current['seats'].items()}
        correct_narungga_candidates(result)
        return result
    root = aec_live.xml_root(path)
    result = {}
    for contest in root.findall('.//a:House/a:Contests/a:Contest',aec_live.NS):
        name = contest.findtext('e:ContestIdentifier/e:ContestName',namespaces=aec_live.NS)
        result[name] = {c.find('e:CandidateIdentifier',aec_live.NS).get('Id'):
            {v.get('Type'):int(v.text) for v in c.findall('a:VotesByType/a:Votes',aec_live.NS)}
            for c in contest.findall('a:FirstPreferences/a:Candidate',aec_live.NS)}
    return result


def correct_narungga_candidates(result):
    """Apply the archived loader's known misordered early-vote candidate row.

    The count reader already applies this correction to the category total.
    The optional candidate-share illustration needs the same candidate mapping,
    rather than allocating the extra 17 votes to an invented party. This exactly
    follows ElectionData.cpp's identified source error, including its trigger.
    """
    records = result.get('Narungga',{})
    if records.get('133004',{}).get('PrePoll') == 189:
        corrected = dict(zip(range(133001,133011),[189,9,95,13,153,11,311,8,13,46]))
        for identity,count in corrected.items():
            records[str(identity)]['PrePoll'] = count


def source_timestamp(path, code):
    """Index the feed's own clock without retaining all its booth/candidate data.

    Archived capture names can differ from source update times. Only the small
    XML header is needed to choose one retained revision per source time; the
    selected result is parsed in full when that prediction is evaluated.
    """
    if code == '2026sa':
        with path.open(encoding='utf-16') as stream:
            match = re.search(r'<last_updated>([^<]+)</last_updated>',stream.read(8192))
    else:
        with zipfile.ZipFile(path) as archive:
            members = [n for n in archive.namelist() if n.lower().endswith('.xml') and 'results-detailed' in n.lower()]
            if len(members) != 1:
                raise ValueError('Expected one detailed result XML in '+str(path))
            with archive.open(members[0]) as stream:
                match = re.search(r'Created="([^"]+)"',stream.read(16384).decode('utf-8-sig'))
    if not match:
        raise ValueError('Missing embedded source timestamp: '+str(path))
    return match[1].replace(' ','T')


def share_effects(units, before, after, candidates):
    """Compare means at observed category mixes; omit unsupported compositions."""
    effects, omitted = [], []
    for name, records in candidates.items():
        columns = [j for j,u in enumerate(units) if u['seat_name'] == name]
        if not sum(units[j]['counted'] for j in columns):
            continue
        categories = sorted({u.get('vote_type','Ordinary') for j in columns for u in [units[j]]})
        identities = sorted(records)
        observed = np.array([[records[p].get(c,0) for p in identities] for c in categories])
        a = np.array([sum(before[j] for j in columns if units[j].get('vote_type','Ordinary') == c) for c in categories])
        b = np.array([sum(after[j] for j in columns if units[j].get('vote_type','Ordinary') == c) for c in categories])
        if not np.array_equal(observed.sum(axis=1),[sum(units[j]['counted'] for j in columns
                           if units[j].get('vote_type','Ordinary') == c) for c in categories]):
            raise ValueError('Candidate and unit category totals differ: '+name)
        if np.any((observed.sum(axis=1) == 0) & (np.maximum(a,b) > 0)):
            omitted.append(name)
            continue
        old = late_counts.reweight_shares(observed,a[None])[0]
        new = late_counts.reweight_shares(observed,b[None])[0]
        for party, old_share, new_share in zip(identities,old,new):
            effects.append(dict(seat=name,candidate_id=party,broad_percent=100*old_share,
                                late_percent=100*new_share,change_pp=100*(new_share-old_share)))
    return dict(omitted_districts=omitted,largest_changes=sorted(effects,key=lambda r:abs(r['change_pp']),reverse=True)[:10])


def continuity_check(prepared, inputs, units, evidence, seed):
    """Check a representative seat across smoothly increasing evidence strength.

    Fixed count quantiles isolate model changes from new random sampling. The
    small perturbation check probes each point as well as the broad sweep; no
    pass/fail completion threshold enters the prediction.
    """
    seat = next(i for i in range(len(inputs['seat_names'])) if any(
        u['seat_index']==i and u['kind']=='declaration' and not prepared['complete'][j] for j,u in enumerate(units)))
    columns = [j for j,u in enumerate(units) if u['seat_index']==seat]
    local_inputs = dict(inputs,seat_names=[inputs['seat_names'][seat]],enrolment=[inputs['enrolment'][seat]])
    local_units = [dict(units[j],seat_index=0) for j in columns]
    local_prepared = dict(prepared,totals=prepared['totals'][:,seat:seat+1],counts=prepared['counts'][:,seat:seat+1],
        unit_counts=prepared['unit_counts'][:,columns],remaining=prepared['remaining'][:,columns],
        complete=prepared['complete'][columns],counted=prepared['counted'][seat:seat+1])
    rows, largest = [], 0.
    for strength in np.linspace(0,1,21):
        def evaluate(value):
            return late_counts.update(local_prepared,local_inputs,local_units,
                [dict(strength=value) for _ in columns],count_draws=8,seed=seed)
        result, nearby = evaluate(strength), evaluate(strength+1e-6)
        quantiles = late_counts.prediction_quantiles(result,[.025,.5,.975])[:,0]
        nearby_quantiles = late_counts.prediction_quantiles(nearby,[.025,.5,.975])[:,0]
        largest = max(largest,float(np.abs(nearby_quantiles-quantiles).max()),
                      float(abs(late_counts.prediction_mean(nearby)[0]-late_counts.prediction_mean(result)[0])))
        rows.append(dict(strength=float(strength),mean_total=float(late_counts.prediction_mean(result)[0]),quantiles=quantiles.tolist(),
                         no_addition_probability=float(result['no_addition_probability'][0])))
    return dict(seat=inputs['seat_names'][seat],evidence_perturbation=1e-6,
                maximum_mean_or_interval_change_votes=largest,curve=rows)


def run(code,args):
    """Score paired broad/late predictions using identical frozen inputs."""
    directory = REPOSITORY_DIRECTORY/'docs'/DIRECTORIES[code]
    origin_path = directory/'analysis.json'
    origin = sa.read_json(origin_path)
    fixture_path = REPOSITORY_DIRECTORY/'docs/turnout-prior-prototype/fixtures-v3.json'
    fixture = sa.load_fixture(fixture_path,code)
    inputs = copy.deepcopy(fixture['inputs'])
    template = origin['snapshots'][0]['units']
    if any(u['counted'] for u in template):
        raise ValueError('Allocation origin must contain no counted votes.')
    if code == '2025fed':
        for row in origin['enrolment_revisions']:
            inputs['enrolment'][inputs['seat_names'].index(row['seat'])] = row['election_day']
    draws = prior.draw(inputs,fixture['parameters'],args.samples,args.seed)
    paths, files = source_paths(code,args.archive,args.downloads,origin)
    files += [origin_path,fixture_path,Path(__file__),Path(late_counts.__file__),Path(live.__file__),Path(allocation.__file__),
              Path(prior.__file__),Path(progress.__file__),Path(sa.__file__),Path(report.__file__),
              Path(aec_live.__file__),Path(ppvc_sizes.__file__),Path(category_policy.__file__),
              Path(federal.__file__),REPOSITORY_DIRECTORY/'ElectionData.cpp']
    metadata = None
    if code == '2025fed':
        metadata_path = next(Path(p) for p in origin['provenance']['inputs'] if 'Preload' in p and 'polling_places' in p)
        metadata = aec_live.booth_metadata(metadata_path)
        files.append(metadata_path)
    sources = {source_timestamp(path,code):path for path in paths}
    # Reviewed finals score the forecast. For SA they also identify previously
    # counted batches in the explicitly retrospective data-repair helper. No
    # final total or completion status is supplied to the count updater.
    reviewed, missing_categories = {}, defaultdict(list)
    if code == '2026sa':
        truth_path = ANALYSIS_DIRECTORY/'Data/Turnout/2026sa.json'
        truth = sa.scoring_data(truth_path,inputs['seat_names'])
        files.append(truth_path)
        _, _, _, reviewed_path = sa.booth_scoring_data(truth_path)
        metadata_path = reviewed_path.with_name('static.json')
        reviewed = sa_hindcast.reviewed_groups(sa.read_json(reviewed_path),sa.read_json(metadata_path))
        # Carry the existing reviewed missing-data policy into the repaired
        # replay too. A zero left behind by relabelling is still unknown if its
        # final source entry was reviewed as missing; it is not a new unstarted
        # category to which the model should allocate a separate expectation.
        types = {'declaration_early':'Early Provisional', 'mobile_or_institution':'EVM',
                 'provisional':'Provisional', 'other':'TIO', 'early_in_person':'PrePoll'}
        for review in category_policy.missing_count_review(turnout_data.load_dataset(truth_path)):
            missing_categories[review['seat']].append(types[review['category']])
        files.extend([reviewed_path,metadata_path,Path(sa_hindcast.__file__)])
    else:
        truth = federal.scoring_data(inputs['seat_names'],inputs['categories'])
        files.extend(truth['files'])
    history, rows = [], []
    started = time.perf_counter()
    last = None
    for stamp,path in sorted(sources.items()):
        current = progress.read_sa(path) if code == '2026sa' else aec_live.read_house(path,metadata,event_id='31496')
        if code == '2026sa':
            current = sa_hindcast.apply_reviewed_overrides(current,reviewed,missing_categories)
        if current['source_time'] != stamp:
            raise ValueError('Indexed and parsed source timestamps differ: '+str(path))
        units = attach_counts(template,current,code)
        grouped = allocation.group_observation(units,inputs)
        history.append(dict(source_time=current['source_time'],seats={name:dict(vote_types={**s['vote_types'],**grouped[name]['vote_types']})
                        for name,s in current['seats'].items()}))
        finalised = [name for name,s in current['seats'].items() if s.get('finalised')]
        hours = (datetime.fromisoformat(stamp)-datetime(2025,5,3,18)).total_seconds()/3600 if code == '2025fed' else 0.
        decay = ppvc_sizes.reporting_decay_factor(hours) if code == '2025fed' and origin['config']['ppvc_reporting_decay'] else 1.
        tick = time.perf_counter()
        result = live.update_with_progress(draws,inputs,units,history,count_draws=args.count_draws,seed=args.seed,
            finalised_seats=finalised,unreported_ppvc_factor=decay,postal_deadline=POSTAL_DEADLINES.get(code),
            progress_options={'postal_deadline_transition_hours':args.postal_transition_hours})
        seconds = time.perf_counter()-tick
        broad = result['broad_prediction']
        means = late_counts.prediction_mean(result)
        bounds = late_counts.prediction_quantiles(result,[.025,.975])
        district_rows = []
        for i,name in enumerate(inputs['seat_names']):
            low,high = bounds[:,i]
            district_rows.append(dict(seat=name,counted=int(result['counted'][i]),actual=int(truth['totals'][i]),
                broad_mean_remaining=float(broad['totals'][:,i].mean()-result['counted'][i]),
                mean_remaining=float(means[i]-result['counted'][i]),
                no_addition_probability=float(result['no_addition_probability'][i]),
                low_remaining=float(low-result['counted'][i]),high_remaining=float(high-result['counted'][i])))
        effects = share_effects(units,broad['unit_counts'].mean(axis=0),result['unit_means'],candidate_counts(path,current,code))
        rows.append(dict(source_timestamp=stamp,counted=int(result['counted'].sum()),
            actual_net_remaining=int(truth['totals'].sum()-result['counted'].sum()),
            # Use the same empirical quantile grid for both predictions. Merely
            # repeating broad samples otherwise changes NumPy's interpolated
            # interval endpoints, even when no progress adjustment is made.
            broad_total_score=sa.interval_summary(np.repeat(broad['totals'],args.count_draws,axis=0),truth['totals']),
            total_score=sa.interval_summary(result['totals'],truth['totals'],mean=means,bounds=bounds),
            broad_category_score=sa.interval_summary(np.repeat(broad['counts'],args.count_draws,axis=0),truth['groups']),
            category_score=sa.interval_summary(result['counts'],truth['groups'],mean=late_counts.prediction_mean(result,'counts'),
                                               bounds=late_counts.prediction_quantiles(result,[.025,.975],'counts')),
            mean_remaining=float((means-result['counted']).mean()),
            broad_mean_remaining=float((broad['totals']-result['counted']).mean()),
            update_seconds=seconds,diagnostics=result['diagnostics'],districts=district_rows,
            components=result['components'],fp_weighting_illustration=effects,
            hindcast_overrides=current.get('hindcast_overrides',[])))
        files.append(path)
        last = result['allocation_prediction'],inputs,units,[dict(strength=0.) for u in units]
        print(code+' '+stamp+' complete',flush=True)
    continuity = continuity_check(*last,args.seed)
    # Measure cheap count evaluations separately, reusing the last prepared
    # broad account. This does not time new party-share preparations or imply
    # that the whole C++ application meets its election-night budget.
    subdivisions = dict(zip(inputs['seat_names'],inputs.get('subdivisions',[None]*len(inputs['seat_names']))))
    options = {'postal_deadline_transition_hours':args.postal_transition_hours}
    evidence = late_counts.progress_evidence(history,units,subdivisions,options,
                                             postal_deadline=POSTAL_DEADLINES.get(code))
    count_timing = {}
    for number in dict.fromkeys([1,args.count_draws]):
        tick = time.perf_counter()
        late_counts.update(last[0],inputs,units,evidence,count_draws=number,seed=args.seed,options=options)
        count_timing[str(number)] = time.perf_counter()-tick
    return dict(election=code,model_version=late_counts.MODEL_VERSION,broad_model=live.MODEL_VERSION,allocation_model=allocation.MODEL_VERSION,
        config=dict(samples=args.samples,count_draws=args.count_draws,seed=args.seed,**late_counts.configuration(options),
                    allocation_progress_scale=allocation.PROGRESS_SCALE,allocation_direction_scale=allocation.DIRECTION_SCALE),
        postal_deadline=POSTAL_DEADLINES.get(code),postal_deadline_source=POSTAL_DEADLINE_SOURCE if code == '2025fed' else None,
        omitted_category_districts=truth.get('omitted',[]),
        categories=inputs['categories'],snapshots=rows,continuity=continuity,count_draw_seconds=count_timing,
        elapsed_seconds=time.perf_counter()-started,
        provenance={str(p):sa.digest(p) for p in dict.fromkeys(files)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elections',nargs='+',choices=tuple(DIRECTORIES),default=list(DIRECTORIES))
    parser.add_argument('--archive',type=Path,default=Path('F:/Election Data/AEC media feed archive'))
    parser.add_argument('--downloads',type=Path,default=Path.home()/'Downloads')
    parser.add_argument('--samples',type=int,default=256)
    parser.add_argument('--count-draws',type=int,default=8)
    parser.add_argument('--seed',type=int,default=20261002)
    parser.add_argument('--postal-transition-hours',type=float,default=48.,
                        help='Smooth postal receipt-deadline response scale in hours.')
    parser.add_argument('--dry-run',action='store_true')
    args = parser.parse_args()
    if args.samples < 2 or args.count_draws < 1:
        parser.error('Use at least two preparation samples and one count draw.')
    for code in args.elections:
        result = run(code,args)
        if not args.dry_run:
            directory = REPOSITORY_DIRECTORY/'docs'/DIRECTORIES[code]
            (directory/'late-batch-analysis.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
            report_path = directory/'report.md'
            previous = report_path.read_text(encoding='utf-8').split(report.HEADING)[0].rstrip()
            report_path.write_text(previous+'\n\n'+report.render(result),encoding='utf-8')
        print(json.dumps(dict(election=code,snapshots=len(result['snapshots']),seconds=result['elapsed_seconds'],dry_run=args.dry_run)),flush=True)


if __name__ == '__main__':
    main()

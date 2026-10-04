"""Compare immediate, gradual and directional release of turnout allowances.

Run from analysis with python -B -m scripts.turnout.turnout_allocation_experiment.
The experiment reads retained SA 2026 and Federal 2025 sources and frozen prior
parameters. It performs no fitting and does not update live-model artifacts.
JSON outputs retain paired scores, assumptions, district/category diagnostics
and exact input hashes. Final results score predictions; the existing explicitly
retrospective SA repairs are kept identical across all alternatives.
"""

import argparse
from collections import defaultdict
import copy
from datetime import datetime
import json
from pathlib import Path
import time

import numpy as np

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.shared import turnout_data
from lib.turnout import aec_live, allocation as allocation_model, allocation_experiment as allocation, category_policy, late_counts, live, ppvc_sizes, prior, sa_hindcast
from scripts.turnout import turnout_declaration_progress as progress
from scripts.turnout import turnout_federal_live as federal
from scripts.turnout import turnout_late_batch as replay
from scripts.turnout import turnout_live_prototype as sa


def selected(stamp,code,position):
    """Score every SA source and a stated federal sequence; retain all history.

    Federal checkpoints cover election night, ordinary/declaration transitions,
    the postal receipt window and the end of counting. Other daily sources still
    supply progress history, so scoring fewer sources does not fabricate pauses.
    """
    return code == '2026sa' or position < 6 or stamp[:10] in {
        '2025-05-07','2025-05-11','2025-05-14','2025-05-17','2025-05-20','2025-05-24','2025-05-31'}


def score(result,actual,field='totals'):
    """Compare the same known districts/groups, including premature upper bounds.

    Negative signed error means underprediction. The upper-bound miss fraction
    identifies final results above the 95% interval, distinguishing premature
    closure from already counted votes that exceed reviewed final results.
    """
    mean = late_counts.prediction_mean(result,field)
    low,high = late_counts.prediction_quantiles(result,[.025,.975],field)
    valid = np.isfinite(actual)
    summary = sa.interval_summary(result[field],actual,mean=mean,bounds=(low,high))
    summary.update(signed_mean_error=float((mean-actual)[valid].mean()),
                   upper_bound_miss_fraction=float((high<actual)[valid].mean()))
    return summary


def cached_federal_history(path,sources):
    """Reuse exact extracted category history without reparsing every AEC feed.

    Only raw federal histories are supported here: SA needs its separate
    retrospective category repairs. Source clocks and hashes must match the
    current retained sources. The snapshot being predicted is still read in
    full, including its booth counts; cached district totals cannot replace it.
    """
    artifact = sa.read_json(path)
    if artifact['election'] != '2025fed':
        raise ValueError('Cached history is supported only for Federal 2025.')
    indexed = artifact['provenance']['sources']
    if set(indexed) != set(sources):
        raise ValueError('Cached history and retained source clocks differ.')
    for stamp,source in sources.items():
        if sa.digest(source) != indexed[stamp]['sha256']:
            raise ValueError('Cached history source has changed: '+str(source))
    history = sorted(artifact['history'],key=lambda row:row['source_time'])
    if [row['source_time'] for row in history] != sorted(sources):
        raise ValueError('Cached observations and retained source clocks differ.')
    # Federal declaration groups each correspond to one actual feed category.
    # Ordinary feed totals combine ordinary and PPVC booths, so they cannot be
    # partitioned here. Release applies only to declarations and needs no such
    # invented partition of booth history.
    aliases = {'Postal':'Postal','Absent':'allocation:absent',
               'PrePoll':'allocation:early_declaration','Provisional':'allocation:other'}
    grouped = [dict(source_time=row['source_time'],seats={name:dict(vote_types={
        target:seat['vote_types'][source] for source,target in aliases.items()})
        for name,seat in row['seats'].items()}) for row in history]
    return history,grouped


def run(code,args):
    """Reuse source parsing and preparation across the predetermined alternatives.

    The current model stays unchanged. Extra category spread is a sensitivity
    assumption, not a tuned parameter. Each update starts from the frozen prior;
    no prediction or final result is fed back into later snapshot estimates.
    """
    started = time.perf_counter()
    origin_path = REPOSITORY_DIRECTORY/'docs'/replay.DIRECTORIES[code]/'analysis.json'
    origin = sa.read_json(origin_path)
    fixture_path = REPOSITORY_DIRECTORY/'docs/turnout-prior-prototype/fixtures-v3.json'
    fixture = sa.load_fixture(fixture_path,code)
    inputs = copy.deepcopy(fixture['inputs'])
    if code == '2025fed':
        for row in origin['enrolment_revisions']:
            inputs['enrolment'][inputs['seat_names'].index(row['seat'])] = row['election_day']
    template = origin['snapshots'][0]['units']
    if any(u['counted'] for u in template):
        raise ValueError('The allocation origin must have no counted votes.')
    draws = prior.draw(inputs,fixture['parameters'],args.samples,args.seed)
    paths,files = replay.source_paths(code,args.archive,args.downloads,origin)
    files += [origin_path,fixture_path,Path(__file__),Path(allocation.__file__),Path(allocation_model.__file__),Path(late_counts.__file__),
              Path(live.__file__),Path(prior.__file__),Path(replay.__file__),Path(progress.__file__),
              Path(category_policy.__file__),Path(sa_hindcast.__file__),Path(aec_live.__file__),Path(ppvc_sizes.__file__)]
    metadata,reviewed,missing = None,{},defaultdict(list)
    if code == '2026sa':
        truth_path = ANALYSIS_DIRECTORY/'Data/Turnout/2026sa.json'
        truth = sa.scoring_data(truth_path,inputs['seat_names'])
        _,_,_,reviewed_path = sa.booth_scoring_data(truth_path)
        static_path = reviewed_path.with_name('static.json')
        reviewed = sa_hindcast.reviewed_groups(sa.read_json(reviewed_path),sa.read_json(static_path))
        types = {'declaration_early':'Early Provisional','mobile_or_institution':'EVM',
                 'provisional':'Provisional','other':'TIO','early_in_person':'PrePoll'}
        for row in category_policy.missing_count_review(turnout_data.load_dataset(truth_path)):
            missing[row['seat']].append(types[row['category']])
        files.extend([truth_path,reviewed_path,static_path,Path(sa.__file__)])
    else:
        metadata_path = next(Path(p) for p in origin['provenance']['inputs'] if 'Preload' in p and 'polling_places' in p)
        metadata = aec_live.booth_metadata(metadata_path)
        truth = federal.scoring_data(inputs['seat_names'],inputs['categories'])
        files.extend([metadata_path,Path(federal.__file__),*truth['files']])
    variants = allocation.configurations(args.experiment)
    history,group_history,rows = [],[],[]
    sources = {replay.source_timestamp(path,code):path for path in paths}
    cached = None
    if args.history_artifact:
        if code != '2025fed':
            raise ValueError('Cached history is supported only for Federal 2025.')
        cached = cached_federal_history(args.history_artifact,sources)
        files.extend([args.history_artifact,*sources.values()])
    for position,(stamp,path) in enumerate(sorted(sources.items())):
        evaluate = stamp[:10] in args.dates if args.dates else selected(stamp,code,position)
        if cached and not evaluate:
            continue
        current = progress.read_sa(path) if code == '2026sa' else aec_live.read_house(path,metadata,event_id='31496')
        if code == '2026sa':
            current = sa_hindcast.apply_reviewed_overrides(current,reviewed,missing)
        if current['source_time'] != stamp:
            raise ValueError('Indexed and parsed source clocks differ: '+str(path))
        units = replay.attach_counts(template,current,code)
        if cached:
            history = [row for row in cached[0] if row['source_time'] <= stamp]
            group_history = [row for row in cached[1] if row['source_time'] <= stamp]
        else:
            history.append(dict(source_time=stamp,seats={n:dict(vote_types=s['vote_types']) for n,s in current['seats'].items()}))
            group_history.append(dict(source_time=stamp,seats=allocation.group_observation(units,inputs)))
        files.append(path)
        if not evaluate:
            continue
        finalised = [n for n,s in current['seats'].items() if s.get('finalised')]
        hours = (datetime.fromisoformat(stamp)-datetime(2025,5,3,18)).total_seconds()/3600 if code == '2025fed' else 0.
        decay = ppvc_sizes.reporting_decay_factor(hours) if code == '2025fed' and origin['config']['ppvc_reporting_decay'] else 1.
        broad = live.update(draws,inputs,units,finalised_seats=finalised,unreported_ppvc_factor=decay)
        subdivisions = dict(zip(inputs['seat_names'],inputs.get('subdivisions',[None]*len(inputs['seat_names']))))
        deadline = replay.POSTAL_DEADLINES.get(code)
        evidence = late_counts.progress_evidence(history,units,subdivisions,postal_deadline=deadline)
        grouped = allocation.group_evidence(group_history,units,inputs,deadline)
        own = {spread:allocation.own_additions(draws,broad,units,template,spread,args.seed)
               for spread in sorted({v['spread'] for v in variants})}
        predictions = []
        for variant in variants:
            tick = time.perf_counter()
            prepared,released = allocation.prepare(broad,inputs,units,own[variant['spread']],grouped,variant['mode'],
                                                   variant['scale'],variant.get('direction_scale',.5))
            result = late_counts.update(prepared,inputs,units,evidence,args.count_draws,args.seed)
            mean = late_counts.prediction_mean(result)
            bounds = late_counts.prediction_quantiles(result,[.025,.975])
            district_rows = [dict(seat=n,counted=int(result['counted'][i]),actual=int(truth['totals'][i]),
                mean_remaining=float(mean[i]-result['counted'][i]),low_remaining=float(bounds[0,i]-result['counted'][i]),
                high_remaining=float(bounds[1,i]-result['counted'][i]),no_addition_probability=float(result['no_addition_probability'][i]))
                for i,n in enumerate(inputs['seat_names'])]
            # Keep every declaration for explaining discrepancies after the
            # paired scores, including its unscaled estimate and release weight.
            declared = [dict(seat=u['seat_name'],name=u['name'],group=u['group'],counted=u['counted'],
                own_remainder=float(own[variant['spread']][:,j].mean()),
                current_broad_remainder=float(broad['remaining'][:,j].mean()),
                alternative_broad_remainder=float(prepared['remaining'][:,j].mean()),
                progress_release_weight=float(released[j]),mean_total=float(result['unit_means'][j]))
                for j,u in enumerate(units) if u['kind'] == 'declaration']
            predictions.append(dict(name=variant['name'],total_score=score(result,truth['totals']),
                category_score=score(result,truth['groups'],'counts'),mean_remaining=float((mean-result['counted']).mean()),
                districts=district_rows,declarations=declared,diagnostics=result['diagnostics'],
                unchanged_counted=bool(np.array_equal(result['counted'],broad['counted'])),seconds=time.perf_counter()-tick))
        rows.append(dict(source_timestamp=stamp,counted=int(broad['counted'].sum()),
                         actual_net_remaining=int(truth['totals'].sum()-broad['counted'].sum()),predictions=predictions))
        print(code+' '+stamp+' compared',flush=True)
    return dict(election=code,experiment_version=allocation.VERSION,current_model=late_counts.MODEL_VERSION,
        samples=args.samples,count_draws=args.count_draws,seed=args.seed,variants=variants,
        definitions=dict(mode='Current retains aggregate allowances; immediate uses own category estimates; gradual releases the constraint using observed whole-group progress; directional additionally limits the influence of larger own estimates smoothly.',
                         spread='Additional log-odds standard deviation for assumed finer category shares; 0.5 is a sensitivity assumption, not a fitted parameter.',
                         scale='Progress response scale in 1-exp(-(evidence/scale)^2); a larger scale retains aggregate influence longer.',
                         direction_scale='Directional mode multiplies the progress weight by logistic(-(own log odds - current log odds)/0.5) separately in each outcome. This favours reductions smoothly; 0.5 is an assumption.',
                         progress_release_weight='Whole-group progress response before the outcome-specific directional adjustment, when that adjustment applies.',
                         score='Same districts and supported groups in every comparison. Positive signed error means overprediction; upper-bound misses indicate premature confidence.'),
        postal_deadline=replay.POSTAL_DEADLINES.get(code),omitted_category_districts=truth.get('omitted',[]),
        history_observations=len(history),cached_history=str(args.history_artifact) if cached else None,
        snapshots=rows,seconds=time.perf_counter()-started,
        provenance={str(p):sa.digest(p) for p in dict.fromkeys(files)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elections',nargs='+',choices=tuple(replay.DIRECTORIES),default=list(replay.DIRECTORIES))
    parser.add_argument('--samples',type=int,default=256)
    parser.add_argument('--count-draws',type=int,default=8)
    parser.add_argument('--seed',type=int,default=20261002)
    parser.add_argument('--experiment',choices=('release','directional'),default='release')
    parser.add_argument('--dates',nargs='+',help='Evaluate only these YYYY-MM-DD dates; retain earlier source history.')
    parser.add_argument('--history-artifact',type=Path,help='Reuse verified raw history from a Federal 2025 shadow artifact.')
    parser.add_argument('--archive',type=Path,default=Path('F:/Election Data/AEC media feed archive'))
    parser.add_argument('--downloads',type=Path,default=Path.home()/'Downloads')
    parser.add_argument('--output',type=Path,default=REPOSITORY_DIRECTORY/'downloads/turnout/allocation-experiment')
    parser.add_argument('--dry-run',action='store_true')
    args = parser.parse_args()
    if args.samples < 2 or args.count_draws < 1:
        parser.error('Use at least two preparation outcomes and one count draw.')
    for code in args.elections:
        result = run(code,args)
        if not args.dry_run:
            args.output.mkdir(parents=True,exist_ok=True)
            filename = f'{code}-comparison.json' if args.experiment == 'release' else f'{code}-directional.json'
            (args.output/filename).write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
        print(json.dumps(dict(election=code,evaluated_snapshots=len(result['snapshots']),history_observations=result['history_observations'],seconds=result['seconds'],dry_run=args.dry_run)),flush=True)


if __name__ == '__main__':
    main()

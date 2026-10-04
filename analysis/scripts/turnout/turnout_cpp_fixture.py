"""Export C++ shadow priors and small source-backed Python comparison fixtures.

The exporter reads existing frozen no-results allocations and prior parameters;
it does not fit a model. Archived exact counts provide snapshot inputs. Final
results do not select priors, progress evidence or expected comparison outputs.
"""

import argparse
import copy
from datetime import datetime
import json
from pathlib import Path

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY
from lib.turnout import aec_live, allocation, cpp_contract, ppvc_sizes, prior
from scripts.turnout import turnout_declaration_progress as progress
from scripts.turnout import turnout_late_batch as replay
from scripts.turnout import turnout_live_prototype as sa

SELECTED = {'2026sa':['Croydon','Flinders','Giles','Stuart','Taylor','Kavel'],
            '2025fed':['Gilmore','Watson','Bullwinkel','Leichhardt','Kennedy','Boothby','Brand','Calwell']}


def write(path, payload):
    """Save a complete reproducible artifact, rejecting non-JSON numeric values."""
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(payload,ensure_ascii=False,allow_nan=False,separators=(',',':'))+'\n',encoding='utf-8')


def subset(inputs, draws, units, names):
    """Reduce only comparison fixtures; normal shadow priors keep every district.

    The selected districts retain their original prior outcomes, including their
    common relationships. Progress pooling is recomputed within this fixture's
    stated comparison population, rather than claiming full replay scores.
    """
    positions = [inputs['seat_names'].index(name) for name in names]
    small = dict(seat_names=names,categories=inputs['categories'],
                 enrolment=[inputs['enrolment'][i] for i in positions],
                 subdivisions=[inputs.get('subdivisions',[None]*len(inputs['seat_names']))[i] for i in positions])
    values = dict(totals=draws['totals'][:8,positions], counts=draws['counts'][:8,positions,:])
    selected = [dict(u,seat_index=names.index(u['seat_name'])) for u in units if u['seat_name'] in names]
    return small,values,selected


def export(code,args):
    """Separate raw source replay from retrospective category repairs.

    SA comparison inputs intentionally use the raw feed. Croydon/Taylor's
    reviewed relabellings are hindsight repairs and must not silently enter the
    GUI's real-time shadow path. Existing explicit loader corrections still
    apply through the source readers, as in the Python count baseline.
    """
    directory = REPOSITORY_DIRECTORY/'docs'/replay.DIRECTORIES[code]
    origin_path = directory/'analysis.json'
    origin = sa.read_json(origin_path)
    fixture_path = REPOSITORY_DIRECTORY/'docs/turnout-prior-prototype/fixtures-v3.json'
    fixture = sa.load_fixture(fixture_path,code)
    inputs = copy.deepcopy(fixture['inputs'])
    if code == '2025fed':
        for row in origin['enrolment_revisions']:
            inputs['enrolment'][inputs['seat_names'].index(row['seat'])] = row['election_day']
    draws = prior.draw(inputs,fixture['parameters'],args.samples,args.seed)
    template = origin['snapshots'][0]['units']
    if any(u['counted'] for u in template):
        raise ValueError('Frozen origin contains counted votes.')
    metadata = None
    if code == '2025fed':
        metadata = aec_live.booth_metadata(next(Path(p) for p in origin['provenance']['inputs'] if 'Preload' in p and 'polling_places' in p))
    paths,_ = replay.source_paths(code,args.archive,args.downloads,origin)
    sources = {replay.source_timestamp(p,code):p for p in paths}
    history,observations = [],[]
    for stamp,path in sorted(sources.items()):
        current = progress.read_sa(path) if code == '2026sa' else aec_live.read_house(path,metadata,event_id='31496')
        # Store measured whole groups as well as declaration categories. SA's
        # mixed early group includes PPVC booths, which cannot be recovered by
        # dividing the feed's combined Ordinary total after the fact.
        units = replay.attach_counts(template,current,code)
        grouped = allocation.group_observation(units,inputs)
        history.append(dict(source_time=stamp,seats={name:dict(vote_types={**seat['vote_types'],**grouped[name]['vote_types']})
                                                   for name,seat in current['seats'].items()}))
        observations.append((stamp,path,current))
    fingerprints = dict(origin_sha256=sa.digest(origin_path),prior_fixture_sha256=sa.digest(fixture_path),
        inputs={Path(p).name:digest for p,digest in origin['provenance']['inputs'].items()},sources={stamp:dict(file=path.name,sha256=sa.digest(path)) for stamp,path,_ in observations},
        code={Path(p).name:sa.digest(Path(p)) for p in (cpp_contract.__file__,prior.__file__,replay.live.__file__,replay.late_counts.__file__,allocation.__file__)},
        input_treatment='Raw live feed; no final-derived SA category repairs.')
    decay = code == '2025fed' and origin['config']['ppvc_reporting_decay']
    poll_close = '2025-05-03T18:00:00' if code == '2025fed' else None
    artifact = cpp_contract.artifact(code,inputs,fixture['parameters'],draws,template,history,fingerprints,
        count_draws=args.count_draws,seed=args.seed,postal_deadline=replay.POSTAL_DEADLINES.get(code),poll_close=poll_close,decay=decay)
    write(args.output/f'{code}-shadow.json',artifact)
    # A complete-election checkpoint measures C++ count preparation at the
    # intended sample size, with Python means as a compact parity reference.
    # This is an ignored local replay input, separate from the frozen origin.
    stamp,path,current = observations[-1]
    full_units = replay.attach_counts(template,current,code)
    full_finalised = [n for n,s in current['seats'].items() if s.get('finalised')]
    full_factor = ppvc_sizes.reporting_decay_factor((datetime.fromisoformat(stamp)-datetime.fromisoformat(poll_close)).total_seconds()/3600) if decay else 1.
    reference = cpp_contract.expected(code,inputs,draws,full_units,history,count_draws=args.count_draws,seed=args.seed,
        finalised=full_finalised,ppvc_factor=full_factor,postal_deadline=replay.POSTAL_DEADLINES.get(code))
    write(args.output/f'{code}-checkpoint.json',dict(source_time=stamp,units=cpp_contract.unit_rows(full_units),
        finalised=full_finalised,ppvc_factor=full_factor,
        expected_mean_totals=reference['mean_totals'],expected_unit_means=reference['unit_means']))
    names = SELECTED[code]
    small,values,small_template = subset(inputs,draws,template,names)
    # Four checkpoints cover no results, an early partial return, the postal
    # window and a late return. An additional authoritative-finalisation case
    # checks a flag pathway without assuming that a quiet feed is finalised.
    selected = sorted(set([0,1,len(observations)//2,len(observations)-1]))
    if code == '2025fed':
        # Retain the two checkpoints that exposed the original PPVC/EAV
        # allocation failure and the long pre-deadline postal pause. Repeating
        # two empty early feeds would not exercise those important paths.
        targets = ('2025-05-03T19:59:47','2025-05-04T01:01:19','2025-05-11T21:50:01')
        selected = [next(i for i,(stamp,_,_) in enumerate(observations) if stamp == target) for target in targets]
        selected.append(len(observations)-1)
    cases = []
    # Include a genuine empty account even where the source archive starts
    # after voting begins. This uses the frozen origin, not fabricated zeros
    # in place of unavailable historical measurements.
    cases.append(dict(name=code+'/frozen-no-results',source_time=observations[0][0],source_sha256=None,
        units=cpp_contract.unit_rows(small_template),history=[],finalised=[],ppvc_factor=1.,
        expected=cpp_contract.expected(code,small,values,small_template,[],count_draws=args.count_draws,seed=args.seed)))
    for index in selected:
        stamp,path,current = observations[index]
        units = replay.attach_counts(template,current,code)
        _,_,small_units = subset(inputs,draws,units,names)
        small_history = [dict(source_time=h['source_time'],seats={n:h['seats'][n] for n in names}) for h in history[:index+1]]
        finalised = [n for n in names if current['seats'][n].get('finalised')]
        factor = ppvc_sizes.reporting_decay_factor((datetime.fromisoformat(stamp)-datetime.fromisoformat(poll_close)).total_seconds()/3600) if decay else 1.
        case = dict(name=code+'/'+stamp,source_sha256=sa.digest(path),source_time=stamp,
                    units=cpp_contract.unit_rows(small_units),history=small_history,finalised=finalised,ppvc_factor=factor)
        case['expected'] = cpp_contract.expected(code,small,values,small_units,small_history,count_draws=args.count_draws,
            seed=args.seed,finalised=finalised,ppvc_factor=factor,postal_deadline=replay.POSTAL_DEADLINES.get(code))
        cases.append(case)
    # The fixture is a self-contained, small subset of the exported election
    # contract. Sensitivity summaries are not used by the portable count test.
    artifact['prior'] = dict(seats=names,groups=small['categories'],subdivisions=[s or '' for s in small['subdivisions']],
                             enrolment=small['enrolment'],totals=values['totals'].tolist(),counts=values['counts'].reshape(8,-1).tolist())
    artifact['units'] = cpp_contract.unit_rows(small_template)
    artifact['history'] = []; artifact['sensitivities'] = {}
    artifact['cases'] = cases
    write(args.fixtures/f'{code}-turnout-v1.json',artifact)
    return dict(election=code,full_districts=len(inputs['seat_names']),comparison_districts=len(names),
                source_observations=len(history),comparison_cases=len(cases),preparation_samples=args.samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--election',choices=['2026sa','2025fed','both'],default='both')
    parser.add_argument('--samples',type=int,default=256)
    parser.add_argument('--count-draws',type=int,default=8)
    parser.add_argument('--seed',type=int,default=20261002)
    parser.add_argument('--archive',type=Path,default=Path('F:/Election Data/AEC media feed archive'))
    parser.add_argument('--downloads',type=Path,default=Path.home()/'Downloads')
    parser.add_argument('--output',type=Path,default=REPOSITORY_DIRECTORY/'downloads/turnout/cpp-shadow')
    parser.add_argument('--fixtures',type=Path,default=REPOSITORY_DIRECTORY/'tests/fixtures/turnout')
    args = parser.parse_args()
    if args.samples < 8:
        parser.error('At least eight preparation samples are needed for comparison fixtures.')
    for code in (['2026sa','2025fed'] if args.election == 'both' else [args.election]):
        print(json.dumps(export(code,args)))


if __name__ == '__main__':
    main()

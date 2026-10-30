"""Generate a portable count regression using entirely fictional vote counts.

No feed, retained election dataset or calibrated prior output is read. Invented
districts and booth sizes exercise completion, PPVC/EAV allocation, declaration
progress, receipt deadlines and explicit finalisation. Python supplies expected
outputs so the portable C++ suite can check the same calculations after cloning.
"""

import argparse
import copy
import json
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY
from lib.turnout import allocation, cpp_contract, late_counts, late_schedule, live, prior


def make_fixture():
    """Build arbitrary counts, rather than disguise or rescale real feed rows."""
    names = ['Example East', 'Example West', 'Example Rural']
    groups = ['ordinary', 'early', 'postal', 'remainder']
    inputs = dict(seat_names=names, categories=groups, enrolment=[28000, 30000, 26000],
                  subdivisions=['Example state', 'Example state', 'Another state'])
    base = np.array([[7200., 9400., 2800., 2100.],
                     [8300., 10200., 3200., 2300.],
                     [6800., 8600., 2500., 1900.]])
    # Opposing category changes and shared total changes provide a nontrivial
    # joint prior. These multipliers are hand-selected test data, not a fit.
    factors = np.array([[.94, 1.03, .92, 1.05], [1.04, .97, 1.08, .93],
                        [1.01, 1.06, .96, .98], [.98, .95, 1.03, 1.07]])
    counts = base[None, :, :] * factors[:, None, :]
    draws = dict(counts=counts, totals=counts.sum(axis=2))
    units = []
    specs = [('Ordinary A', 0, 'ordinary', .54, True, False),
             ('Ordinary B', 0, 'ordinary', .45, True, False),
             ('PPVC A', 1, 'ppvc', .46, True, False),
             ('PPVC B', 1, 'ppvc', .39, True, False),
             ('New PPVC', 1, 'ppvc', .14, False, False),
             ('EAV service', 1, 'ppvc', .01, False, True),
             ('Postal', 2, 'declaration', 1., True, False),
             ('Absent', 3, 'declaration', .6, True, False),
             ('PrePoll', 3, 'declaration', .4, True, False),
             ('Provisional', 0, 'declaration', .01, True, False)]
    for seat, name in enumerate(names):
        for label, group, kind, weight, matched, eav in specs:
            units.append(dict(seat_index=seat, seat_name=name, group_index=group,
                              group=groups[group], name=label, kind=kind, weight=weight,
                              counted=0., matched=matched, is_eav=eav))
    election = '2025fed'  # Reuse the published timetable; all counts are fictional.
    seed = 12345
    artifact = dict(schema_version=cpp_contract.SCHEMA_VERSION, election=election,
                    prior_model=prior.MODEL_VERSION, count_model=live.MODEL_VERSION,
                    progress_model=late_counts.MODEL_VERSION, allocation_model=allocation.MODEL_VERSION,
                    prior=dict(seats=names, groups=groups, subdivisions=inputs['subdivisions'],
                               enrolment=inputs['enrolment'], totals=draws['totals'].tolist(),
                               counts=counts.reshape(4, -1).tolist()),
                    units=cpp_contract.unit_rows(units), history=[], sensitivities={},
                    provenance=dict(kind='wholly_synthetic', generator='turnout_synthetic_fixture.py',
                                    description='Hand-selected fictional counts; no electoral feed is read.'),
                    options=dict(count_draws=4, seed=seed, postal_deadline=None,
                                 receipt_deadline=late_schedule.RECEIPT_DEADLINES[election],
                                 declaration_schedule_model=late_schedule.MODEL_VERSION,
                                 poll_close='2025-05-03T18:00:00', ppvc_reporting_decay=True))
    history, cases = [], []
    snapshots = [('2025-05-03T18:00:00', 'no-results', 0, 1.),
                 ('2025-05-03T22:00:00', 'ordinary-and-one-ppvc', 1, 1.),
                 ('2025-05-15T20:00:00', 'declarations-before-deadline', 2, .1),
                 ('2025-05-18T20:00:00', 'processing-after-deadline', 3, .02),
                 ('2025-05-24T20:00:00', 'quiet-late-count', 3, .02)]
    for stamp, label, stage, factor in snapshots:
        current = copy.deepcopy(units)
        for u in current:
            s, g = u['seat_index'], u['group_index']
            expected = base[s, g] * u['weight']
            if stage >= 1 and (u['kind'] == 'ordinary' or u['name'] == 'PPVC A'):
                u['counted'] = round(expected * (.9 + .04 * s))
            if stage >= 2 and u['name'] == 'PPVC B':
                u['counted'] = round(expected * .95)
            if stage >= 2 and u['kind'] == 'declaration':
                u['counted'] = round(expected * (.63 if stage == 2 else .92))
            if stage >= 3 and u['name'] == 'EAV service':
                u['counted'] = 19 + s
        observation = dict(source_time=stamp, seats={name: dict(vote_types={}) for name in names})
        for u in current:
            types = observation['seats'][u['seat_name']]['vote_types']
            category = late_counts.category_key(u)
            types[category] = types.get(category, 0) + u['counted']
        # Include received aggregate progress as well as service counts. This
        # checks that quiet mixed groups cannot release/reinflate service sizes.
        grouped = allocation.group_observation(current, inputs)
        for name, row in grouped.items():
            observation['seats'][name]['vote_types'].update({
                key: value for key, value in row['vote_types'].items() if key.startswith('allocation:')})
        history.append(observation)
        expected = cpp_contract.expected(election, inputs, draws, current, history,
                                         count_draws=4, seed=seed, ppvc_factor=factor,
                                         poll_close=artifact['options']['poll_close'])
        cases.append(dict(name=label, source_time=stamp, source_sha256=None,
                          units=cpp_contract.unit_rows(current), history=copy.deepcopy(history),
                          finalised=[], ppvc_factor=factor, expected=expected))
    # A few small revisions while below the original expectation exercise the
    # positive-remainder sampler, rather than only its zero-weight boundary.
    # Keep other categories fixed so cross-language parity covers their isolation.
    current = copy.deepcopy(current)
    for u in current:
        if u['name'] == 'Absent':
            u['counted'] = round(base[u['seat_index'],u['group_index']]*.4)
    local_history = []
    start = datetime(2025,5,10)
    for step in range(17):
        stamp = (start+timedelta(hours=6*step)).isoformat()
        observation = dict(source_time=stamp,seats={name:dict(vote_types={}) for name in names})
        for u in current:
            count = u['counted']
            if u['name']=='Absent' and u['seat_index']==0 and step==16:
                count += 21
                u['counted'] = count
            observation['seats'][u['seat_name']]['vote_types'][late_counts.category_key(u)] = count
        local_history.append(observation)
    expected = cpp_contract.expected(election,inputs,draws,current,local_history,count_draws=4,seed=seed,ppvc_factor=.02,
        poll_close=artifact['options']['poll_close'])
    assert max(c['positive_remainder_weight'] for c in expected['components']) > .1
    cases.append(dict(name='small-revision-positive-remainder',source_time=stamp,source_sha256=None,
        units=cpp_contract.unit_rows(current),history=local_history,finalised=[],ppvc_factor=.02,expected=expected))
    # Identical counts and pauses earlier in the election must not imply the
    # same completion confidence when still far below their original sizes.
    young_history = copy.deepcopy(local_history)
    for row in young_history:
        row['source_time'] = (datetime.fromisoformat(row['source_time'])-timedelta(days=7)).isoformat()
    expected = cpp_contract.expected(election,inputs,draws,current,young_history,count_draws=4,seed=seed,ppvc_factor=.02,
        poll_close=artifact['options']['poll_close'])
    assert min(c['early_completion_retention'] for c in expected['components']) < .1
    cases.append(dict(name='early-pause-below-expectation',source_time=young_history[-1]['source_time'],source_sha256=None,
        units=cpp_contract.unit_rows(current),history=young_history,finalised=[],ppvc_factor=.02,expected=expected))
    # An explicitly separate postal timetable exercises processing-sensitive
    # quietness without moving receipt safeguards or other categories' clocks.
    postal_timing = {'Postal': dict(days_after_poll_close=13,transition_days=2)}
    expected = cpp_contract.expected(election,inputs,draws,current,local_history,count_draws=4,seed=seed,ppvc_factor=.02,
        poll_close=artifact['options']['poll_close'],processing_overrides=postal_timing)
    cases.append(dict(name='separate-postal-processing',source_time=local_history[-1]['source_time'],source_sha256=None,
        units=cpp_contract.unit_rows(current),history=local_history,finalised=[],ppvc_factor=.02,
        processing_timetables=postal_timing,expected=expected))
    artifact['cases'] = cases
    return artifact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=REPOSITORY_DIRECTORY / 'tests/fixtures/turnout/synthetic-turnout-v1.json')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(make_fixture(), indent=2, allow_nan=False) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

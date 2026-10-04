"""Translate frozen count inputs into the portable C++ shadow contract.

Fitting remains in Python. The contract carries immutable count outcomes,
separate shared count sensitivities, source identities and exact unit metadata.
Current counts and source history are separate from the frozen prior account.
"""

import numpy as np
from lib.turnout import allocation, late_counts, live, prior

SCHEMA_VERSION = 1
MASK = (1 << 64)-1


def unit_rows(units):
    """Export only the fields the count updater uses; unknowns stay absent."""
    return [dict(seat=u['seat_index'], group=u['group_index'], name=u['name'],
                 category=late_counts.category_key(u), kind=u['kind'], weight=u['weight'],
                 counted=u['counted'], matched=u['matched'], eav=live.is_eav(u),
                 closed=bool(u.get('closed_reason')), excluded=bool(u.get('calibration_exclusion_reason')),
                 closure_reason=u.get('closed_reason') or '', exclusion_reason=u.get('calibration_exclusion_reason') or '')
            for u in units]


def quantiles(election, inputs, units, complete, outcomes, seed):
    """Reproduce C++'s stable Fisher-Yates permutation separately for each unit.

    Every unit receives all midpoint quantiles exactly once. Keying by identity
    keeps an omitted missing category from changing another category's draws.
    This is a comparison/simulation generator, not a source of fitted parameters.
    """
    columns = [j for j,u in enumerate(units) if u['kind'] == 'declaration' and not complete[j]]
    result = np.empty((outcomes, len(columns)))
    for column,j in enumerate(columns):
        key = election+'/'+inputs['seat_names'][units[j]['seat_index']]+'/'+units[j]['name']
        state = 14695981039346656037
        for byte in key.encode('utf-8'):
            state = ((state ^ byte)*1099511628211) & MASK
        state ^= seed
        positions = list(range(outcomes))
        for n in range(outcomes, 1, -1):
            state = (state+0x9e3779b97f4a7c15) & MASK
            value = state
            value = ((value ^ (value >> 30))*0xbf58476d1ce4e5b9) & MASK
            value = ((value ^ (value >> 27))*0x94d049bb133111eb) & MASK
            value ^= value >> 31
            position = value % n
            positions[n-1], positions[position] = positions[position], positions[n-1]
        result[:,column] = (np.asarray(positions)+.5)/outcomes
    return result


def artifact(election, inputs, parameters, draws, units, history, provenance, *, count_draws=8, seed=20261002,
             postal_deadline=None, poll_close=None, decay=False):
    """Keep frozen distributions and later source observations distinguishable."""
    return dict(schema_version=SCHEMA_VERSION, election=election, prior_model=prior.MODEL_VERSION,
        count_model=live.MODEL_VERSION, progress_model=late_counts.MODEL_VERSION,allocation_model=allocation.MODEL_VERSION,
        prior=dict(seats=inputs['seat_names'], groups=inputs['categories'],
                   subdivisions=[s or '' for s in inputs.get('subdivisions',[None]*len(inputs['seat_names']))],
                   enrolment=inputs['enrolment'], totals=draws['totals'].tolist(),
                   counts=draws['counts'].reshape(len(draws['totals']),-1).tolist()),
        units=unit_rows(units), history=history, provenance=provenance,
        sensitivities=prior.prepare_responses(inputs,parameters),
        options=dict(count_draws=count_draws, seed=seed, postal_deadline=postal_deadline,
                     poll_close=poll_close, ppvc_reporting_decay=decay))


def expected(election, inputs, draws, units, history, *, count_draws=8, seed=20261002,
             finalised=(), ppvc_factor=1., postal_deadline=None):
    """Prepare the Python reference account on the C++ quantile grid."""
    broad = live.update(draws,inputs,units,finalised_seats=finalised,unreported_ppvc_factor=ppvc_factor)
    subdivisions = dict(zip(inputs['seat_names'],inputs.get('subdivisions',[None]*len(inputs['seat_names']))))
    evidence = late_counts.progress_evidence(history,units,subdivisions,postal_deadline=postal_deadline)
    balanced_means = broad['remaining'].mean(axis=0)
    grouped = allocation.group_evidence(history,units,inputs,postal_deadline)
    broad,weights = allocation.release(broad,inputs,units,broad['own_remaining'],grouped)
    grid = quantiles(election,inputs,units,broad['complete'],len(draws['totals'])*count_draws,seed)
    result = late_counts.update(broad,inputs,units,evidence,count_draws,seed,uniforms=grid)
    components = []
    prepared = result['component_preparation']
    for k,c in enumerate(result['components']):
        components.append(dict(unit=c['unit'],weight=c['weight'],
            probabilities=c['probabilities'],
            small_mean=c['small_mean_if_other_counts_fixed'],batch_mean=c['batch_mean_if_other_counts_fixed'],
            capacity=prepared['unused_capacity'][:,k].tolist(), reference=prepared['reference_additions'][:,k].tolist(),
            reference_odds=prepared['reference_log_odds'][:,k].tolist(),small_odds=prepared['small_log_odds'][:,k].tolist(),
            batch_odds=prepared['batch_log_odds'][:,k].tolist()))
    return dict(broad_totals=broad['totals'].tolist(),broad_counts=broad['counts'].reshape(len(draws['totals']),-1).tolist(),
        remaining=broad['remaining'].tolist(),own_remaining=broad['own_remaining'].tolist(),
        balanced_remaining_means=balanced_means.tolist(),allocation_weight=weights.tolist(),
        allocation_evidence=[grouped.get((u['seat_name'],'Postal' if u['group']=='postal' else 'allocation:'+u['group']),0.)
                             if u['kind']=='declaration' and not broad['complete'][j] else 0. for j,u in enumerate(units)],
        totals=result['totals'].tolist(),
        counts=result['counts'].reshape(len(result['totals']),-1).tolist(),unit_means=result['unit_means'].tolist(),
        no_addition_probability=result['no_addition_probability'].tolist(),mean_totals=late_counts.prediction_mean(result).tolist(),
        evidence=[e['strength'] for e in evidence],components=components)

"""Prepare expected additions separately from exceptional declaration batches.

An explicit receipt deadline supplies a timetable, not a completion flag.
Each snapshot starts from immutable priors and the observed count history.
Routine additions recede over days, while exceptional possibilities fade over
weeks. These helpers reproduce the selected offline experiment for C++ tests.
"""
from datetime import datetime, timedelta
from functools import lru_cache

import numpy as np
from scipy.special import expit

from lib.turnout import allocation, late_counts, live

MODEL_VERSION = 'live-declaration-schedule-1'
RECEIPT_DEADLINES = {'2025fed':'2025-05-17T00:00:00', '2026sa':'2026-03-28T18:00:00'}


@lru_cache(maxsize=4096)
def counting_hours(before, after):
    """Measure processing time continuously, giving Sundays half influence."""
    if after<before:
        return -counting_hours(after,before)
    total = 0.
    while before<after:
        boundary = min(after,before.replace(hour=0,minute=0,second=0,microsecond=0)+timedelta(days=1))
        total += (.5 if before.weekday()==6 else 1.)*(boundary-before).total_seconds()/3600
        before = boundary
    return total


def booth_completion(draws, units, seats):
    """Weight reported booths by expected size, excluding known closures."""
    total,reported = np.zeros(seats),np.zeros(seats)
    original = np.stack([draws['counts'][:,u['seat_index'],u['group_index']]*u['weight'] for u in units],axis=1)
    expected = live.eav_starting_counts(original,units).mean(axis=0)
    for j,u in enumerate(units):
        if u['kind']=='declaration' or u.get('closed_reason'):
            continue
        total[u['seat_index']] += expected[j]
        if u['counted']>0:
            reported[u['seat_index']] += expected[j]
    return np.divide(reported,total,out=np.zeros_like(total),where=total>0)


def allocation_evidence(grouped, units, completed, prepared):
    """Use completed booths to relax independently supported category allowances.

    One actual declaration category per historical group is required. Mixed
    groups retain their existing release rule. This avoids inventing evidence
    for finer category splits that old election records cannot support.
    """
    result = dict(grouped)
    groups = {}
    for j,u in enumerate(units):
        groups.setdefault((u['seat_index'],u['group_index']),[]).append(j)
    for u in units:
        if u['kind']!='declaration':
            continue
        key = (u['seat_name'],'Postal' if u['group']=='postal' else 'allocation:'+u['group'])
        old = -np.expm1(-(grouped.get(key,0.)/allocation.PROGRESS_SCALE)**2)
        columns = groups[u['seat_index'],u['group_index']]
        compatible = all(units[j]['kind']=='declaration' for j in columns) and len({late_counts.category_key(units[j]) for j in columns})==1
        counted = sum(units[j]['counted'] for j in columns)
        own = float(prepared['own_remaining'][:,columns].sum(axis=1).mean())
        maturity = counted/(counted+own) if counted+own else 0.
        extra = .5*completed[u['seat_index']]**2*maturity**2 if compatible else 0.
        weight = old+(1-old)*extra
        result[key] = allocation.PROGRESS_SCALE*np.sqrt(-np.log1p(-weight))
    return result


def progress_evidence(history, units, states, deadline, completed):
    """Blend cautious activity responses and support from small late batches.

    Two change-size measurements are blended in probability space. A backlog
    after the deadline continues to influence its own seat but progressively
    loses influence elsewhere. Final published counts never enter this step.
    """
    old = late_counts.progress_evidence(history,units,states,postal_deadline=deadline,
        counting_clock=counting_hours,event_scale=.001,shared_deadline=deadline)
    result = late_counts.progress_evidence(history,units,states,postal_deadline=deadline,
        counting_clock=counting_hours,event_scale=.02,shared_deadline=deadline)
    quiet = lambda value,scale: 1/(1+(value/scale)**2)
    for before,e,u in zip(old,result,units):
        if 'current' not in e:
            continue
        pooled = e['pooled']
        shared = quiet(pooled['activity'],.3)
        strength = e['coverage']*e['started']*quiet(1-pooled['started'],.1)*quiet(e['activity'],.3)*(.5+.5*shared)*e['postal_receipt_support']
        proposed = -np.expm1(-(strength/.3)**2)*shared
        weight = .5*(-np.expm1(-(before['strength']/.3)**2)+proposed)
        extra = .8*e['small_batch_support']*completed[u['seat_index']]**2*e['started']
        weight += (1-weight)*extra
        e['strength'] = float(.3*np.sqrt(-np.log1p(-weight)))
    return result


def schedule(history, units, evidence, deadline):
    """Return routine-size shifts and a separately fading batch probability.

    Allow 48 counting hours after receipt closes, with a 12-hour transition.
    Local counting can extend that allowance by up to 24 hours and retain some
    routine size. Two-percent recent category volume supplies half the delay;
    five percent supplies half the ongoing-activity retention. Batch probability
    has a fourteen-day exponential timescale, not a permanent probability floor.
    """
    shifts = np.zeros(len(units))
    if not history or not sum(u['counted'] for u in units):
        return dict(processing_phase=0.,batch_survival=1.,counting_hours_after_deadline=0.,usual_log_shifts=shifts)
    now = max(datetime.fromisoformat(h['source_time']) for h in history)
    hours = counting_hours(datetime.fromisoformat(deadline),now)
    age = 12*np.logaddexp(0.,(hours-48)/12)
    for j,(u,e) in enumerate(zip(units,evidence)):
        if u['kind']!='declaration':
            continue
        relative = e.get('volume',0.)/(e.get('current',u['counted'])+.5)
        delay = 24*relative**2/(relative**2+.02**2)
        routine = expit(-(hours-48-delay)/12)
        active = relative**2/(relative**2+.05**2)
        shifts[j] = np.log(routine+(1-routine)*active)
    return dict(processing_phase=float(expit((hours-48)/12)),batch_survival=float(np.exp(-age/(14*24))),
        counting_hours_after_deadline=hours,usual_log_shifts=shifts)

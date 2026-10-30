"""Separate expected counting progress from legal postal receipt dates.

Processing dates are calendar offsets from polling close, normally informed by
the previous comparable election. The defaults are modelling assumptions, not
legal deadlines: absent/declaration pre-poll usually enter a mostly-finished
phase around day 14, and provisionals around day 18. Election overrides can
change these dates or disable a category with null. Unknown dates/categories
retain activity-based evidence without time-based suppression. Postal timing
keeps its legacy receipt-based allowance unless an explicit processing entry is
supplied; that entry does not change the separate receipt safeguards.
"""
from datetime import datetime, timedelta
import math


DEFAULTS = {'Absent': 14., 'PrePoll': 14., 'Provisional': 18.}
TRANSITION_DAYS = 2.


def validate(overrides):
    """Reject ambiguous category names and non-finite or non-positive widths."""
    if not isinstance(overrides, dict):
        raise ValueError('Processing timetables must be a category object.')
    for category, row in overrides.items():
        if not isinstance(category, str) or not category.strip():
            raise ValueError('Processing timetables require non-empty category names.')
        if row is None:
            continue
        if not isinstance(row, dict):
            raise ValueError('A processing timetable must be an object or null.')
        days, width = row.get('days_after_poll_close'), row.get('transition_days', TRANSITION_DAYS)
        if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
               for v in (days, width)) or days < 0 or width <= 0:
            raise ValueError('Processing days must be finite and nonnegative, with a positive transition width.')


def resolve(poll_close=None, receipt_deadline=None, overrides=None):
    """Return one clock per category, retaining unconfigured postal timing.

    The non-postal date centres a smooth decline: a quiet category retains half
    its routine remaining allowance there. Two days is the logistic scale, not
    a hard completion window. Receipt safeguards remain exclusively postal.
    An explicit Postal entry also moderates the support for nearly finished
    counting until processing matures. Early receipt closure does not establish
    that most accepted envelopes have been processed. Physical early-voting
    places are booths and do not use the PrePoll entry.
    """
    overrides = {} if overrides is None else overrides
    validate(overrides)
    settings = {key: dict(days_after_poll_close=days) for key, days in DEFAULTS.items()}
    settings.update(overrides)
    result = {}
    if poll_close is not None:
        close = datetime.fromisoformat(poll_close)
        for category, row in settings.items():
            if row is None:
                continue
            width = 24*row.get('transition_days', TRANSITION_DAYS)
            result[category] = dict(reference=close+timedelta(days=row['days_after_poll_close']),
                allowance_hours=0., transition_hours=width, shared_transition_hours=width,
                small_batch_transition_hours=width, counting_clock=False)
            if category == 'Postal':
                result[category]['separate_postal_processing'] = True
    if receipt_deadline is not None and 'Postal' not in overrides:
        result['Postal'] = dict(reference=datetime.fromisoformat(receipt_deadline),
            allowance_hours=48., transition_hours=12., shared_transition_hours=24.,
            small_batch_transition_hours=12., counting_clock=True)
    return result

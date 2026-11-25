"""Decide whether observed count changes warrant paid commentary.

This is an editorial filter, not part of the election model. Forecasts and full
mechanical comparisons are still exported. Candidate corrections matter even
when a booth's total or the seat's winning chances do not change.
"""


def affected_ballots(before, after):
    """Moving ten votes between candidates affects ten ballots even though
    both candidates' counts change. A change in turnout can also be one-sided."""
    differences = [after.get(p, 0)-before.get(p, 0) for p in before.keys() | after.keys()]
    return max(sum(max(0, v) for v in differences), sum(max(0, -v) for v in differences))


def first_tcp_size(old, new):
    """Return the sample size of a first positive count for these opponents.
    Even if all ballots went to the continuing candidate, counting them against
    a different opponent is new evidence. Two names with zero votes are not."""
    before, after = old.get('tcp_votes_current', {}), new.get('tcp_votes_current', {})
    if before == after or len(after) != 2 or min(after.values()) < 0 or not sum(after.values()):
        return None
    def identities(booth, tally):
        aliases = booth.get('count_to_projection_ids', {})
        return {aliases.get(p, p) for p in tally}
    pair = identities(new, after)
    if len(pair) == 2 and (not sum(before.values()) or (before.keys() != after.keys() and identities(old, before) != pair)):
        return sum(after.values())
    return None


def count_changes(previous, current):
    if not previous.get('observed_counts_available') or not current.get('observed_counts_available'):
        return {'known': False}
    changed = 0
    # Administrative 3CP/4CP samples can add real counting information without
    # any new FP/TCP feed totals. Include their published vote allocations, not
    # changing model draw statistics or an operator's uncertainty settings.
    sources = [('booths', ('fp_votes_current', 'tcp_votes_current')),
               ('manual_count_observations', ('transferred_votes', 'sampled_first_preferences', 'sampled_candidate_totals'))]
    for source, fields in sources:
        old, new = previous.get(source, {}), current.get(source, {})
        for key in old.keys() | new.keys():
            accounts = []
            for field in fields:
                before, after = old.get(key, {}).get(field, {}), new.get(key, {}).get(field, {})
                accounts.append(affected_ballots(before, after))
            if source == 'booths':
                first_count = first_tcp_size(old.get(key, {}), new.get(key, {}))
                if first_count is not None:
                    accounts.append(first_count)
            # Accounts at a booth usually describe the same ballots. Their
            # maximum avoids adding FP and TCP twice. Separate manual counts
            # contribute additional observed allocations, not extra turnout.
            changed += max(accounts)
    return {'known': True, 'changed_votes': changed}


def decision(group, threshold=10):
    if not group.get('available'):
        return 'unavailable'
    counts = group.get('count_activity', {})
    if not counts.get('known'):
        return 'request'  # Missing diagnostics cannot establish that nothing changed.
    votes = counts['changed_votes']
    # A published TCP can reset across a district while the forecast keeps
    # using the same earlier booth counts. That change in reported progress
    # warrants an explanation even when the usable model inputs barely move.
    reporting = group.get('tcp_reporting_changes', {})
    votes = max(votes, reporting.get('votes_for_activity_threshold', 0))
    if votes == 0:
        return 'no_new_counting'
    if group.get('noteworthy_booth_warnings'):
        return 'request'
    if votes < threshold and group.get('seat_call_changes'):
        return 'call_update'
    return 'minor_changes' if votes < threshold else 'request'


QUIET_RESULTS = 'There were no significant changes to the results'


def comparison_period(window, seconds):
    # Rolling comparisons can span longer than their nominal window. The text
    # uses the actual interval; reference IDs/times remain in the comparison.
    if window == 'previous':
        return 'since the previous forecast'
    if window == 'since_start':
        return 'since counting began'
    if window == 'today':
        return "since the forecast used as today's starting point"
    if seconds is None:
        return 'over this comparison period'
    return f'over the {seconds / 60:g}-minute comparison period'


def notice(group, window):
    kind = group.get('commentary_decision')
    if kind not in ('no_new_counting', 'minor_changes', 'call_update'):
        return None
    period = comparison_period(window, group.get('actual_seconds'))
    text = (f'No new counting occurred {period}.' if kind == 'no_new_counting' else
            f'{QUIET_RESULTS} {period}.')
    if kind == 'call_update':
        text = f'There was a small count update {period}.'
    if group.get('input_revision_changed'):
        text += ' Forecast inputs changed; any resulting forecast movement is listed separately.'
    return {'status': 'complete', 'source': 'local', 'kind': kind,
            'format': 'inline-figures-v1', 'entities': {},
            'text': {'summary': text, 'developments': [], 'insufficient_evidence': False},
            'usage': {}, 'estimated_usd': 0}

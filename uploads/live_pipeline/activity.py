"""Decide whether observed count changes warrant paid commentary.

This is an editorial filter, not part of the election model. Forecasts and full
mechanical comparisons are still exported. Candidate corrections matter even
when a booth's total or the seat's winning chances do not change.
"""


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
                differences = [after.get(p, 0)-before.get(p, 0) for p in before.keys() | after.keys()]
                additions = sum(max(0, v) for v in differences)
                removals = sum(max(0, -v) for v in differences)
                # Moving ten ballots from one candidate to another changes
                # twenty candidate entries, but affects ten ballots.
                accounts.append(max(additions, removals))
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
    if votes == 0:
        return 'no_new_counting'
    return 'minor_changes' if votes < threshold else 'request'


def notice(group, window):
    kind = group.get('commentary_decision')
    if kind not in ('no_new_counting', 'minor_changes'):
        return None
    # Rolling comparisons can span longer than their nominal window. The text
    # uses the actual interval; reference IDs/times remain in the comparison.
    seconds = group.get('actual_seconds')
    if window == 'previous':
        period = 'since the previous forecast'
    elif window == 'since_start':
        period = 'since counting began'
    elif window == 'today':
        period = 'since the forecast used as today\'s starting point'
    elif seconds is None:
        period = 'over this comparison period'
    else:
        minutes = seconds / 60
        period = f'over the {minutes:g}-minute comparison period'
    text = (f'No new counting occurred {period}.' if kind == 'no_new_counting' else
            f'There were no significant changes to the results {period}.')
    if group.get('input_revision_changed'):
        text += ' Forecast inputs changed; any resulting forecast movement is listed separately.'
    return {'status': 'complete', 'source': 'local', 'kind': kind,
            'format': 'inline-figures-v1', 'entities': {},
            'text': {'summary': text, 'developments': [], 'insufficient_evidence': False},
            'usage': {}, 'estimated_usd': 0}

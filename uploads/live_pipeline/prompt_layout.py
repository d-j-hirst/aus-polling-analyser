"""Arrange numerical evidence for reuse without introducing earlier LLM prose.

The prefix first contains editorial rules and election labels, then the current
forecast shared by its comparison windows. The changing reference and evidence
come last. Explicit cache boundaries avoid paying to write a different complete
comparison on every ordinary update. A benchmark uses the same boundaries;
reuse across different models, reasoning levels or speed tiers is not assumed.
"""
import json
from . import commentary_format


FORMAT = '''Reading the supplied data
Use the display figures: Each district has tables grouped by quantity. A level
row is [subject, percentile, before, after, change]. A three-column row
[subject, percentile, level] means both displayed levels are that value and
the displayed change is zero. A subject is a local
candidate ID, a pair of IDs in order, or a statewide party label. Null means
not applicable. For district context figures, each group names a booth (or
an empty string for the district) and quantity. The election reference carries
the shared quantity codes and their meanings. Each row starts with its subject;
the remaining columns are named in context_columns for that quantity.
Copy before, after or change into
**bold** prose, or combine after and change as **47.9% (+7.5 pp)**. Do not calculate
different rounded values. Percentile 50 is the median; 5 and 95 delimit a 90%
interval. A change in probability/share is in percentage points (pp), not percent.
For booth comparisons, use magnitude followed by direction, such as **13.7 pp**
below expectations. Direction supplies the sign.
For matched TCP comparisons, expected_share is the reference forecast and
observed_share is the current cumulative count, not a newly added batch.
equivalent_vote_difference compares them at the current TCP count size; it is
not an actual addition. An unavailable comparison does not imply agreement.
An apparent category transfer matches the candidate allocations removed from
one declaration category to those added to another. Read its label as source
→ destination. It adds no district votes by itself; check the district net FP
and TCP changes for other counting. FP and TCP can cover the same ballots.
For a close TCP, the counted lead belongs to the first candidate in the pair.
A negative lead means that candidate was behind. Net votes still to come are
estimated, including possible count revisions; a missing figure means unknown.
Within an evidence record, fields under common apply at both times. Add them
to the respective before or after fields to read each observation. An absent
field is unknown; a reported zero is a count.
'''

RAW_COUNTS = '''Raw fp_votes_current and tcp_votes_current candidate counts in the supplied
booth records may also be quoted as **83 votes** or **1,234 votes**, retaining
their before/after, seat, candidate and FP/TCP context. Prefer useful comparisons
over listing these counts; never round a projected number into a counted vote.
'''

BOOTH_COUNTS = '''For a booth quote the total formal votes counted so far in that ledger and
the candidate's observed percentage. FPtotal and TCPtotal are denominators;
netFP and netTCP are changes since the reference. {tcp_guidance}
District FP completion is the estimated percentage of the district count
completed; expected turnout may change. Keep it distinct from a booth share.
'''

ENTITIES_AND_OVERVIEW = '''Use entities: Write {seat:Flinders} for a district, {candidate:Flinders:1} for its
candidate with party ID 1, and {party:1} for that party generally. The election
reference lists each district once as [candidate names by ID, holder party ID,
holder margin]. Null means unavailable. The website displays the party label
where a candidate name is unavailable. Do not invent identifiers or name an unlisted person.
Seat holder badges describe the pre-election holder, not the projected winner.
Read the overview as context: All-seat chances are rounded for orientation;
use the display figures for numerical claims. A TCP share is conditional on its named
pairing, and a changed pairing must not be treated as one continuous margin.
Treat selected evidence cautiously: Some local details are omitted to bound
request size. Never claim that omitted booths had no counting or that a model
estimate is a directly measured vote. The current overview is shared across
timeframes; explain only changes supported by this comparison's reference.
'''


def canonical(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(',', ':'))


# Booth measures recur for many seats and windows. Give their long labels
# once in the reusable election reference, including the units and denominator.
BOOTH_MEASURES = {
    'affected': 'changed_votes',
    'netFP': 'fp_vote_change',
    'netTCP': 'tcp_vote_change',
    'FPdiff': 'FP share minus previous booth expectation',
    'TCPtotal': 'reported cumulative TCP total for this pairing',
    'TCPshare': 'observed TCP share versus reference forecast for the same pair',
}
MEASURE_CODES = {label: code for code, label in BOOTH_MEASURES.items()}
OBSERVED_MEASURES = {'FPtotal': 'reported cumulative FP total',
                     'FPshare': 'share of reported FP votes',
                     'TCPobserved': 'share of reported TCP votes'}
MEASURE_CODES.update({label: code for code, label in OBSERVED_MEASURES.items()})


def _subject(row):
    # The enclosing district supplies the rest of the frontend token. Use the
    # actual party/candidate identifier, including distinct independents.
    if 'candidate' in row:
        return row['candidate'].rsplit(':', 1)[1][:-1]
    if 'pair' in row:
        return [candidate.rsplit(':', 1)[1][:-1] for candidate in row['pair']]
    return row.get('party')


def _figure_tables(rows):
    """Send each district, booth and measure once per table. All quotable
    numbers retain the original units, sign and display precision used by
    the validator. Columns for unlike quantities are explicitly labelled."""
    tables, contexts, columns = {}, {}, {}
    for row in rows:
        seat = row['seat']
        quantity = MEASURE_CODES.get(row['quantity'], row['quantity'])
        if 'before' in row:
            table = tables.setdefault(seat, {}).setdefault(quantity, [])
            values = [_subject(row), row.get('percentile'), row['before']]
            # The difference is rounded independently of the levels. Two
            # levels can both display as 47.9% while their change is +0.1 pp.
            if row['before'] != row['after'] or row['change'] not in ('+0.0 pp', '+0.0 seats', '+0 votes'):
                values.extend([row['after'], row['change']])
            table.append(values)
        else:
            fields = [k for k in commentary_format.CONTEXT_FIGURE_FIELDS if k in row]
            if quantity in ('FPdiff', 'TCPshare') and {'value', 'magnitude', 'direction'}.issubset(fields):
                fields.remove('value')
            columns[quantity] = fields
            table = contexts.setdefault(seat, {}).setdefault(row.get('booth', ''), {}).setdefault(quantity, [])
            table.append([_subject(row), *(row[k] for k in fields)])
    return {'figures': tables, 'context_figures': contexts, 'context_columns': columns}


def _share_unchanged_fields(event):
    """Write identical parts of two reported observations once. Keep changes,
    newly present fields and withdrawals attached to the correct endpoint."""
    before, after = event.get('before'), event.get('after')
    if not isinstance(before, dict) or not isinstance(after, dict):
        return event
    # Compare the serialized values so, for example, a boolean flag cannot
    # disappear into a vote count of 1. A missing key never matches a null.
    common = {k: v for k, v in before.items() if k in after and canonical(v) == canonical(after[k])}
    if not common:
        return event
    packed = dict(event, common=common, before={k: v for k, v in before.items() if k not in common},
                  after={k: v for k, v in after.items() if k not in common})
    return packed if len(canonical(packed)) < len(canonical(event)) else event


def _prompt_event(event):
    """Keep the observations behind a selected booth comparison. The display
    tables already give checked differences from expectations and net additions.
    The long, unrounded projected party vectors used to derive those comparisons
    are local working data. Shared preference and recovery evidence has no
    equivalent in the display table, so keep its explanatory detail."""
    if event['kind'] != 'booths':
        return _share_unchanged_fields({k:v for k,v in event.items() if k not in ('id', 'interpretation')})
    result = {k:event[k] for k in ('kind', 'seat', 'record', 'pair_changed') if k in event}
    for time in ('before', 'after'):
        record = event.get(time)
        if record is not None:
            result[time] = {k:record[k] for k in ('fp_votes_current', 'tcp_votes_current',
                            'count_to_projection_ids', 'expected_remaining_fp') if k in record}
    comparison = event.get('tcp_against_previous_forecast')
    if comparison:
        if comparison.get('available'):
            result['tcp_expectation_source'] = comparison['expectation_kind']
        else:
            result['tcp_comparison_unavailable'] = comparison['reason']
    return _share_unchanged_fields(result)


def _district_roster(packet):
    """Group names and pre-election holder details in the reusable reference.
    Older packets can lack a name table or a margin. Preserve the district
    and any known identifiers, including separate independent candidates."""
    candidates = packet.get('all_candidate_identities', packet.get('candidate_identities', {}))
    holders = packet.get('seat_holders', {})
    roster = {}
    for seat in sorted(set(packet['all_seats']) | candidates.keys() | holders.keys()):
        names = None
        if seat in candidates:
            names = {party: identity['name'] for party, identity in candidates[seat].items() if identity.get('name')}
        holder = holders.get(seat) or {}
        roster[seat] = [names, holder.get('party_id'), holder.get('margin')]
    return roster


def input_messages(packet, instructions, settings, *, include_local_win_order=False, include_booth_context=False,
                   include_unchanged_local_counts=False, require_tcp_comparison=False):
    """Return stable-prefix messages with readable, checked display figures.

    The verbose numerical blocks are represented by rounded display figures.
    Send these once; keep non-numerical identity-change flags
    and the detailed booth evidence, which contain additional information.
    Local validation and website exports retain the original named objects.
    """
    reference = {'parties': packet['parties'], 'units': packet['units'], 'timezone': packet.get('source_timezone'),
                 'booth_measures': BOOTH_MEASURES, 'districts': _district_roster(packet)}
    if include_booth_context:
        reference['booth_measures'] = dict(BOOTH_MEASURES, **OBSERVED_MEASURES)
    shared = {'source_time': packet['source_time'], 'all_seats': packet['all_seats'],
              'overview_precision': packet.get('overview_precision')}
    duplicate_fields = {'parties', 'units', 'source_timezone', 'source_time', 'all_seats',
                        'overview_precision', 'candidate_identities', 'all_candidate_identities',
                        'win_changes', 'seat_changes', 'statewide', 'observed_count_changes_by_seat',
                        'metrics', 'metric_contexts', 'display_figures', 'entities', 'seat_holders', 'booth_changes',
                        'evidence', 'category_transfers', 'close_tcp_counts', 'statewide_preference_changes',
                        'district_fp_completion'}
    comparison = {k: v for k, v in packet.items() if k not in duplicate_fields}
    if include_local_win_order and 'observed_count_changes_by_seat' in packet:
        # The win table is already ordered by absolute percentage-point change.
        # List each district once if its counts also changed during this window.
        # Use the district totals from before booth evidence was shortened for
        # the prompt. A missing booth detail alone tells us nothing about whether
        # local counting took place. The full figures still cover wider movements.
        local_counts = packet['observed_count_changes_by_seat']
        moving_districts = dict.fromkeys(
            row['seat'] for row in packet['win_changes']
            if row['change_pp'] and not row.get('identity_changed'))
        comparison['local_seats_by_win_movement'] = [
            seat for seat in moving_districts if local_counts.get(seat, {}).get('affected_votes', 0) > 0]
        if include_unchanged_local_counts and all('affected_votes' in counts for counts in local_counts.values()):
            # An omitted booth is not evidence that counting stood still.
            # Name districts only when the full district totals account for
            # every observed change and history shows no extra activity between
            # the two endpoints (such as a correction later reversed).
            # This is count context. A separate statewide or regional movement
            # is needed to support an explanation of the district forecast.
            activity = packet.get('count_activity') or {}
            accounted_votes = sum(row['affected_votes'] for row in local_counts.values())
            if (activity.get('known') and accounted_votes > 0
                    and activity.get('changed_votes') == activity.get('endpoint_changed_votes') == accounted_votes):
                unchanged = [seat for seat in moving_districts if not local_counts.get(seat, {}).get('affected_votes')]
                if unchanged:
                    comparison['win_movements_without_changed_local_counts'] = unchanged
    if packet.get('statewide_preference_changes'):
        comparison['preference_flow_basis'] = (
            'Estimated share of all projected source-party first preferences in districts where all three parties stand. '
            'In mixed counts, the two finalist totals are measured and the split by source party is estimated.')
    if packet.get('category_transfers'):
        activity = packet.get('count_activity') or {}
        # The sum across records includes both sides of a reclassification.
        # Its only additional editorial use here is detecting changes within
        # the window that are hidden by a simple endpoint comparison.
        comparison['count_activity'] = {'known': activity.get('known'),
            'additional_activity_between_endpoints': activity['changed_votes'] > activity['endpoint_changed_votes']
            if 'endpoint_changed_votes' in activity else None}
    comparison['identity_changes'] = [r for r in packet['win_changes'] if r.get('identity_changed')]
    if include_booth_context:
        figures = commentary_format.figures_with_booth_context(packet, require_tcp_comparison=require_tcp_comparison)
    else:
        figures = packet.get('display_figures') or commentary_format.figures(packet)
    # Repeating long quantity labels and object keys on every number doubles
    # the request without adding evidence. A labelled table keeps the subject,
    # units and before/after distinction while avoiding repeated metadata.
    comparison.update(_figure_tables(figures))
    comparison['evidence'] = [_prompt_event(event) for event in packet['evidence']]
    comparison['booth_summary_by_seat'] = {seat: {kind: group[kind]['text'] for kind in ('new', 'updated')}
        for seat, group in packet.get('booth_changes', {}).get('by_seat', {}).items()}
    boundary = {'mode': 'explicit', 'ttl': '30m'}
    def content(text, cache=False):
        block = {'type': 'input_text', 'text': text}
        if cache:
            block['prompt_cache_breakpoint'] = boundary.copy()
        return block
    booth_guide = BOOTH_COUNTS.format(tcp_guidance=(
        'Only TCPshare supplies a quotable booth TCP result and its same-pair difference.'
        if require_tcp_comparison else 'An observed share can be\navailable even when the reference has no projection for the same candidates.'))
    data_guide = FORMAT + (booth_guide if include_booth_context else RAW_COUNTS) + ENTITIES_AND_OVERVIEW
    return [
        {'role': 'developer', 'content': [content(instructions+'\n\n'+data_guide)]},
        {'role': 'user', 'content': [content('Election reference\n'+canonical(reference), True),
                                   content('Current forecast overview\n'+canonical(shared), settings.get('cache_overview', True)),
                                   content('Comparison evidence\n'+canonical(comparison), settings.get('cache_comparison', False))]}]


def sizes(messages):
    blocks = [c for m in messages for c in m['content']]
    return {'total_text_bytes': sum(len(c['text'].encode()) for c in blocks),
            'election_prefix_bytes': sum(len(c['text'].encode()) for c in blocks[:2]),
            'current_forecast_prefix_bytes': sum(len(c['text'].encode()) for c in blocks[:3]),
            'comparison_bytes': len(blocks[-1]['text'].encode())}

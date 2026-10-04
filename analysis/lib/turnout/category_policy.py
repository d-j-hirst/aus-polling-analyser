"""Keep reporting and service changes separate from voting-behaviour changes.

The official counts stay in the source datasets. These rules choose which
parts can be compared and explain exceptional empty rows before a statistical
model sees them. A broader observed group is preferable to a fabricated split
when an election introduces or changes a category.
"""

import math
from dataclasses import replace


# Effective elections identify changes to particular categories, rather than
# globally discarding an election's useful turnout, postal and early evidence.
# A comparison crossing a listed boundary must combine the affected category
# into an observed parent group or leave that comparison out.
DEFINITION_CHANGES = (
    dict(election='2010vic', categories=('other', 'other_declaration'),
         reason='On-the-day enrolment/provisional arrangements replace the older declaration grouping.'),
    dict(election='2025wa', categories=('other', 'provisional'),
         reason='Polling-day enrolment expands provisional eligibility.'),
    dict(election='2025wa', categories=('early', 'early_in_person', 'absent'),
         reason='Polling-place and report categories do not preserve the older early/absent distinction.'),
    dict(election='2023nsw', categories=('remote_electronic', 'other'),
         reason='Electronic voting is unavailable after the previous election.'),
    dict(election='2015qld', categories=('early_ordinary', 'early_declaration', 'other', 'other_small_modes'),
         reason='Queensland reporting and voting-category definitions change.'),
    dict(election='2017qld', categories=('other', 'other_small_modes', 'provisional'),
         reason='The general voter-identification requirement no longer supplies uncertain-identity votes.'),
    dict(election='2020qld', categories=('early_ordinary', 'early_declaration', 'other', 'other_small_modes'),
         reason='Queensland detailed early and small-mode reporting changes.'),
)

SERVICE_EXCLUSIONS = (
    dict(election='2020qld', category='mobile_or_institution',
         source_category='EV Early Voting Centre (mobile polling)',
         reason='Declared-institution polling was cancelled; empty service placeholders are not behavioural zeros.'),
)

# These zeros were reviewed against other districts and the published results.
# They describe incomplete reporting, not an unavailable voting service or
# voter behaviour. A later positive source revision automatically releases the
# exception; the unmodified source dataset always remains available for audit.
MISSING_COUNT_REVIEWS = (
    dict(election='2010vic', seat='Oakleigh District', category='provisional'),
    dict(election='2014vic', seat='Croydon District', category='provisional'),
    dict(election='2014vic', seat='Ringwood District', category='provisional'),
    *(dict(election='2026sa', seat=seat, category='declaration_early')
      for seat in ('Black', 'Croydon', 'Davenport', 'Hammond', 'Light', 'Ngadjuri', 'Taylor')),
    dict(election='2026sa', seat='Enfield', category='mobile_or_institution'),
    dict(election='2026sa', seat='Hammond', category='mobile_or_institution'),
    dict(election='2026sa', seat='Black', category='provisional'),
    dict(election='2026sa', seat='MacKillop', category='other'),
    *(dict(election='2026sa', seat=seat, category='early_in_person',
           source_category_prefix='Early Voting - Absent Declaration')
      for seat in ('Flinders', 'Kavel')),
)


def missing_count_review(dataset):
    """Identify reviewed empty groups still present in this source revision."""
    election = dataset.elections[0].election_code
    result = []
    for rule in MISSING_COUNT_REVIEWS:
        rows = [r for r in dataset.vote_types if election == rule['election']
                and r.seat_name == rule['seat'] and r.canonical_category == rule['category']
                and r.source_category.startswith(rule.get('source_category_prefix', ''))]
        if rows and all(r.formal_votes in (0, None) for r in rows):
            result.append(dict(rule, reason='Reviewed reporting anomaly: the category count is missing.',
                               source_categories=sorted({r.source_category for r in rows})))
    return result


def apply_missing_count_policy(dataset):
    """Omit unreliable category partitions while retaining district totals.

    An unrecorded component may have been included in another category. Mark
    the whole affected district partition partial rather than trusting the
    other components as a complete division. Replace the missing counts with
    None only in the analytical copy; source zeros remain untouched.
    """
    reviews = missing_count_review(dataset)
    if not reviews:
        return dataset
    affected = {r['seat'] for r in reviews}
    # Some canonical groups contain both a sound booth total and a missing
    # declaration subcategory. Only erase the specifically reviewed source
    # rows; the broader partition is still partial because its split is unknown.
    missing = {(r['seat'], r['category'], source)
               for r in reviews for source in r['source_categories']}
    rows = []
    for row in dataset.vote_types:
        if row.seat_name not in affected:
            rows.append(row)
        elif (row.seat_name, row.canonical_category, row.source_category) in missing:
            rows.append(replace(row, formal_votes=None, informal_votes=None,
                                total_ballots=None, coverage='partial'))
        else:
            rows.append(replace(row, coverage='partial'))
    return replace(dataset, vote_types=rows)


def suppressed_record(election, record):
    """Identify a known empty service placeholder in within-election analysis.

    The Cook remote-mobile row has a different source label and remains in the
    analysis. Suppression cannot discard a positive or unknown observation:
    a revised source with either requires an explicit review of this exception.
    """
    for rule in SERVICE_EXCLUSIONS:
        if (election == rule['election'] and record.canonical_category == rule['category']
                and getattr(record, 'source_category', '') == rule['source_category']):
            if any(getattr(record, field, None) != 0
                   for field in ('formal_votes', 'informal_votes', 'total_ballots')):
                raise ValueError('Service exclusion contains nonzero or unknown counts: ' + election)
            return True
    return False


def definition_epoch(election, category):
    """Name the definition periods relevant to this particular category."""
    return tuple(rule['election'] for rule in DEFINITION_CHANGES
                 if election[4:] == rule['election'][4:] and election[:4] >= rule['election'][:4]
                 and category in rule['categories'])


def comparison_exclusion(previous, current, category):
    """Explain why an individual category cannot measure behaviour here.

    Service cancellation matters both when the service disappears and when it
    returns. Combining it with ordinary votes preserves a broader vote pool,
    but its own count must not train election-to-election behaviour uncertainty.
    """
    if previous.endswith('qld') and '2020qld' in (previous, current) and category in {
            'mobile_or_institution', 'other', 'other_small_modes'}:
        return '2020 declared-institution service cancellation affects this category.'
    if definition_epoch(previous, category) != definition_epoch(current, category):
        return 'Known category definition/eligibility change.'
    return None


def merge_other_required(previous, current):
    """Use ordinary plus other when the other-category baseline is incompatible."""
    return comparison_exclusion(previous, current, 'other') is not None


def merge_other(values):
    """Add observed groups without manufacturing any missing component count."""
    output = dict(values)
    ordinary = 'ordinary_early_absent' if 'ordinary_early_absent' in output else 'ordinary'
    if 'other' not in output or ordinary not in output:
        raise ValueError('Cannot combine an unpublished ordinary/other split.')
    output[ordinary + '_other'] = output.pop(ordinary) + output.pop('other')
    return output


def subset_log_odds(count, parent):
    """Transform an observed finite subset while retaining small-count evidence.

    A possible zero receives half a vote, and an observed full share receives
    half a vote in its complement. The input floor is the smaller of 0.1% and
    half a vote divided by the parent. Thus a large pool can retain genuine
    positive shares below 0.1%. This alters modelling coordinates only; raw
    counts and accuracy scores retain the observations. Unknowns stay unknown.
    """
    if count is None or parent is None or parent == 0:
        return None
    if not 0 <= count <= parent:
        raise ValueError('A subset count must lie within its recorded parent.')
    included, excluded = count or .5, (parent - count) or .5
    proportion = included / (included + excluded)
    floor = min(.001, .5 / parent)
    proportion = min(1 - floor, max(floor, proportion))
    return math.log(proportion) - math.log1p(-proportion)


def possible_proportions(counts):
    """Give available categories finite starting proportions, including zeros.

    Structural and unknown categories must be removed or regrouped before this
    helper is called. A whole empty group has no observed internal division and
    is returned as unknown; assigning equal weights would invent that division.
    """
    if any(value is None for value in counts):
        return None
    if any(value < 0 for value in counts):
        raise ValueError('Recorded category counts cannot be negative.')
    if not sum(counts):
        return None
    parent = sum(counts)
    floor_count = min(.001 * parent, .5)
    adjusted = [max(.5 if value == 0 else value, floor_count) for value in counts]
    total = sum(adjusted)
    return [value / total for value in adjusted]


def empty_groups(datasets):
    """List recorded all-zero groups after adding mutually exclusive source rows.

    Suppressed service rows are omitted. Null counts are not empty groups.
    Each remaining entry retains its source labels for case-by-case assessment;
    it is not automatically classified as a structural zero.
    """
    result = []
    for election, dataset in sorted(datasets.items()):
        reviewed = {(r['seat'], r['category']) for r in missing_count_review(dataset)} if hasattr(dataset, 'elections') else set()
        grouped = {}
        seats = ({row.seat_name: row for row in dataset.seat_totals}
                 if hasattr(dataset, 'seat_totals') else getattr(dataset, 'seats', {}))
        records = (dataset.vote_types if hasattr(dataset, 'vote_types') else
                   [row for rows in dataset.categories.values() for row in rows])
        for row in records:
            if suppressed_record(election, row):
                continue
            grouped.setdefault((row.seat_name, row.canonical_category), []).append(row)
        for (seat, category), rows in sorted(grouped.items()):
            if all(row.formal_votes == 0 for row in rows):
                ballots = [getattr(row, 'total_ballots', None) for row in rows]
                # A zero formal count can accompany informal ballots. Unknown
                # ballots cannot establish that a reporting group is empty.
                if any(value is not None and value > 0 for value in ballots):
                    continue
                status = 'recorded_empty' if all(value == 0 for value in ballots) else 'formal_zero_ballots_unknown'
                if (seat, category) in reviewed:
                    status = 'reviewed_missing_count'
                result.append(dict(election=election, seat=seat, category=category,
                    status=status,
                    formal_votes=0,
                    total_ballots=0 if status == 'recorded_empty' else None,
                    parent_formal_votes=seats[seat].formal_votes if seat in seats else None,
                    source_rows=len(rows),
                    source_categories=sorted({getattr(row, 'source_category', category) for row in rows}),
                    assessment=('Recorded empty group; availability and reporting basis require review.'
                                if status == 'recorded_empty' else
                                'Formal count is zero, but unknown ballot counts cannot establish an empty group.')))
    return result


# Transforming each rate does not resolve a difference in its parent group.
# Keep these reviews explicit wherever relationships are analysed together.
PARENT_REVIEWS = (
    dict(analysis='Turnout and formality relationship',
         parents=('enrolment', 'total ballots'),
         reason='Formal ballots are conditional on participation; transformed rates have different parent groups.'),
    dict(analysis='Category-share changes and turnout relationship',
         parents=('formal votes', 'enrolment'),
         reason='Category substitution and participation use different parent groups and are not interchangeable.'),
    dict(analysis='Federal issuing-location early count and resident-electorate final count',
         parents=('voting-location electorate', 'enrolled electorate'),
         reason='Districts refer to different voter populations; the operational model uses the national aggregate.'),
    dict(analysis='Federal declaration anchor and within-early allocation',
         parents=('all formal votes', 'combined formal early votes'),
         reason='The declaration baseline uses all formal votes, then must fit inside an uncertain early-vote pool.'),
)

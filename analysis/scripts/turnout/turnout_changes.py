"""Describe historical turnout changes without changing live-model assumptions.

Parent context: the turnout-data foundation in docs/turnout-data-foundation.md.
This exploratory report uses final counts, not operational pre-election series.

Main functions:
* load_elections validates the source datasets and excludes operational-only data.
* category_counts groups complete partitions at defensible comparison levels.
* build_tables performs the real analysis: rates, changes and matched-seat evidence.
* render_report explains coverage, distributions and modelling implications.
* main writes the Markdown report and detailed CSVs using only the standard library.
"""

import argparse
from collections import defaultdict
import csv
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from lib.paths import REPOSITORY_DIRECTORY
import statistics

from lib.shared.turnout_data import load_dataset


ROOT = REPOSITORY_DIRECTORY
RATE_METRICS = ('turnout_pct', 'formality_pct', 'formal_per_enrolled_pct')
COUNTS = ('enrolment', 'total_ballots', 'formal_votes', 'informal_votes')
METRIC_LABELS = {
    'turnout_pct': 'Turnout',
    'formality_pct': 'Formality',
    'formal_per_enrolled_pct': 'Formal / enrolled',
    'enrolment_growth_pct': 'Enrolment growth',
    'formal_votes_growth_pct': 'Formal-vote growth',
}


@dataclass
class Election:
    code: str
    date: str
    jurisdiction: str
    seats: dict
    categories: dict
    sources: list


def percent(numerator, denominator):
    return 100.0 * numerator / denominator if denominator else None


def rates(counts):
    return {
        'turnout_pct': percent(counts['total_ballots'], counts['enrolment']),
        'formality_pct': percent(counts['formal_votes'], counts['total_ballots']),
        'formal_per_enrolled_pct': percent(counts['formal_votes'], counts['enrolment']),
    }


def load_elections(directory):
    """Require known final counts; incomplete category coverage is handled separately."""
    elections, excluded = [], []
    for path in sorted(directory.glob('*.json')):
        dataset = load_dataset(path)
        if len(dataset.elections) != 1:
            raise ValueError('{} must describe one election'.format(path))
        definition = dataset.elections[0]
        if not dataset.seat_totals:
            excluded.append(definition.election_code)
            continue
        if any(getattr(seat, key) is None for seat in dataset.seat_totals for key in COUNTS):
            raise ValueError('{} has incomplete final totals'.format(path))
        partitions = defaultdict(set)
        categories = defaultdict(list)
        for record in dataset.vote_types:
            partitions[record.seat_name].add(record.partition_id)
            categories[record.seat_name].append(record)
        if any(len(ids) != 1 for ids in partitions.values()):
            raise ValueError('{} has alternative partitions; select one explicitly'.format(path))
        complete = {
            seat: records for seat, records in categories.items()
            if all(row.coverage == 'complete' for row in records)
        }
        elections.append(Election(
            definition.election_code, definition.election_date, definition.jurisdiction,
            {seat.seat_name: seat for seat in dataset.seat_totals}, complete,
            [source for source in dataset.sources if source.status == 'final'],
        ))
    return sorted(elections, key=lambda election: election.date), excluded


def comparison_group(jurisdiction, category):
    # These groups preserve observed totals across reporting changes. They do
    # not reconstruct unavailable early/ordinary splits or postal acceptance.
    if jurisdiction == 'fed' and category in {
        'election_day_ordinary', 'ordinary_combined', 'declaration_early'
    }:
        return 'ordinary_and_all_prepoll'
    if jurisdiction == 'vic' and category in {
        'declaration_combined', 'provisional', 'marked_as_voted'
    }:
        return 'other_declaration'
    if jurisdiction == 'nsw' and category in {
        'enrolment', 'provisional', 'enrolment_or_provisional'
    }:
        return 'enrolment_and_provisional'
    if jurisdiction == 'qld':
        if category in {'early_in_person', 'declaration_early'}:
            return 'all_early_in_person'
        if category not in {'election_day_ordinary', 'postal', 'absent'}:
            return 'other_small_modes'
    if jurisdiction == 'wa' and category in {'election_day_ordinary', 'mobile_or_institution'}:
        return 'ordinary_including_mobile'
    return category


def category_counts(election, seat, native=False):
    """Sum duplicate source rows; never add alternative partitions together."""
    records = election.categories.get(seat)
    if records is None:
        return None
    groups = defaultdict(list)
    for record in records:
        key = record.canonical_category if native else comparison_group(
            election.jurisdiction, record.canonical_category)
        groups[key].append(record)
    return {
        key: {
            field: sum(getattr(row, field) for row in rows)
            if all(getattr(row, field) is not None for row in rows) else None
            for field in ('formal_votes', 'total_ballots', 'informal_votes')
        }
        for key, rows in groups.items()
    }


def total_counts(election, names):
    return {key: sum(getattr(election.seats[name], key) for name in names) for key in COUNTS}


def comparison_note(previous, current):
    notes = []
    if previous.jurisdiction == 'fed' and previous.code == '2007fed':
        notes.append('AEC 2010 category break: only ordinary + all pre-poll is comparable')
    if previous.jurisdiction == 'qld' and (previous.code, current.code) in {
        ('2012qld', '2015qld'), ('2017qld', '2020qld')
    }:
        notes.append('Queensland reporting/category break; exclude from category correlations')
    if previous.jurisdiction == 'vic' and current.code == '2022vic':
        notes.append('2022 excludes postponed Narracan; aggregate electorates differ')
    if previous.jurisdiction == 'wa' and current.code == '2025wa':
        notes.append('WA report-to-polling-place split: investigate early/absent mode comparability')
    if current.jurisdiction in {'qld', 'wa'} and current.code in {'2020qld', '2021wa'}:
        notes.append('COVID-period election; assess separately before forecasting')
    if current.code in {'2022fed', '2022vic', '2022sa'}:
        notes.append('2022 election; inspect COVID-period category changes separately')
    return '; '.join(notes)


def change_row(previous, current, level, geography, old_names, new_names):
    old_counts, new_counts = total_counts(previous, old_names), total_counts(current, new_names)
    old_rates, new_rates = rates(old_counts), rates(new_counts)
    row = dict(jurisdiction=current.jurisdiction, previous=previous.code, current=current.code,
               level=level, geography=geography, previous_seats=len(old_names), current_seats=len(new_names),
               interval_years=(date.fromisoformat(current.date) - date.fromisoformat(previous.date)).days / 365.25,
               notes=comparison_note(previous, current))
    for key in COUNTS + RATE_METRICS:
        old = old_counts.get(key, old_rates.get(key))
        new = new_counts.get(key, new_rates.get(key))
        row['previous_' + key], row['current_' + key] = old, new
        row[key + '_change' + ('_pp' if key in RATE_METRICS else '')] = (
            new - old if old is not None and new is not None else None)
    for key in COUNTS:
        row[key + '_growth_pct'] = percent(new_counts[key] - old_counts[key], old_counts[key])
    # The multiplicative identity distinguishes more enrolled electors from
    # changes in voting participation and ballot formality.
    if all(old_rates[key] and new_rates[key] for key in ('turnout_pct', 'formality_pct')):
        ratio = (new_counts['enrolment'] / old_counts['enrolment']
                 * new_rates['turnout_pct'] / old_rates['turnout_pct']
                 * new_rates['formality_pct'] / old_rates['formality_pct'])
        if abs(ratio - new_counts['formal_votes'] / old_counts['formal_votes']) > 1e-10:
            raise ValueError('Formal-vote growth decomposition does not reconcile')
    return row


def category_rows(previous, current, level, geography, old_names, new_names, native=False):
    old_covered = sorted(set(old_names) & previous.categories.keys())
    new_covered = sorted(set(new_names) & current.categories.keys())
    if not old_covered or not new_covered:
        return []
    old_by_seat = {name: category_counts(previous, name, native) for name in old_covered}
    new_by_seat = {name: category_counts(current, name, native) for name in new_covered}
    old_keys = {key for groups in old_by_seat.values() for key in groups}
    new_keys = {key for groups in new_by_seat.values() for key in groups}
    # Native rows are comparable only where the exact category set survives.
    # Broad partitions remain exhaustive: an absent grouped mode is a zero
    # contribution to that partition, never a fabricated missing observation.
    keys = old_keys & new_keys if native else old_keys | new_keys
    if native and old_keys != new_keys:
        return []
    if native and previous.jurisdiction == 'fed' and previous.code == '2007fed':
        return []
    output = []
    for key in sorted(keys):
        row = dict(jurisdiction=current.jurisdiction, previous=previous.code, current=current.code,
                   level=level, geography=geography, category=key,
                   view='native' if native else 'comparison',
                   previous_coverage=len(old_covered), current_coverage=len(new_covered),
                   previous_total_seats=len(old_names), current_total_seats=len(new_names),
                   notes=comparison_note(previous, current))
        if len(old_covered) != len(old_names) or len(new_covered) != len(new_names):
            row['notes'] += '; incomplete category coverage; denominators use covered seats only'
        for prefix, election, names, groups in (
            ('previous_', previous, old_covered, old_by_seat),
            ('current_', current, new_covered, new_by_seat),
        ):
            counts = []
            for name in names:
                # No row is an observed zero in the exhaustive comparison
                # grouping; native export omits these newly introduced modes.
                counts.append(groups[name].get(key, dict(formal_votes=0, total_ballots=0, informal_votes=0)))
            for field in ('formal_votes', 'total_ballots', 'informal_votes'):
                values = [record[field] for record in counts]
                row[prefix + field] = sum(values) if all(v is not None for v in values) else None
            totals = total_counts(election, names)
            row[prefix + 'formal_share_pct'] = percent(row[prefix + 'formal_votes'], totals['formal_votes'])
            row[prefix + 'ballot_share_pct'] = (
                percent(row[prefix + 'total_ballots'], totals['total_ballots'])
                if row[prefix + 'total_ballots'] is not None else None)
            row[prefix + 'formality_pct'] = (
                percent(row[prefix + 'formal_votes'], row[prefix + 'total_ballots'])
                if row[prefix + 'total_ballots'] is not None else None)
        for metric in ('formal_share_pct', 'ballot_share_pct', 'formality_pct'):
            a, b = row['previous_' + metric], row['current_' + metric]
            row[metric + '_change_pp'] = b - a if a is not None and b is not None else None
        row['formal_votes_growth_pct'] = percent(
            row['current_formal_votes'] - row['previous_formal_votes'], row['previous_formal_votes'])
        output.append(row)
    return output


def build_tables(elections):
    """Analyze consecutive elections separately within each jurisdiction."""
    histories = defaultdict(list)
    snapshots, changes, categories, matches, native = [], [], [], [], []
    for election in elections:
        histories[election.jurisdiction].append(election)
        for level, geography, names in geographies(election):
            counts = total_counts(election, names)
            snapshots.append(dict(election=election.code, jurisdiction=election.jurisdiction,
                                  level=level, geography=geography, seats=len(names), **counts, **rates(counts)))
            covered = sorted(set(names) & election.categories.keys())
            totals = total_counts(election, covered)
            groups = [category_counts(election, name, True) for name in covered]
            for category in sorted({key for group in groups for key in group}):
                records = [group[category] for group in groups if category in group]
                counts = {key: sum(row[key] for row in records)
                          if all(row[key] is not None for row in records) else None
                          for key in ('formal_votes', 'total_ballots', 'informal_votes')}
                native.append(dict(election=election.code, jurisdiction=election.jurisdiction,
                                   level=level, geography=geography, category=category,
                                   covered_seats=len(covered), total_seats=len(names), **counts,
                                   formal_share_pct=percent(counts['formal_votes'], totals['formal_votes']),
                                   formality_pct=percent(counts['formal_votes'], counts['total_ballots'])
                                   if counts['total_ballots'] is not None else None))
    for jurisdiction, history in sorted(histories.items()):
        for previous, current in zip(history, history[1:]):
            old_geo = {(level, geo): names for level, geo, names in geographies(previous)}
            new_geo = {(level, geo): names for level, geo, names in geographies(current)}
            for level, geography in sorted(old_geo.keys() & new_geo.keys()):
                old_names, new_names = old_geo[level, geography], new_geo[level, geography]
                changes.append(change_row(previous, current, level, geography, old_names, new_names))
                categories.extend(category_rows(previous, current, level, geography, old_names, new_names))
                if jurisdiction == 'fed':
                    categories.extend(category_rows(previous, current, level, geography, old_names, new_names, True))
            matched = set(previous.seats) & set(current.seats)
            matches.append(dict(jurisdiction=jurisdiction, previous=previous.code, current=current.code,
                                matched_seats=len(matched),
                                previous_only='; '.join(sorted(previous.seats.keys() - matched)),
                                current_only='; '.join(sorted(current.seats.keys() - matched))))
    aggregate_changes = {(r['previous'], r['current'], r['level'], r['geography']): r
                         for r in changes if r['level'] != 'seat'}
    election_by_code = {e.code: e for e in elections}
    for row in changes:
        if row['level'] != 'seat':
            continue
        national = aggregate_changes[row['previous'], row['current'], 'election', row['jurisdiction']]
        state = None
        if row['jurisdiction'] == 'fed':
            subdivision = election_by_code[row['current']].seats[row['geography']].subdivision
            state = aggregate_changes[row['previous'], row['current'], 'state', subdivision]
        for metric in RATE_METRICS:
            key = metric + '_change_pp'
            row[metric + '_change_minus_election_pp'] = row[key] - national[key]
            row[metric + '_change_minus_state_pp'] = row[key] - state[key] if state else None
    return dict(levels=snapshots, changes=changes, category_changes=categories,
                matching=matches, native_categories=native)


def geographies(election):
    yield 'election', election.jurisdiction, sorted(election.seats)
    if election.jurisdiction == 'fed':
        subdivisions = sorted({seat.subdivision for seat in election.seats.values()})
        for subdivision in subdivisions:
            if not subdivision:
                raise ValueError('Federal record has no state/territory')
            yield 'state', subdivision, sorted(
                name for name, seat in election.seats.items() if seat.subdivision == subdivision)
    for name in sorted(election.seats):
        yield 'seat', name, [name]


def quantile(values, fraction):
    values = sorted(values)
    position = (len(values) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(values) - 1)
    return values[lower] + (values[upper] - values[lower]) * (position - lower)


def distribution(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    return [len(values), statistics.mean(values), statistics.median(values),
            quantile(values, .1), quantile(values, .9),
            statistics.stdev(values) if len(values) > 1 else 0.0]


def correlation(pairs):
    pairs = [(a, b) for a, b in pairs if a is not None and b is not None]
    if len(pairs) < 3:
        return None
    xs, ys = zip(*pairs)
    x_mean, y_mean = statistics.mean(xs), statistics.mean(ys)
    cross = sum((x - x_mean) * (y - y_mean) for x, y in pairs)
    square_x = sum((x - x_mean) ** 2 for x in xs)
    square_y = sum((y - y_mean) ** 2 for y in ys)
    return cross / (square_x * square_y) ** .5 if square_x and square_y else None


def fmt(value, signed=False):
    if value is None:
        return '-'
    if isinstance(value, float):
        return format(value, '+.2f' if signed else '.2f')
    return str(value).replace('|', '\\|')


def table(headers, rows):
    return ['| ' + ' | '.join(headers) + ' |',
            '| ' + ' | '.join('---' for _ in headers) + ' |'] + [
        '| ' + ' | '.join(fmt(value) for value in row) + ' |' for row in rows]


def render_report(elections, excluded, tables):
    changes, categories = tables['changes'], tables['category_changes']
    lines = [
        '# Historical turnout and vote-category changes', '',
        'Generated from the validated normalized final-result JSONs in '
        '`analysis/Data/Turnout`. Reproduce with `cd analysis && python3 -B -m scripts.turnout.turnout_changes`.', '',
        '## Scope and interpretation', '',
        '{} elections and {} final seat records. Consecutive elections are compared '
        'within each jurisdiction; federal states and territories are also analysed.'.format(
            len(elections), sum(len(e.seats) for e in elections)), '',
        '* Turnout = total ballots / enrolment; formality = formal / total ballots; '
        'formal yield = formal / enrolment. Rate changes are percentage points (pp).',
        '* Category shares use **formal votes**, so they are comparable where category '
        'informality is unavailable. CSVs also include ballot shares and category formality where known.',
        '* Aggregate rates are ratios of summed counts, never means of seat percentages. '
        'Seat distribution statistics give each matched seat equal weight.',
        '* Seat matches use exact source names. They are **not boundary-adjusted**: '
        'redistributions can alter the population of a same-named seat. Unmatched seats '
        'are listed in `matching.csv`; no guessed predecessor mapping is used.',
        '* Category tables use only seats with complete partitions at each election. '
        'Coverage is shown; changing missing-seat coverage can affect aggregate shares.',
        '* Category correlations describe compositional changes, not causal substitution. '
        'Seats from the same election are not independent calibration trials.',
        '* Operational pre-election observations are reserved for the next analysis: '
        'this report establishes final-result dynamics first.',
        '* Excluded operational-only elections: {}.'.format(', '.join(excluded) or 'none'), '',
    ]
    lines += findings(tables)
    lines += [
        '## Election histories', '',
        'Enrolment and formal-vote growth are relative percentages; turnout/formality '
        'changes are pp. Formal-vote growth equals the product of the enrolment, turnout '
        'and formality ratios, exposing changes that a fixed population-growth factor misses.', '',
    ]
    lookup = {(row['current'], row['level'], row['geography']): row for row in changes}
    for jurisdiction in sorted({e.jurisdiction for e in elections}):
        lines += ['### ' + jurisdiction.upper(), '']
        rows = []
        for row in tables['levels']:
            if row['level'] != 'election' or row['jurisdiction'] != jurisdiction:
                continue
            delta = lookup.get((row['election'], 'election', jurisdiction), {})
            rows.append([row['election'], row['seats'], row['enrolment'],
                         row['turnout_pct'], delta.get('turnout_pct_change_pp'),
                         row['formality_pct'], delta.get('formality_pct_change_pp'),
                         row['formal_per_enrolled_pct'], delta.get('enrolment_growth_pct'),
                         delta.get('formal_votes_growth_pct')])
        lines += table(['Election', 'Seats', 'Enrolled', 'Turnout %', 'Delta pp',
                        'Formal %', 'Delta pp', 'Formal/enrolled %', 'Roll growth %',
                        'Formal growth %'], rows) + ['']
    lines += ['## Category allocation changes', '',
              'Entries give previous share -> current share (change in pp). Coverage '
              'counts refer to seats with complete category data. The full per-seat '
              'category changes are in `category_changes.csv`.', '',
              'Federal ordinary + all pre-poll is deliberately combined across the '
              '2010 rule change. Later ordinary/declaration-prepoll detail is retained '
              'in the CSV and the federal detail table below. Victoria combines '
              'provisional/marked-as-voted/older declaration rows; NSW combines '
              'enrolment/provisional rows. Queensland early includes both own-district '
              'and absent early votes; its changing small modes are grouped. WA ordinary '
              'includes mobile polling to match the older report coverage.', '']
    for jurisdiction in sorted({e.jurisdiction for e in elections}):
        lines += ['### ' + jurisdiction.upper(), '']
        rows = []
        for row in categories:
            if row['level'] == 'election' and row['view'] == 'comparison' and row['jurisdiction'] == jurisdiction:
                rows.append([row['previous'] + ' -> ' + row['current'], row['category'],
                             str(row['previous_coverage']) + '/' + str(row['previous_total_seats'])
                             + ' -> ' + str(row['current_coverage']) + '/' + str(row['current_total_seats']),
                             row['previous_formal_share_pct'], row['current_formal_share_pct'],
                             row['formal_share_pct_change_pp']])
        lines += table(['Elections', 'Category', 'Coverage', 'Before %', 'After %', 'Delta pp'], rows) + ['']
    lines += ['### Federal ordinary and declaration pre-poll detail', '',
              'After 2010, ordinary includes election-day and own-division early votes. '
              'Declaration pre-poll is a subset of early voting; its decline does not '
              'measure a decline in all pre-poll voting.', '']
    lines += table(['Elections', 'Category', 'Before %', 'After %', 'Delta pp'], [
        [r['previous'] + ' -> ' + r['current'], r['category'], r['previous_formal_share_pct'],
         r['current_formal_share_pct'], r['formal_share_pct_change_pp']]
        for r in categories if r['level'] == 'election' and r['view'] == 'native'
        and r['category'] in {'ordinary_combined', 'declaration_early', 'election_day_ordinary'}
    ]) + ['']
    lines += ['## Federal state and territory changes', '',
              'State totals include every division in the state at each election. '
              'State minus national turnout change indicates geographic variation '
              'that a single national adjustment would miss.', '']
    rows = []
    for r in changes:
        if r['level'] != 'state':
            continue
        national = lookup[r['current'], 'election', 'fed']
        rows.append([r['previous'] + ' -> ' + r['current'], r['geography'].upper(),
                     r['current_turnout_pct'], r['turnout_pct_change_pp'],
                     r['turnout_pct_change_pp'] - national['turnout_pct_change_pp'],
                     r['formality_pct_change_pp'], r['formal_per_enrolled_pct_change_pp'],
                     r['enrolment_growth_pct'], r['formal_votes_growth_pct']])
    lines += table(['Elections', 'State', 'Turnout %', 'Turnout delta pp', 'Minus national pp',
                    'Formality delta pp', 'Formal yield delta pp', 'Roll growth %', 'Formal growth %'], rows) + ['']
    lines += ['### Federal state category changes', '',
              'The largest ordinary/pre-poll and postal groups are shown here; '
              'all categories and their levels are available in the CSVs.', '']
    lines += table(['Elections', 'State', 'Ordinary + pre-poll delta pp', 'Postal delta pp',
                    'Absent delta pp', 'Provisional delta pp'], state_category_table(categories)) + ['']
    lines += ['## Matched-seat changes and persistence', '',
              'Each row summarises same-named seats in one consecutive-election pair. '
              'P10/P90 bracket the middle 80% of changes. Persistence r correlates '
              'previous and current seat levels within that pair; it is descriptive '
              'and can include redistribution effects.', '']
    pair_keys = sorted({(r['previous'], r['current']) for r in changes if r['level'] == 'seat'})
    for metric in RATE_METRICS + ('enrolment_growth_pct', 'formal_votes_growth_pct'):
        lines += ['### ' + METRIC_LABELS[metric] + (' change (pp)' if metric in RATE_METRICS else ' (%)'), '']
        rows = []
        for old, new in pair_keys:
            selected = [r for r in changes if r['previous'] == old and r['current'] == new and r['level'] == 'seat']
            key = metric + '_change_pp' if metric in RATE_METRICS else metric
            summary = distribution([r[key] for r in selected])
            persistence = correlation([(r['previous_' + metric], r['current_' + metric]) for r in selected]) if metric in RATE_METRICS else None
            rows.append([old + ' -> ' + new] + summary + [persistence])
        lines += table(['Elections', 'N', 'Mean', 'Median', 'P10', 'P90', 'SD', 'Persistence r'], rows) + ['']
    lines += ['### Largest seat rate changes', '',
              'These are investigation candidates, not verified turnout anomalies. '
              'Roll changes and redistributions should be checked before treating '
              'them as behavioural changes.', '']
    for metric in RATE_METRICS:
        lines += ['#### ' + METRIC_LABELS[metric], '']
        selected = sorted((r for r in changes if r['level'] == 'seat'),
                          key=lambda r: abs(r[metric + '_change_pp']), reverse=True)[:12]
        lines += table(['Elections', 'Seat', 'Before %', 'After %', 'Delta pp', 'Roll growth %'], [
            [r['previous'] + ' -> ' + r['current'], r['geography'], r['previous_' + metric],
             r['current_' + metric], r[metric + '_change_pp'], r['enrolment_growth_pct']]
            for r in selected]) + ['']
    lines += ['## Matched-seat category dynamics', '',
              'Category-share and category-formality spreads appear below. Each '
              'seat must have a complete partition at both elections. Category '
              'formality is omitted where informal counts are unavailable.', '']
    rows = []
    for old, new in pair_keys:
        selected = [r for r in categories if r['previous'] == old and r['current'] == new
                    and r['level'] == 'seat' and r['view'] == 'comparison']
        for cat in sorted({r['category'] for r in selected}):
            group = [r for r in selected if r['category'] == cat]
            summary = distribution([r['formal_share_pct_change_pp'] for r in group])
            formality = distribution([r['formality_pct_change_pp'] for r in group])
            rows.append([old + ' -> ' + new, cat] + summary
                        + [formality[0] if formality else 0, formality[1] if formality else None])
    lines += table(['Elections', 'Category', 'N', 'Mean delta pp', 'Median', 'P10', 'P90', 'SD',
                    'Formality N', 'Formality mean delta pp'], rows) + ['']
    lines += ['### Early/postal versus ordinary substitution', '',
              'Within each election pair, correlate matched-seat share changes. '
              'Negative r is compatible with substitution, but also follows from '
              'the fixed-sum nature of shares. Use counts, enrolment and whole-election '
              'holdouts before assigning a predictive coefficient. Queensland category '
              'breaks are excluded here.', '']
    lines += table(['Elections', 'N', 'Early vs ordinary r', 'Postal vs ordinary r',
                    'Early vs turnout r', 'Postal vs turnout r'], substitution_table(changes, categories)) + ['']
    lines += ['## Other relationships relevant to vote-count expectations', '',
              'Within-pair seat correlations help identify predictors for later testing. '
              'Roll growth versus turnout can also reflect redistribution or enrolment '
              'composition. The federal residual SDs compare removing the national '
              'turnout shift with removing each state/territory shift.', '']
    rows = []
    for old, new in pair_keys:
        selected = [r for r in changes if r['previous'] == old and r['current'] == new and r['level'] == 'seat']
        national_residual = distribution([r['turnout_pct_change_minus_election_pp'] for r in selected])
        state_residual = distribution([r['turnout_pct_change_minus_state_pp'] for r in selected])
        rows.append([old + ' -> ' + new, len(selected),
                     correlation([(r['enrolment_growth_pct'], r['turnout_pct_change_pp']) for r in selected]),
                     correlation([(r['turnout_pct_change_pp'], r['formality_pct_change_pp']) for r in selected]),
                     national_residual[-1], state_residual[-1] if state_residual else None])
    lines += table(['Elections', 'N', 'Roll growth vs turnout r', 'Turnout vs formality r',
                    'Turnout residual SD (national)', 'Turnout residual SD (state)'], rows) + ['']
    lines += ['## Coverage and modelling implications', '',
              '* Total formal votes should be decomposed into enrolment, turnout and '
              'formality. A category decline alone does not establish lost turnout.',
              '* Category allocation needs both common election shifts and seat-specific '
              'variation. The paired seat spreads show the residual risk of applying '
              'one uniform multiplier to every seat.',
              '* Federal state differences suggest testing state-level adjustments '
              'before assuming every division follows the national turnout change.',
              '* Early-voting definitions require jurisdiction-specific matching; '
              'federal final category totals alone cannot identify all early ordinary votes.',
              '* WA absent votes can include voting outside the enrolled district before '
              'polling day. The 2025 rise in absent share must be investigated before '
              'interpreting the early-polling-place decline as fewer early voters. '
              'NSW electronic voting disappearing from the 2023 partition is also a '
              'change in available voting modes, not ordinary behavioural substitution.',
              '* Fit/test splits should hold out elections. Inspect COVID-era elections '
              'and category breaks separately; thousands of seats are not thousands '
              'of independent election conditions.',
              '* Next, match the latest pre-election early/postal observations to '
              'these final totals, preserving observation geography, precision and '
              'application/issue/return/acceptance distinctions.', '',
              '### Transition notes', '']
    for r in changes:
        if r['level'] == 'election' and r['notes']:
            lines.append('* {} -> {}: {}.'.format(r['previous'], r['current'], r['notes']))
    lines += ['', '### Missing category seats', '']
    for e in elections:
        missing = sorted(e.seats.keys() - e.categories.keys())
        if missing:
            lines.append('* {}: {}.'.format(e.code, ', '.join(missing)))
    lines += ['', '### Source references', '',
              'Commission sources used by the normalized adapters are listed per '
              'election below; operational sources are not used in this report.', '']
    for e in elections:
        lines.append('* {}: {}.'.format(e.code, '; '.join(
            '[{}]({})'.format(s.authority, s.locator) for s in e.sources)))
    lines += ['', '## Detailed outputs', '',
              '* `levels.csv`: counts and rates for every election, seat and federal state.',
              '* `changes.csv`: all consecutive-election count/rate changes at those levels.',
              '* `category_changes.csv`: category shares, counts, formality and changes; '
              '`comparison` is an exhaustive grouped partition, while federal `native` '
              'rows are overlapping supplementary detail and must not be added to it.',
              '* `native_categories.csv`: original canonical-category levels, including '
              'definitions that cannot be compared directly across all years.',
              '* `matching.csv`: exact-name match counts and unmatched seats.', '']
    return '\n'.join(lines)


def findings(tables):
    """Lead with computed observations; interpretation remains exploratory."""
    lines = ['## Main observations', '']
    levels = [r for r in tables['levels'] if r['level'] == 'election']
    for jurisdiction in sorted({r['jurisdiction'] for r in levels}):
        history = sorted((r for r in levels if r['jurisdiction'] == jurisdiction), key=lambda r: r['election'])
        first, last = history[0], history[-1]
        lines.append('* {} turnout: {:.2f}% in {} -> {:.2f}% in {} ({:+.2f} pp); '
                     'formality changed by {:+.2f} pp; formal votes per enrolled elector '
                     'changed by {:+.2f} pp.'.format(
                         jurisdiction.upper(), first['turnout_pct'], first['election'],
                         last['turnout_pct'], last['election'], last['turnout_pct'] - first['turnout_pct'],
                         last['formality_pct'] - first['formality_pct'],
                         last['formal_per_enrolled_pct'] - first['formal_per_enrolled_pct']))
    lines += ['', 'Largest category shifts at each jurisdiction\'s latest transition '
              '(formal-vote shares; see coverage and definition notes below):', '']
    rows = []
    for jurisdiction in sorted({r['jurisdiction'] for r in levels}):
        groups = [r for r in tables['category_changes'] if r['level'] == 'election'
                  and r['view'] == 'comparison' and r['jurisdiction'] == jurisdiction]
        if not groups:
            continue
        latest = max(r['current'] for r in groups)
        for row in sorted((r for r in groups if r['current'] == latest),
                          key=lambda r: abs(r['formal_share_pct_change_pp']), reverse=True)[:2]:
            rows.append([row['previous'] + ' -> ' + row['current'], row['category'],
                         row['previous_formal_share_pct'], row['current_formal_share_pct'],
                         row['formal_share_pct_change_pp']])
    lines += table(['Elections', 'Category', 'Before %', 'After %', 'Delta pp'], rows)
    lines += ['', 'These are observed trends, not a recommendation to extrapolate '
              'them linearly. Formal counts can grow despite falling turnout because '
              'the enrolled population grows. COVID, voting rules and reporting '
              'coverage require separate checks.', '']
    return lines


def state_category_table(categories):
    grouped = defaultdict(dict)
    for row in categories:
        if row['level'] == 'state' and row['view'] == 'comparison':
            grouped[row['previous'], row['current'], row['geography']][row['category']] = row['formal_share_pct_change_pp']
    return [[old + ' -> ' + new, state.upper()] + [values.get(key) for key in
            ('ordinary_and_all_prepoll', 'postal', 'absent', 'provisional')]
            for (old, new, state), values in sorted(grouped.items())]


def substitution_table(changes, categories):
    seat_changes = {(r['previous'], r['current'], r['geography']): r
                    for r in changes if r['level'] == 'seat'}
    grouped = defaultdict(dict)
    for r in categories:
        if r['level'] == 'seat' and r['view'] == 'comparison':
            grouped[r['previous'], r['current'], r['geography']][r['category']] = r['formal_share_pct_change_pp']
    output = []
    for old, new in sorted({(a, b) for a, b, name in grouped}):
        names = [name for a, b, name in grouped if (a, b) == (old, new)]
        records = [seat_changes[old, new, name] for name in names]
        if any('category break' in r['notes'] or 'mode comparability' in r['notes'] for r in records):
            continue
        early, ordinary, postal, turnout = [], [], [], []
        for name in names:
            values = grouped[old, new, name]
            early.append(next((values[k] for k in ('early_combined', 'early_in_person', 'all_early_in_person') if k in values), None))
            ordinary.append(next((values[k] for k in ('election_day_ordinary', 'ordinary_including_mobile', 'ordinary_and_all_prepoll') if k in values), None))
            postal.append(values.get('postal'))
            turnout.append(seat_changes[old, new, name]['turnout_pct_change_pp'])
        output.append([old + ' -> ' + new, len(names), correlation(zip(early, ordinary)),
                       correlation(zip(postal, ordinary)), correlation(zip(early, turnout)),
                       correlation(zip(postal, turnout))])
    return output


def write_csv(path, rows):
    fields = list(dict.fromkeys(key for row in rows for key in row))
    with path.open('w', encoding='utf-8', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-directory', type=Path, default=ROOT / 'analysis/Data/Turnout')
    parser.add_argument('--output-directory', type=Path, default=ROOT / 'docs/turnout-election-changes')
    args = parser.parse_args(argv)
    print('Loading and validating final turnout datasets...', flush=True)
    elections, excluded = load_elections(args.input_directory)
    if not elections:
        parser.error('no final-result elections found')
    print('Analysing {} elections and {} seats...'.format(
        len(elections), sum(len(e.seats) for e in elections)), flush=True)
    tables = build_tables(elections)
    report = render_report(elections, excluded, tables)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    for name, rows in tables.items():
        write_csv(args.output_directory / (name + '.csv'), rows)
    (args.output_directory / 'report.md').write_text(report, encoding='utf-8')
    print('Report written to {}'.format(args.output_directory / 'report.md'))


if __name__ == '__main__':
    main()

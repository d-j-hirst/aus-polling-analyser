"""Download and normalize final South Australian turnout evidence.

ECSA's election-statistics reports publish district enrolment and a complete
ordinary/declaration ballot-paper split, including formal and informal votes.
The 2022 election exposes the same evidence in two purpose-built CSV files.
The declaration aggregate is deliberately retained: the source does not
support a defensible split into early, postal, absent and provisional votes.

Main functions:
* ``extract_pdf_tables`` reads only the configured report pages with pypdf.
* ``parse_pdf_tables`` extracts and reconciles the two historical tables.
* ``parse_2022_csvs`` validates the equivalent 2022 result and enrolment CSVs.
* ``build_dataset`` creates the normalized complete two-category partition.
* ``coverage_summary`` reports turnout, informality and category totals.
* ``main`` provides the ``--election`` and output-directory command line flow.
"""

import argparse
import csv
from dataclasses import dataclass
from io import BytesIO, StringIO
from pathlib import Path
import re
from urllib.request import Request, urlopen

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 60
DOWNLOADS_PAGE = (
    'https://www.ecsa.sa.gov.au/elections/past-state-election-results'
    '?catid=12:elections&id=636:2022-state-election-results-and-statistics-downloads'
    '&view=article'
)


@dataclass(frozen=True)
class SaElection:
    election_code: str
    election_date: str
    results_url: str
    expected_totals: tuple
    source_format: str
    enrolment_url: str = ''
    enrolment_page: int = -1
    ballot_page: int = -1
    expected_districts: int = 47


@dataclass(frozen=True)
class DistrictTurnout:
    name: str
    enrolment: int
    ordinary_formal: int
    ordinary_informal: int
    ordinary_total: int
    declaration_formal: int
    declaration_informal: int
    declaration_total: int
    formal: int
    informal: int
    total: int


_REPORT_ROOT = 'https://www.ecsa.sa.gov.au/component/edocman/'
_2022_RESULTS_URL = (
    _REPORT_ROOT
    + '239-2022se-ha-ordinary%2C-declaration-and-total-ballot-papers-formal-informal/download'
)
_2022_ENROLMENT_URL = (
    _REPORT_ROOT
    + '19-2022se-enrolment-breakdown-by-district-and-gender-1/download'
)
ELECTIONS = {
    '2006sa': SaElection(
        '2006sa',
        '2006-03-18',
        _REPORT_ROOT + '2006-state-election-statistics/download',
        (1055347, 772896, 29936, 802832, 166265, 5093, 171358,
         939161, 35029, 974190),
        'pdf',
        enrolment_page=220,
        ballot_page=221,
    ),
    '2010sa': SaElection(
        '2010sa',
        '2010-03-20',
        _REPORT_ROOT + '2010-state-election-statistics/download',
        (1093316, 770234, 27670, 797904, 210435, 6092, 216527,
         980669, 33762, 1014431),
        'pdf',
        enrolment_page=458,
        ballot_page=459,
    ),
    '2014sa': SaElection(
        '2014sa',
        '2014-03-15',
        _REPORT_ROOT + '2014-state-election-statistics/download',
        (1142419, 759207, 25986, 785193, 258649, 6517, 265166,
         1017856, 32503, 1050359),
        'pdf',
        enrolment_page=238,
        ballot_page=239,
    ),
    '2018sa': SaElection(
        '2018sa',
        '2018-03-17',
        _REPORT_ROOT + '2018-state-election-statistics-report/download',
        (1201775, 761670, 34592, 796262, 287043, 10229, 297272,
         1048713, 44821, 1093534),
        'pdf',
        enrolment_page=247,
        ballot_page=248,
    ),
    '2022sa': SaElection(
        '2022sa',
        '2022-03-19',
        _2022_RESULTS_URL,
        (1266719, 663966, 24887, 688853, 427207, 11582, 438789,
         1091173, 36469, 1127642),
        'csv',
        enrolment_url=_2022_ENROLMENT_URL,
    ),
}

_CANONICAL_DISPLAY_NAMES = {
    'hammond': 'Hammond',
    'hurtlevale': 'Hurtle Vale',
    'littlepara': 'Little Para',
    'mackillop': 'MacKillop',
    'mountgambier': 'Mount Gambier',
    'portadelaide': 'Port Adelaide',
    'westtorrens': 'West Torrens',
}


def _download(url):
    request = Request(
        url,
        headers={
            'User-Agent': (
                'AEF turnout research '
                '(https://www.aeforecasts.com/; aeforecasts@gmail.com)'
            )
        },
    )
    with urlopen(request, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response:
        return response.read()


def _count(value, label):
    normalized = str(value).replace(',', '').replace(' ', '').strip()
    try:
        count = int(normalized)
    except (TypeError, ValueError):
        raise turnout_data.TurnoutDataError(
            '{} has invalid count {!r}'.format(label, value)
        )
    if count < 0:
        raise turnout_data.TurnoutDataError('{} has negative count'.format(label))
    return count


def _layout_fields(line):
    return [field.strip() for field in re.split(r'\s{2,}', line.strip()) if field.strip()]


def _district_name(value):
    # Some ECSA PDF text layers insert a space after the initial letter in
    # names such as "W aite" while retaining normal inter-word spaces.
    return re.sub(r'\b([A-Z]) (?=[a-z])', r'\1', value.strip())


def _repair_split_district_field(fields):
    if (
        len(fields) >= 2
        and len(fields[0]) == 1
        and fields[0].isupper()
        and fields[1]
        and fields[1][0].islower()
    ):
        return [fields[0] + fields[1]] + fields[2:]
    return fields


def _compact_name(value):
    return ''.join(character for character in value if character.isalnum()).casefold()


def _word_boundaries(value):
    boundaries = set()
    position = 0
    for character in value.strip():
        if character.isspace():
            boundaries.add(position)
        elif character.isalnum():
            position += 1
    return boundaries


def _reconciled_name(first, second):
    compact_first = ''.join(character for character in first if character.isalnum())
    compact_second = ''.join(character for character in second if character.isalnum())
    if compact_first.casefold() != compact_second.casefold():
        raise turnout_data.TurnoutDataError(
            'cannot reconcile ECSA district names {!r} and {!r}'.format(first, second)
        )
    known_name = _CANONICAL_DISPLAY_NAMES.get(compact_first.casefold())
    if known_name is not None:
        return known_name
    boundaries = _word_boundaries(first) & _word_boundaries(second)
    return ''.join(
        (' ' if index in boundaries else '') + character
        for index, character in enumerate(compact_first)
    )


def extract_pdf_tables(pdf_data, election):
    """Extract the configured ECSA enrolment and ballot-summary pages."""
    try:
        from pypdf import PdfReader
    except ImportError:
        raise turnout_data.TurnoutDataError(
            'turnout_sa.py requires pypdf; install analysis/requirements.txt'
        )

    try:
        reader = PdfReader(BytesIO(pdf_data))
        page_numbers = (election.enrolment_page, election.ballot_page)
        if min(page_numbers) < 0 or max(page_numbers) >= len(reader.pages):
            raise turnout_data.TurnoutDataError(
                '{} report has {} pages, expected pages {} and {}'.format(
                    election.election_code,
                    len(reader.pages),
                    election.enrolment_page + 1,
                    election.ballot_page + 1,
                )
            )
        texts = tuple(
            reader.pages[index].extract_text(extraction_mode='layout') or ''
            for index in page_numbers
        )
    except turnout_data.TurnoutDataError:
        raise
    except Exception as error:
        raise turnout_data.TurnoutDataError(
            '{} is not a readable PDF: {}'.format(election.election_code, error)
        )

    enrolment_text, ballot_text = texts
    normalized_enrolment = ' '.join(enrolment_text.upper().split())
    normalized_ballots = ' '.join(ballot_text.upper().split())
    if 'ELECTOR, VOTER, INFORMAL AND DECLARATION' not in normalized_enrolment:
        raise turnout_data.TurnoutDataError(
            '{} enrolment page does not contain the expected table'.format(
                election.election_code
            )
        )
    ballot_heading = 'ORDINARY, DECLARATION AND TOTAL BALLOT PAPERS'
    if ballot_heading not in normalized_ballots:
        raise turnout_data.TurnoutDataError(
            '{} ballot page does not contain the expected table'.format(
                election.election_code
            )
        )
    return texts


def _parse_pdf_enrolments(text):
    records = {}
    published_total = None
    for line in text.splitlines():
        fields = _repair_split_district_field(_layout_fields(line))
        if len(fields) != 8:
            continue
        try:
            enrolment = _count(fields[1], '{} enrolment'.format(fields[0]))
            _count(fields[2], '{} voters'.format(fields[0]))
            _count(fields[4], '{} informal votes'.format(fields[0]))
            _count(fields[6], '{} declaration votes'.format(fields[0]))
            float(fields[3])
            float(fields[5])
            float(fields[7])
        except (turnout_data.TurnoutDataError, ValueError):
            continue
        name = _district_name(fields[0])
        if name in ('Total', 'Totals'):
            published_total = enrolment
        elif _compact_name(name) in records:
            raise turnout_data.TurnoutDataError(
                'duplicate ECSA enrolment district {}'.format(name)
            )
        else:
            records[_compact_name(name)] = (name, enrolment)
    return records, published_total


def _parse_pdf_ballots(text):
    records = {}
    published_totals = None
    for line in text.splitlines():
        fields = _repair_split_district_field(_layout_fields(line))
        if len(fields) != 10:
            continue
        try:
            values = tuple(
                _count(value, '{} ballot table'.format(fields[0]))
                for value in fields[1:]
            )
        except turnout_data.TurnoutDataError:
            continue
        name = _district_name(fields[0])
        if name in ('Total', 'Totals'):
            published_totals = values
        elif _compact_name(name) in records:
            raise turnout_data.TurnoutDataError(
                'duplicate ECSA ballot district {}'.format(name)
            )
        else:
            records[_compact_name(name)] = (name, values)
    return records, published_totals


def _validate_record(record):
    if record.ordinary_formal + record.ordinary_informal != record.ordinary_total:
        raise turnout_data.TurnoutDataError(
            '{} ordinary ballots do not reconcile'.format(record.name)
        )
    if (
        record.declaration_formal + record.declaration_informal
        != record.declaration_total
    ):
        raise turnout_data.TurnoutDataError(
            '{} declaration ballots do not reconcile'.format(record.name)
        )
    if record.ordinary_formal + record.declaration_formal != record.formal:
        raise turnout_data.TurnoutDataError(
            '{} category formal votes do not reconcile'.format(record.name)
        )
    if record.ordinary_informal + record.declaration_informal != record.informal:
        raise turnout_data.TurnoutDataError(
            '{} category informal votes do not reconcile'.format(record.name)
        )
    if record.ordinary_total + record.declaration_total != record.total:
        raise turnout_data.TurnoutDataError(
            '{} category ballot totals do not reconcile'.format(record.name)
        )
    if record.total > record.enrolment:
        raise turnout_data.TurnoutDataError(
            '{} total ballots exceed enrolment'.format(record.name)
        )


def parse_pdf_tables(enrolment_text, ballot_text, election):
    """Parse and reconcile one pair of ECSA report tables."""
    enrolments, published_enrolment = _parse_pdf_enrolments(enrolment_text)
    ballots, published_ballots = _parse_pdf_ballots(ballot_text)
    expected_keys = set(enrolments)
    if expected_keys != set(ballots):
        missing_enrolment = sorted(
            ballots[key][0] for key in set(ballots) - expected_keys
        )
        missing_ballots = sorted(
            enrolments[key][0] for key in expected_keys - set(ballots)
        )
        raise turnout_data.TurnoutDataError(
            '{} district identity mismatch; missing enrolment: {}; missing ballots: {}'.format(
                election.election_code,
                ', '.join(missing_enrolment) or 'none',
                ', '.join(missing_ballots) or 'none',
            )
        )
    if len(expected_keys) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} contains {} districts, expected {}'.format(
                election.election_code, len(expected_keys), election.expected_districts
            )
        )

    records = []
    for key in sorted(expected_keys):
        enrolment_name, enrolment = enrolments[key]
        ballot_name, ballot_values = ballots[key]
        name = _reconciled_name(enrolment_name, ballot_name)
        record = DistrictTurnout(name, enrolment, *ballot_values)
        _validate_record(record)
        records.append(record)
    _validate_aggregates(
        records,
        election,
        published_enrolment,
        published_ballots,
    )
    return records


def _csv_rows(data, label):
    try:
        text = data.decode('utf-8-sig') if isinstance(data, bytes) else data
        return list(csv.reader(StringIO(text)))
    except (UnicodeDecodeError, csv.Error, TypeError) as error:
        raise turnout_data.TurnoutDataError(
            '{} is not readable CSV: {}'.format(label, error)
        )


def parse_2022_csvs(results_data, enrolment_data, election):
    """Parse ECSA's final 2022 ballot-summary and enrolment CSVs."""
    results_rows = _csv_rows(results_data, '2022 ECSA ballot summary')
    enrolment_rows = _csv_rows(enrolment_data, '2022 ECSA enrolment')
    if (
        len(results_rows) < 6
        or '2022 State Election' not in results_rows[0][0]
        or results_rows[1][:12] != [
            'District', 'Ordinary Ballot Papers', '', '', '', '',
            'Declaration Ballot Papers', '', '', '', '', 'Total Ballot Papers',
        ]
    ):
        raise turnout_data.TurnoutDataError(
            '2022 ECSA ballot summary has unexpected identity or headers'
        )
    if (
        len(enrolment_rows) < 4
        or '2022 State Election' not in enrolment_rows[0][0]
        or enrolment_rows[1][0] != 'District'
        or enrolment_rows[1][7] != 'Total enrolments'
    ):
        raise turnout_data.TurnoutDataError(
            '2022 ECSA enrolment CSV has unexpected identity or headers'
        )

    enrolments = {}
    published_enrolment = None
    for row in enrolment_rows[2:]:
        if len(row) < 8 or not row[0].strip():
            continue
        name = row[0].strip()
        value = _count(row[7], '{} enrolment'.format(name))
        if name == 'Total':
            published_enrolment = value
        elif name in enrolments:
            raise turnout_data.TurnoutDataError(
                'duplicate ECSA enrolment district {}'.format(name)
            )
        else:
            enrolments[name] = value

    ballots = {}
    published_ballots = None
    count_columns = (1, 3, 5, 6, 8, 10, 11, 13, 15)
    for row in results_rows[4:]:
        if len(row) < 16 or not row[0].strip():
            continue
        name = row[0].strip()
        values = tuple(
            _count(row[index], '{} ballot summary'.format(name))
            for index in count_columns
        )
        if name == 'Total':
            published_ballots = values
        elif name in ballots:
            raise turnout_data.TurnoutDataError(
                'duplicate ECSA ballot district {}'.format(name)
            )
        else:
            ballots[name] = values

    expected_names = set(enrolments)
    if expected_names != set(ballots):
        raise turnout_data.TurnoutDataError(
            '{} enrolment and ballot CSV district identities differ'.format(
                election.election_code
            )
        )
    if len(expected_names) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} contains {} districts, expected {}'.format(
                election.election_code, len(expected_names), election.expected_districts
            )
        )

    records = []
    for name in sorted(expected_names):
        record = DistrictTurnout(name, enrolments[name], *ballots[name])
        _validate_record(record)
        records.append(record)
    _validate_aggregates(
        records,
        election,
        published_enrolment,
        published_ballots,
    )
    return records


def _aggregate_totals(records):
    fields = (
        'enrolment',
        'ordinary_formal',
        'ordinary_informal',
        'ordinary_total',
        'declaration_formal',
        'declaration_informal',
        'declaration_total',
        'formal',
        'informal',
        'total',
    )
    return tuple(sum(getattr(record, field) for record in records) for field in fields)


def _validate_aggregates(records, election, published_enrolment, published_ballots):
    if published_enrolment is None or published_ballots is None:
        raise turnout_data.TurnoutDataError(
            '{} source is missing its published total row'.format(election.election_code)
        )
    published = (published_enrolment,) + published_ballots
    if published != election.expected_totals:
        raise turnout_data.TurnoutDataError(
            '{} published totals {} differ from configured controls {}'.format(
                election.election_code, published, election.expected_totals
            )
        )
    actual = _aggregate_totals(records)
    if actual != published:
        raise turnout_data.TurnoutDataError(
            '{} district totals {} differ from published {}'.format(
                election.election_code, actual, published
            )
        )


def build_dataset(election, records):
    """Build a normalized complete ordinary/declaration partition."""
    source_id = 'ecsa-{}-assembly-final'.format(election.election_code[:4])
    locator = election.results_url if election.source_format == 'pdf' else DOWNLOADS_PAGE
    source_notes = (
        'The election-statistics report tables provide district enrolment and '
        'ordinary/declaration formal, informal and total ballot papers.'
        if election.source_format == 'pdf'
        else
        'The final ballot-summary and district-enrolment CSVs are combined as '
        'one validated source bundle; their district identities and statewide '
        'controls must match.'
    )
    source_notes += (
        ' Declaration ballot papers combine pre-poll, postal, absent and other '
        'declaration modes and are not split by this adapter.'
    )
    dataset = turnout_data.TurnoutDataset(
        elections=[
            turnout_data.ElectionDefinition(
                election.election_code, election.election_date, 'sa'
            )
        ],
        sources=[
            turnout_data.SourceDefinition(
                source_id=source_id,
                election_code=election.election_code,
                authority='Electoral Commission of South Australia',
                locator=locator,
                adapter=(
                    'ecsa-election-statistics-pdf-v1'
                    if election.source_format == 'pdf'
                    else 'ecsa-final-csv-bundle-v1'
                ),
                status='final',
                category_regime='ecsa-ordinary-declaration-v1',
                notes=source_notes,
            )
        ],
    )
    for record in sorted(records, key=lambda item: item.name):
        dataset.seat_totals.append(
            turnout_data.SeatTotal(
                election_code=election.election_code,
                seat_name=record.name,
                source_id=source_id,
                enrolment=record.enrolment,
                formal_votes=record.formal,
                informal_votes=record.informal,
                total_ballots=record.total,
                subdivision='sa',
            )
        )
        for source_category, canonical_category, formal, informal, total in (
            (
                'Ordinary Ballot Papers',
                'election_day_ordinary',
                record.ordinary_formal,
                record.ordinary_informal,
                record.ordinary_total,
            ),
            (
                'Declaration Ballot Papers',
                'declaration_combined',
                record.declaration_formal,
                record.declaration_informal,
                record.declaration_total,
            ),
        ):
            dataset.vote_types.append(
                turnout_data.VoteTypeRecord(
                    election_code=election.election_code,
                    seat_name=record.name,
                    source_id=source_id,
                    partition_id='final-ballot-types',
                    source_category=source_category,
                    canonical_category=canonical_category,
                    formal_votes=formal,
                    informal_votes=informal,
                    total_ballots=total,
                    coverage='complete',
                )
            )
    dataset.validate()
    return dataset


def coverage_summary(dataset):
    """Return compact election-wide coverage totals for inspection."""
    enrolment = sum(record.enrolment for record in dataset.seat_totals)
    ballots = sum(record.total_ballots for record in dataset.seat_totals)
    informal = sum(record.informal_votes for record in dataset.seat_totals)
    categories = {}
    for record in dataset.vote_types:
        category = record.canonical_category
        category_totals = categories.setdefault(
            category, {'formal_votes': 0, 'informal_votes': 0, 'total_ballots': 0}
        )
        category_totals['formal_votes'] += record.formal_votes
        category_totals['informal_votes'] += record.informal_votes
        category_totals['total_ballots'] += record.total_ballots
    return {
        'districts': len(dataset.seat_totals),
        'enrolment': enrolment,
        'total_ballots': ballots,
        'turnout_percent': ballots / enrolment * 100.0,
        'informal_percent': informal / ballots * 100.0,
        'categories': categories,
    }


def _print_summary(election_code, summary, output_path):
    print(
        '{}: {} districts; {:,} ballots from {:,} enrolled '
        '({:.2f}% turnout, {:.2f}% informal)'.format(
            election_code,
            summary['districts'],
            summary['total_ballots'],
            summary['enrolment'],
            summary['turnout_percent'],
            summary['informal_percent'],
        )
    )
    for category, counts in sorted(summary['categories'].items()):
        print(
            '  {:24} {:>10,} ballots ({:>8,} formal; {:>6,} informal)'.format(
                category,
                counts['total_ballots'],
                counts['formal_votes'],
                counts['informal_votes'],
            )
        )
    print('  Wrote {}'.format(output_path))


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize final South Australian turnout evidence.'
    )
    parser.add_argument(
        '--election',
        action='append',
        choices=tuple(ELECTIONS) + ('all',),
        required=True,
        help='Election to acquire; repeat for multiple elections or use all.',
    )
    parser.add_argument(
        '--output-directory',
        type=Path,
        default=DEFAULT_OUTPUT_DIRECTORY,
    )
    return parser.parse_args(args)


def main(args=None):
    options = parse_args(args)
    selected = list(ELECTIONS) if 'all' in options.election else options.election
    selected = list(dict.fromkeys(selected))
    for election_code in selected:
        election = ELECTIONS[election_code]
        if election.source_format == 'pdf':
            print('Downloading final ECSA statistics report for {}...'.format(election_code))
            report_data = _download(election.results_url)
            enrolment_text, ballot_text = extract_pdf_tables(report_data, election)
            records = parse_pdf_tables(enrolment_text, ballot_text, election)
        else:
            print('Downloading final ECSA result CSVs for {}...'.format(election_code))
            results_data = _download(election.results_url)
            enrolment_data = _download(election.enrolment_url)
            records = parse_2022_csvs(results_data, enrolment_data, election)
        dataset = build_dataset(election, records)
        output_path = options.output_directory / '{}.json'.format(election_code)
        turnout_data.write_dataset_atomically(output_path, dataset)
        _print_summary(election_code, coverage_summary(dataset), output_path)
    return 0


if __name__ == '__main__':
    main()

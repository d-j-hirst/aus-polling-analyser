"""Download and normalize operational Queensland turnout evidence.

This adapter preserves ECQ's published counts without treating mark-offs as
final ballots. Early in-person figures are attached to the district
administering the polling place; postal figures are attached to the elector's
district. Both retained 2024 workbooks are final-reconciled publications.

Main functions:
* ``download_election_files`` retrieves the official workbooks, with archived
  copies of the same ECQ files as fallbacks for ECQ's anti-bot response.
* ``read_xlsx_rows`` reads the small tabular subset of XLSX needed here.
* ``build_observations`` validates district coverage and published totals.
* ``merge_dataset`` replaces this adapter's prior records in the final dataset.
* ``main`` provides election selection, reporting and atomic publication.
"""

import argparse
from dataclasses import dataclass
from datetime import date, datetime, timedelta
from io import BytesIO
from pathlib import Path
import re
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET
from zipfile import BadZipFile, ZipFile

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 60
ADAPTER_ID = 'ecq-pre-election-operational-v1'

EARLY_ATTENDANCE_MEASURE = 'early_in_person_attendance_cumulative'
POSTAL_ISSUED_MEASURE = 'postal_ballots_issued_cumulative'
POSTAL_RETURNED_MEASURE = 'postal_ballots_returned_cumulative'
POSTAL_ACCEPTED_MEASURE = 'postal_votes_accepted_cumulative'

_SPREADSHEET_NAMESPACE = (
    'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
)
_CELL_REFERENCE = re.compile(r'^([A-Z]+)([1-9][0-9]*)$')


@dataclass(frozen=True)
class QldOperationalElection:
    election_code: str
    election_date: str
    attendance_url: str
    attendance_archive_url: str
    postal_url: str
    postal_archive_url: str


_ECQ_2024_DIRECTORY = (
    'https://www.ecq.qld.gov.au/_resource/documents/pdf/elections/'
    'election-events/sge/2024-sge/'
)
_ARCHIVE_PREFIX = 'https://web.archive.org/web/20250708034100id_/'
ELECTIONS = {
    '2024qld': QldOperationalElection(
        election_code='2024qld',
        election_date='2024-10-26',
        attendance_url=(
            _ECQ_2024_DIRECTORY + 'SGE-Daily-InPerson-Attendance_271024.xlsx'
        ),
        attendance_archive_url=(
            _ARCHIVE_PREFIX + _ECQ_2024_DIRECTORY
            + 'SGE-Daily-InPerson-Attendance_271024.xlsx'
        ),
        postal_url=(
            _ECQ_2024_DIRECTORY + '2024-SGE-Postal-votes-issued_051124.xlsx'
        ),
        postal_archive_url=(
            _ARCHIVE_PREFIX + _ECQ_2024_DIRECTORY
            + '2024-SGE-Postal-votes-issued_051124.xlsx'
        ),
    ),
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
        data = response.read()
    if not data.startswith(b'PK'):
        raise turnout_data.TurnoutDataError(
            '{} did not return an XLSX workbook'.format(url)
        )
    return data


def _download_with_fallback(primary_url, archive_url):
    try:
        return _download(primary_url)
    except (
        HTTPError,
        URLError,
        TimeoutError,
        OSError,
        turnout_data.TurnoutDataError,
    ) as primary_error:
        try:
            return _download(archive_url)
        except (
            HTTPError,
            URLError,
            TimeoutError,
            OSError,
            turnout_data.TurnoutDataError,
        ) as archive_error:
            raise turnout_data.TurnoutDataError(
                'could not download {} ({}) or its archived official copy ({})'
                .format(primary_url, primary_error, archive_error)
            )


def download_election_files(election):
    """Download both official ECQ workbooks for one election."""
    return {
        'attendance': _download_with_fallback(
            election.attendance_url, election.attendance_archive_url
        ),
        'postal': _download_with_fallback(
            election.postal_url, election.postal_archive_url
        ),
    }


def _shared_strings(archive, label):
    try:
        root = ET.fromstring(archive.read('xl/sharedStrings.xml'))
    except KeyError:
        return []
    except ET.ParseError as error:
        raise turnout_data.TurnoutDataError(
            '{} has invalid shared strings XML: {}'.format(label, error)
        )
    namespace = {'m': _SPREADSHEET_NAMESPACE}
    return [
        ''.join(text.text or '' for text in item.findall('.//m:t', namespace))
        for item in root.findall('m:si', namespace)
    ]


def read_xlsx_rows(data, label):
    """Return sheet-one values indexed by one-based row and column letters."""
    try:
        with ZipFile(BytesIO(data)) as archive:
            strings = _shared_strings(archive, label)
            sheet = ET.fromstring(archive.read('xl/worksheets/sheet1.xml'))
    except (BadZipFile, KeyError, ET.ParseError) as error:
        raise turnout_data.TurnoutDataError(
            '{} is not a readable XLSX workbook: {}'.format(label, error)
        )

    namespace = {'m': _SPREADSHEET_NAMESPACE}
    rows = {}
    for row in sheet.findall('.//m:sheetData/m:row', namespace):
        row_number = int(row.get('r'))
        values = {}
        for cell in row.findall('m:c', namespace):
            reference = cell.get('r', '')
            match = _CELL_REFERENCE.fullmatch(reference)
            if match is None or int(match.group(2)) != row_number:
                raise turnout_data.TurnoutDataError(
                    '{} has invalid cell reference {!r}'.format(label, reference)
                )
            column = match.group(1)
            value_node = cell.find('m:v', namespace)
            value = '' if value_node is None else (value_node.text or '')
            cell_type = cell.get('t')
            if cell_type == 's' and value:
                try:
                    value = strings[int(value)]
                except (ValueError, IndexError):
                    raise turnout_data.TurnoutDataError(
                        '{} has invalid shared string at {}'.format(label, reference)
                    )
            elif cell_type == 'inlineStr':
                value = ''.join(
                    text.text or ''
                    for text in cell.findall('.//m:t', namespace)
                )
            elif cell_type not in (None, 'n'):
                raise turnout_data.TurnoutDataError(
                    '{} has unsupported cell type {!r} at {}'.format(
                        label, cell_type, reference
                    )
                )
            values[column] = value.strip()
        rows[row_number] = values
    return rows


def _parse_count(value, label):
    try:
        count = int(value.replace(',', ''))
    except (AttributeError, ValueError):
        raise turnout_data.TurnoutDataError(
            '{} has invalid count {!r}'.format(label, value)
        )
    if count < 0:
        raise turnout_data.TurnoutDataError('{} has negative count'.format(label))
    return count


def _parse_date(value, label):
    for date_format in ('%d/%m/%Y', '%Y-%m-%d'):
        try:
            return datetime.strptime(value, date_format).date()
        except (TypeError, ValueError):
            pass
    try:
        serial = int(value)
    except (TypeError, ValueError):
        raise turnout_data.TurnoutDataError(
            '{} has invalid date {!r}'.format(label, value)
        )
    return date(1899, 12, 30) + timedelta(days=serial)


def _seat_index(dataset):
    indexed = {}
    for seat in dataset.seat_totals:
        key = seat.seat_name.casefold()
        if key in indexed:
            raise turnout_data.TurnoutDataError(
                'duplicate final district identity {!r}'.format(seat.seat_name)
            )
        indexed[key] = seat.seat_name
    return indexed


def _resolve_seat(value, seats, label):
    try:
        return seats[value.strip().casefold()]
    except (AttributeError, KeyError):
        raise turnout_data.TurnoutDataError(
            '{} references unknown district {!r}'.format(label, value)
        )


def _validate_coverage(represented, seats, label):
    expected = set(seats.values())
    if represented != expected:
        raise turnout_data.TurnoutDataError(
            '{} district coverage differs; missing={}, extra={}'.format(
                label,
                sorted(expected - represented),
                sorted(represented - expected),
            )
        )


def _parse_attendance(election, data, dataset, source_id):
    rows = read_xlsx_rows(data, 'ECQ attendance workbook')
    if rows.get(1, {}).get('A') != 'In-person voting attendance (mark-off)':
        raise turnout_data.TurnoutDataError(
            'ECQ attendance workbook has an unexpected title'
        )
    header = rows.get(11, {})
    if header.get('A') != 'State electorate' or header.get('M') != 'TOTAL':
        raise turnout_data.TurnoutDataError(
            'ECQ attendance workbook has an unexpected table header'
        )

    columns = tuple(chr(code) for code in range(ord('B'), ord('M')))
    dated_columns = tuple(
        (column, _parse_date(header.get(column), 'attendance {}'.format(column)))
        for column in columns
    )
    election_date = date.fromisoformat(election.election_date)
    expected_dates = (
        date(2024, 10, 14), date(2024, 10, 15), date(2024, 10, 16),
        date(2024, 10, 17), date(2024, 10, 18), date(2024, 10, 21),
        date(2024, 10, 22), date(2024, 10, 23), date(2024, 10, 24),
        date(2024, 10, 25), election_date,
    )
    if tuple(item[1] for item in dated_columns) != expected_dates:
        raise turnout_data.TurnoutDataError(
            'ECQ attendance workbook has unexpected voting dates'
        )

    seats = _seat_index(dataset)
    represented = set()
    daily_totals = {observed_date: 0 for _column, observed_date in dated_columns}
    observations = []
    for row_number in range(13, 13 + len(seats)):
        row = rows.get(row_number, {})
        seat_name = _resolve_seat(
            row.get('A'), seats, 'attendance row {}'.format(row_number)
        )
        if seat_name in represented:
            raise turnout_data.TurnoutDataError(
                'ECQ attendance workbook repeats {}'.format(seat_name)
            )
        represented.add(seat_name)
        cumulative = 0
        row_total = 0
        for column, observed_date in dated_columns:
            count = _parse_count(
                row.get(column), '{} attendance {}'.format(seat_name, observed_date)
            )
            row_total += count
            daily_totals[observed_date] += count
            if observed_date < election_date:
                cumulative += count
                observations.append(turnout_data.OperationalObservation(
                    election_code=election.election_code,
                    source_id=source_id,
                    measure=EARLY_ATTENDANCE_MEASURE,
                    observed_at=observed_date.isoformat(),
                    count=cumulative,
                    geography_basis='administering_division',
                    observation_status='final_reconciled',
                    seat_name=seat_name,
                    source_category='In-person voting attendance (mark-off)',
                ))
        reported_total = _parse_count(
            row.get('M'), '{} attendance total'.format(seat_name)
        )
        if row_total != reported_total:
            raise turnout_data.TurnoutDataError(
                '{} attendance rows total {}, expected {}'.format(
                    seat_name, row_total, reported_total
                )
            )
    _validate_coverage(represented, seats, 'ECQ attendance workbook')

    total_row = rows.get(12, {})
    if total_row.get('A') != 'Total':
        raise turnout_data.TurnoutDataError(
            'ECQ attendance workbook has no statewide total row'
        )
    for column, observed_date in dated_columns:
        reported = _parse_count(
            total_row.get(column), 'state attendance {}'.format(observed_date)
        )
        if daily_totals[observed_date] != reported:
            raise turnout_data.TurnoutDataError(
                'state attendance {} totals {}, expected {}'.format(
                    observed_date, daily_totals[observed_date], reported
                )
            )
    reported_grand_total = _parse_count(
        total_row.get('M'), 'state attendance grand total'
    )
    if sum(daily_totals.values()) != reported_grand_total:
        raise turnout_data.TurnoutDataError(
            'state attendance totals {}, expected {}'.format(
                sum(daily_totals.values()), reported_grand_total
            )
        )
    return observations


def _parse_postal(election, data, dataset, source_id):
    rows = read_xlsx_rows(data, 'ECQ postal workbook')
    if rows.get(1, {}).get('A') != 'Postal votes issued':
        raise turnout_data.TurnoutDataError(
            'ECQ postal workbook has an unexpected title'
        )
    expected_header = {
        'A': 'State Electorate',
        'B': 'Postal ballots issued',
        'C': 'Postal ballots returned',
        'E': 'Postal votes accepted',
    }
    header = {key: value.strip() for key, value in rows.get(16, {}).items()}
    if any(header.get(column) != value for column, value in expected_header.items()):
        raise turnout_data.TurnoutDataError(
            'ECQ postal workbook has an unexpected table header'
        )
    observed_at = '2024-11-05'
    if rows.get(14, {}).get('A') != 'Current as at 5/11/2024':
        raise turnout_data.TurnoutDataError(
            'ECQ postal workbook has an unexpected observation date'
        )

    seats = _seat_index(dataset)
    represented = set()
    totals = [0, 0, 0]
    observations = []
    measures = (
        ('B', POSTAL_ISSUED_MEASURE, 'Postal ballots issued'),
        ('C', POSTAL_RETURNED_MEASURE, 'Postal ballots returned'),
        ('E', POSTAL_ACCEPTED_MEASURE, 'Postal votes accepted'),
    )
    for row_number in range(18, 18 + len(seats)):
        row = rows.get(row_number, {})
        seat_name = _resolve_seat(
            row.get('A'), seats, 'postal row {}'.format(row_number)
        )
        if seat_name in represented:
            raise turnout_data.TurnoutDataError(
                'ECQ postal workbook repeats {}'.format(seat_name)
            )
        represented.add(seat_name)
        counts = []
        for index, (column, measure, category) in enumerate(measures):
            count = _parse_count(
                row.get(column), '{} {}'.format(seat_name, category)
            )
            totals[index] += count
            counts.append(count)
            observations.append(turnout_data.OperationalObservation(
                election_code=election.election_code,
                source_id=source_id,
                measure=measure,
                observed_at=observed_at,
                count=count,
                geography_basis='elector_division',
                observation_status='final_reconciled',
                seat_name=seat_name,
                source_category=category,
            ))
        if not counts[2] <= counts[1] <= counts[0]:
            raise turnout_data.TurnoutDataError(
                '{} postal accepted/returned/issued counts are inconsistent'
                .format(seat_name)
            )
    _validate_coverage(represented, seats, 'ECQ postal workbook')

    total_row = rows.get(17, {})
    if total_row.get('A') != 'TOTAL':
        raise turnout_data.TurnoutDataError(
            'ECQ postal workbook has no statewide total row'
        )
    reported = [
        _parse_count(total_row.get(column), 'state {}'.format(category))
        for column, _measure, category in measures
    ]
    if any(district_total > state_total for district_total, state_total in zip(
        totals, reported
    )):
        raise turnout_data.TurnoutDataError(
            'district postal totals {} exceed published state totals {}'.format(
                totals, reported
            )
        )
    for (_column, measure, category), count in zip(measures, reported):
        observations.append(turnout_data.OperationalObservation(
            election_code=election.election_code,
            source_id=source_id,
            measure=measure,
            observed_at=observed_at,
            count=count,
            geography_basis='state',
            observation_status='final_reconciled',
            source_category='{} - statewide published total'.format(category),
        ))
    return observations


def build_observations(election, downloaded_files, dataset):
    """Parse both workbooks into validated sources and observations."""
    attendance_source_id = 'ecq-2024-attendance-operational'
    postal_source_id = 'ecq-2024-postal-operational'
    sources = [
        turnout_data.SourceDefinition(
            source_id=attendance_source_id,
            election_code=election.election_code,
            authority='Electoral Commission of Queensland',
            locator=election.attendance_url,
            adapter=ADAPTER_ID,
            status='operational',
            category_regime='ecq-attendance-administering-district-v1',
            notes=(
                'Final-reconciled daily mark-offs in the district administering '
                'the polling place, including absent voters enrolled elsewhere.'
            ),
        ),
        turnout_data.SourceDefinition(
            source_id=postal_source_id,
            election_code=election.election_code,
            authority='Electoral Commission of Queensland',
            locator=election.postal_url,
            adapter=ADAPTER_ID,
            status='operational',
            category_regime='ecq-postal-elector-district-v1',
            notes=(
                'Final-reconciled postal ballots issued, returned and accepted; '
                'issued ballots may include duplicates according to ECQ.'
            ),
        ),
    ]
    observations = _parse_attendance(
        election, downloaded_files['attendance'], dataset, attendance_source_id
    )
    observations.extend(_parse_postal(
        election, downloaded_files['postal'], dataset, postal_source_id
    ))
    return sources, observations


def merge_dataset(dataset, election, downloaded_files):
    """Replace prior ECQ operational records while retaining final evidence."""
    if len(dataset.elections) != 1:
        raise turnout_data.TurnoutDataError(
            '{} must contain exactly one election'.format(election.election_code)
        )
    definition = dataset.elections[0]
    if (
        definition.election_code != election.election_code
        or definition.election_date != election.election_date
    ):
        raise turnout_data.TurnoutDataError(
            'turnout file does not identify {}'.format(election.election_code)
        )

    replaced_source_ids = {
        source.source_id for source in dataset.sources
        if source.adapter == ADAPTER_ID
    }
    dataset.sources = [
        source for source in dataset.sources
        if source.source_id not in replaced_source_ids
    ]
    dataset.operational_observations = [
        observation for observation in dataset.operational_observations
        if observation.source_id not in replaced_source_ids
    ]
    sources, observations = build_observations(
        election, downloaded_files, dataset
    )
    dataset.sources.extend(sources)
    dataset.operational_observations.extend(observations)
    dataset.operational_observations.sort(key=lambda record: (
        record.observed_at,
        record.measure,
        record.seat_name,
        record.source_id,
    ))
    dataset.validate()
    return dataset


def coverage_summary(dataset):
    summary = {}
    measures = sorted({
        observation.measure
        for observation in dataset.operational_observations
        if observation.source_id.startswith('ecq-2024-')
    })
    for measure in measures:
        records = [
            observation for observation in dataset.operational_observations
            if observation.measure == measure
            and observation.source_id.startswith('ecq-2024-')
        ]
        latest_date = max(record.observed_at for record in records)
        latest_records = [
            record for record in records if record.observed_at == latest_date
        ]
        state_records = [
            record for record in latest_records
            if record.geography_basis == 'state'
        ]
        latest_count = (
            state_records[0].count
            if len(state_records) == 1
            else sum(record.count for record in latest_records)
        )
        summary[measure] = {
            'dates': len({record.observed_at for record in records}),
            'records': len(records),
            'latest_date': latest_date,
            'latest_count': latest_count,
        }
    return summary


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize operational ECQ turnout evidence.'
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
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Download and validate without replacing normalized files.',
    )
    return parser.parse_args(args)


def main(args=None):
    options = parse_args(args)
    selected = list(ELECTIONS) if 'all' in options.election else options.election
    selected = list(dict.fromkeys(selected))
    for election_code in selected:
        election = ELECTIONS[election_code]
        output_path = options.output_directory / '{}.json'.format(election_code)
        print('Downloading operational ECQ data for {}...'.format(election_code))
        downloaded_files = download_election_files(election)
        dataset = turnout_data.load_dataset(output_path)
        dataset = merge_dataset(dataset, election, downloaded_files)
        if not options.dry_run:
            turnout_data.write_dataset_atomically(output_path, dataset)
        print('{}:'.format(election_code))
        for measure, summary in coverage_summary(dataset).items():
            print(
                '  {}: {} dates, {} observations; '
                'latest {} = {:,}'.format(
                    measure,
                    summary['dates'],
                    summary['records'],
                    summary['latest_date'],
                    summary['latest_count'],
                )
            )
        print('  {}'.format(
            'Validated without writing' if options.dry_run
            else 'Updated {}'.format(output_path)
        ))
    return 0


if __name__ == '__main__':
    main()

"""Download and normalize operational AEC early-voting evidence.

This adapter records published counts without estimating turnout. Pre-poll
figures describe votes issued by centres administered under a division, while
postal figures describe electors enrolled in a division; consumers must not
treat those two geographies as interchangeable.

Main functions:
* ``download_election_files`` downloads the configured official AEC CSVs.
* ``build_observations`` parses and validates cumulative operational series.
* ``merge_dataset`` replaces this adapter's prior records in a final dataset.
* ``main`` provides election selection, reporting and atomic publication.
"""

import argparse
import csv
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from datetime import date, datetime, timedelta
import io
from pathlib import Path
from urllib.request import Request, urlopen

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 30
DOWNLOAD_WORKERS = 8
ADAPTER_ID = 'aec-pre-election-operational-v1'
STATE_CODES = frozenset({'ACT', 'NSW', 'NT', 'QLD', 'SA', 'TAS', 'VIC', 'WA'})

PREPOLL_MEASURE = 'prepoll_votes_issued_cumulative'
POSTAL_APPLICATION_MEASURE = 'postal_applications_cumulative'
POSTAL_RETURN_MEASURE = 'postal_votes_returned_cumulative'


@dataclass(frozen=True)
class AecOperationalElection:
    election_code: str
    election_date: str
    prepoll_url: str
    prepoll_layout: str
    postal_url: str
    postal_layout: str
    postal_snapshot_date: str = ''
    postal_snapshot_start: str = ''
    postal_snapshot_end: str = ''


ELECTIONS = {
    '2010fed': AecOperationalElection(
        '2010fed',
        '2010-08-21',
        'https://www.aec.gov.au/Elections/federal_elections/2010/'
        'files/e2010-prepoll-stats-19-08.csv',
        'wide',
        'https://www.aec.gov.au/Elections/federal_elections/2010/'
        'files/e2010-pva-stats-19-08.csv',
        'wide-legacy',
    ),
    '2013fed': AecOperationalElection(
        '2013fed',
        '2013-09-07',
        'https://www.aec.gov.au/Elections/federal_elections/2013/'
        'files/statistics/e2013-prepoll-stats-07-09.csv',
        'wide',
        'https://www.aec.gov.au/Elections/federal_elections/2013/'
        'files/statistics/e2013-pva-stats-07-09.csv',
        'wide-legacy',
    ),
    '2016fed': AecOperationalElection(
        '2016fed',
        '2016-07-02',
        'https://www.aec.gov.au/Elections/federal_elections/2016/'
        'files/20160702_WEB_Pre-Poll_Report.csv',
        'wide',
        'https://www.aec.gov.au/Elections/federal_elections/2016/'
        'files/20160702_WEB_Postal_Report.csv',
        'wide-modern',
    ),
    '2019fed': AecOperationalElection(
        '2019fed',
        '2019-05-18',
        'https://www.aec.gov.au/Elections/federal_elections/2019/'
        'files/downloads/20190518_WEB_Pre-poll_Report_FE2019.csv',
        'wide',
        'https://www.aec.gov.au/Elections/federal_elections/2019/'
        'files/downloads/20190518_WEB_Postal_Report_FE2019.csv',
        'wide-modern',
    ),
    '2022fed': AecOperationalElection(
        '2022fed',
        '2022-05-21',
        'https://www.aec.gov.au/Elections/federal_elections/2022/'
        'files/downloads/20220620_WEB_FINAL_Pre-poll_report_FE2022.csv',
        'long',
        'https://www.aec.gov.au/Elections/federal_elections/2022/'
        'files/downloads/Postal-votes-220603.csv',
        'snapshot-final',
        postal_snapshot_date='2022-06-03',
    ),
    '2025fed': AecOperationalElection(
        '2025fed',
        '2025-05-03',
        'https://www.aec.gov.au/election/fe25/files/downloads/pre-poll/'
        'pre-pollvotesissued_final_FE25.csv',
        'long',
        'https://www.aec.gov.au/election/fe25/files/downloads/postal/'
        'Postal-votes-{date}.csv',
        'snapshot-series',
        postal_snapshot_start='2025-04-12',
        postal_snapshot_end='2025-05-21',
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
    if data.lstrip().lower().startswith(b'<!doctype html'):
        raise turnout_data.TurnoutDataError(
            'AEC returned HTML instead of CSV for {}'.format(url)
        )
    return data


def _snapshot_dates(election):
    start = date.fromisoformat(election.postal_snapshot_start)
    end = date.fromisoformat(election.postal_snapshot_end)
    return tuple(
        start + timedelta(days=offset)
        for offset in range((end - start).days + 1)
    )


def _download_specs(election):
    specs = [('prepoll', election.prepoll_url)]
    if election.postal_layout == 'snapshot-series':
        for snapshot_date in _snapshot_dates(election):
            tag = snapshot_date.strftime('%y%m%d')
            specs.append((
                'postal:{}'.format(snapshot_date.isoformat()),
                election.postal_url.format(date=tag),
            ))
    elif election.postal_url:
        key = 'postal'
        if election.postal_layout == 'snapshot-final':
            if not election.postal_snapshot_date:
                raise turnout_data.TurnoutDataError(
                    '{} has no final postal snapshot date'.format(
                        election.election_code
                    )
                )
            key = 'postal:{}'.format(election.postal_snapshot_date)
        specs.append((key, election.postal_url))
    return specs


def download_election_files(election):
    """Download all official files configured for one election."""
    specs = _download_specs(election)
    with ThreadPoolExecutor(max_workers=DOWNLOAD_WORKERS) as executor:
        downloaded = executor.map(lambda spec: _download(spec[1]), specs)
        return {
            key: data for (key, _url), data in zip(specs, downloaded)
        }


def _csv_rows(data, label):
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} is not valid UTF-8: {}'.format(label, error)
        )
    if text.lstrip().lower().startswith('<!doctype html'):
        raise turnout_data.TurnoutDataError(
            '{} contains HTML instead of CSV'.format(label)
        )
    reader = csv.DictReader(io.StringIO(text))
    if not reader.fieldnames:
        raise turnout_data.TurnoutDataError('{} has no CSV header'.format(label))
    rows = list(reader)
    if not rows:
        raise turnout_data.TurnoutDataError('{} has no CSV rows'.format(label))
    return tuple(reader.fieldnames), rows


def _parse_count(value, label, blank_is_zero=False):
    text = '' if value is None else value.strip().replace(',', '')
    if blank_is_zero and not text:
        return 0
    try:
        count = int(text)
    except ValueError:
        raise turnout_data.TurnoutDataError(
            '{} has invalid count {!r}'.format(label, value)
        )
    if count < 0:
        raise turnout_data.TurnoutDataError(
            '{} has negative count'.format(label)
        )
    return count


def _parse_date(value, label):
    formats = (
        '%Y-%m-%d',
        '%Y%m%d',
        '%d/%m/%Y',
        '%d/%m/%y',
        '%d %b %y',
        '%d-%b-%y',
    )
    for date_format in formats:
        try:
            return datetime.strptime(value.strip(), date_format).date()
        except ValueError:
            pass
    raise turnout_data.TurnoutDataError(
        '{} has invalid date {!r}'.format(label, value)
    )


def _date_headers(headers):
    dates = []
    for header in headers:
        try:
            parsed = _parse_date(header, 'CSV header')
        except turnout_data.TurnoutDataError:
            continue
        dates.append((header, parsed))
    if not dates:
        raise turnout_data.TurnoutDataError('CSV has no dated count columns')
    if len({parsed for _header, parsed in dates}) != len(dates):
        raise turnout_data.TurnoutDataError('CSV repeats a dated count column')
    return sorted(dates, key=lambda item: item[1])


def _seat_index(dataset):
    indexed = {}
    for seat in dataset.seat_totals:
        key = (seat.subdivision.upper(), seat.seat_name.casefold())
        if key in indexed:
            raise turnout_data.TurnoutDataError(
                'duplicate final division identity {}'.format(key)
            )
        indexed[key] = seat.seat_name
    return indexed


def _resolve_division(state, division, seats, label):
    state = state.strip().upper()
    division = division.strip()
    if state not in STATE_CODES or not division:
        raise turnout_data.TurnoutDataError(
            '{} has incomplete division identity'.format(label)
        )
    key = (state, division.casefold())
    try:
        return seats[key]
    except KeyError:
        raise turnout_data.TurnoutDataError(
            '{} references unknown {} division {!r}'.format(label, state, division)
        )


def _field(row, alternatives, label):
    for name in alternatives:
        if name in row:
            return row[name]
    raise turnout_data.TurnoutDataError(
        '{} is missing one of: {}'.format(label, ', '.join(alternatives))
    )


def _cumulative_observations(
    election,
    source_id,
    daily_counts,
    dates,
    measure,
    geography_basis,
    observation_status,
    source_category,
):
    observations = []
    for seat_name in sorted(daily_counts):
        cumulative = 0
        for observed_date in dates:
            cumulative += daily_counts[seat_name].get(observed_date, 0)
            observations.append(turnout_data.OperationalObservation(
                election_code=election.election_code,
                source_id=source_id,
                measure=measure,
                observed_at=observed_date.isoformat(),
                count=cumulative,
                geography_basis=geography_basis,
                observation_status=observation_status,
                seat_name=seat_name,
                source_category=source_category,
            ))
    return observations


def _parse_wide_prepoll(election, data, seats, source_id):
    headers, rows = _csv_rows(data, 'pre-poll file')
    date_columns = _date_headers(headers)
    daily_counts = defaultdict(lambda: defaultdict(int))
    represented_seats = set()

    for row_number, row in enumerate(rows, start=2):
        state = _field(row, ('State', 'm_state_ab'), 'pre-poll header').strip()
        division = _field(row, ('Division', 'm_div_nm'), 'pre-poll header').strip()
        if state.upper() not in STATE_CODES and not division:
            continue
        seat_name = _resolve_division(
            state, division, seats, 'pre-poll row {}'.format(row_number)
        )
        represented_seats.add(seat_name)
        for header, observed_date in date_columns:
            daily_counts[seat_name][observed_date] += _parse_count(
                row[header],
                'pre-poll row {} {}'.format(row_number, header),
                blank_is_zero=True,
            )

    _validate_division_coverage(represented_seats, seats, 'pre-poll file')
    return _cumulative_observations(
        election,
        source_id,
        daily_counts,
        [item[1] for item in date_columns],
        PREPOLL_MEASURE,
        'administering_division',
        'final_reconciled',
        'Pre-poll votes issued at PPVCs and divisional offices',
    )


def _parse_long_prepoll(election, data, seats, source_id):
    _headers, rows = _csv_rows(data, 'pre-poll file')
    daily_counts = defaultdict(lambda: defaultdict(int))
    represented_seats = set()
    dates = set()

    for row_number, row in enumerate(rows, start=2):
        seat_name = _resolve_division(
            _field(row, ('State',), 'pre-poll header'),
            _field(row, ('Division',), 'pre-poll header'),
            seats,
            'pre-poll row {}'.format(row_number),
        )
        observed_date = _parse_date(
            _field(row, ('Issue Date',), 'pre-poll header'),
            'pre-poll row {} date'.format(row_number),
        )
        count = _parse_count(
            _field(row, ('Total Votes',), 'pre-poll header'),
            'pre-poll row {} Total Votes'.format(row_number),
        )
        represented_seats.add(seat_name)
        dates.add(observed_date)
        daily_counts[seat_name][observed_date] += count

    _validate_division_coverage(represented_seats, seats, 'pre-poll file')
    return _cumulative_observations(
        election,
        source_id,
        daily_counts,
        sorted(dates),
        PREPOLL_MEASURE,
        'administering_division',
        'final_reconciled',
        'Pre-poll votes issued at PPVCs',
    )


def _validate_division_coverage(represented_seats, seats, label):
    expected = set(seats.values())
    if represented_seats != expected:
        raise turnout_data.TurnoutDataError(
            '{} division coverage differs; missing={}, extra={}'.format(
                label,
                sorted(expected - represented_seats),
                sorted(represented_seats - expected),
            )
        )


def _wide_postal_total(row, headers, election, label):
    if election.postal_layout == 'wide-legacy':
        total_header = next(
            (
                header for header in (
                    'TOTAL to date (Inc GPV)', 'TOTAL to date'
                )
                if header in headers
            ),
            None,
        )
        if total_header is None:
            raise turnout_data.TurnoutDataError(
                '{} has no postal application total'.format(label)
            )
        return _parse_count(row[total_header], label)

    divider_names = ('PVA_Web_2_Date_Div', 'PVA_Web_2_Date_V2_Div')
    divider = next((name for name in divider_names if name in headers), None)
    if divider is None:
        raise turnout_data.TurnoutDataError(
            '{} has no postal source/date divider'.format(label)
        )
    divider_index = headers.index(divider)
    return sum(
        _parse_count(row[header], '{} {}'.format(label, header), blank_is_zero=True)
        for header in headers[2:divider_index]
    )


def _parse_wide_postal(election, data, seats, source_id):
    headers, rows = _csv_rows(data, 'postal file')
    date_columns = _date_headers(headers)
    daily_counts = defaultdict(lambda: defaultdict(int))
    represented_seats = set()

    for row_number, row in enumerate(rows, start=2):
        state = _field(row, ('State', 'State_Cd'), 'postal header').strip()
        division = _field(
            row,
            ('Enrolment', 'Enrolment Division', 'PVA_Web_1_Party_Div'),
            'postal header',
        ).strip()
        if state.upper() not in STATE_CODES:
            continue
        label = 'postal row {}'.format(row_number)
        seat_name = _resolve_division(state, division, seats, label)
        represented_seats.add(seat_name)
        daily_total = 0
        for header, observed_date in date_columns:
            count = _parse_count(
                row[header],
                '{} {}'.format(label, header),
                blank_is_zero=True,
            )
            daily_counts[seat_name][observed_date] += count
            daily_total += count
        reported_total = _wide_postal_total(row, headers, election, label)
        undated_total = sum(
            _parse_count(
                row.get(header, ''),
                '{} {}'.format(label, header),
                blank_is_zero=True,
            )
            for header in ('<>', 'Date out of range')
        )
        if daily_total + undated_total != reported_total:
            raise turnout_data.TurnoutDataError(
                '{} dated and out-of-range applications total {}, expected {}'.format(
                    label, daily_total + undated_total, reported_total
                )
            )

    _validate_division_coverage(represented_seats, seats, 'postal file')
    return _cumulative_observations(
        election,
        source_id,
        daily_counts,
        [item[1] for item in date_columns],
        POSTAL_APPLICATION_MEASURE,
        'elector_division',
        'final_reconciled',
        'Postal vote applications processed',
    )


def _parse_postal_snapshots(election, files, seats, source_id):
    observations = []
    previous_dates = set()
    for key, data in sorted(files.items()):
        try:
            observed_date = date.fromisoformat(key.split(':', 1)[1])
        except (IndexError, ValueError):
            raise turnout_data.TurnoutDataError(
                'postal snapshot has invalid key {!r}'.format(key)
            )
        if observed_date in previous_dates:
            raise turnout_data.TurnoutDataError(
                'postal snapshots repeat {}'.format(observed_date)
            )
        previous_dates.add(observed_date)
        _headers, rows = _csv_rows(data, '{} postal file'.format(observed_date))
        represented_seats = set()
        for row_number, row in enumerate(rows, start=2):
            state = _field(row, ('State',), 'postal header').strip()
            division = _field(row, ('Division',), 'postal header').strip()
            if state.upper() not in STATE_CODES and not division:
                continue
            seat_name = _resolve_division(
                state,
                division,
                seats,
                '{} postal row {}'.format(observed_date, row_number),
            )
            represented_seats.add(seat_name)
            applications = _parse_count(
                _field(
                    row,
                    ('Valid Applications Received', 'Valid Apps Received'),
                    'postal header',
                ),
                '{} {} applications'.format(observed_date, seat_name),
            )
            returned = _parse_count(
                _field(row, ('Postal Votes Returned',), 'postal header'),
                '{} {} returns'.format(observed_date, seat_name),
            )
            status = (
                'final_reconciled'
                if election.postal_layout == 'snapshot-final'
                else 'contemporaneous'
            )
            for measure, count, category in (
                (
                    POSTAL_APPLICATION_MEASURE,
                    applications,
                    'Valid postal vote applications received',
                ),
                (
                    POSTAL_RETURN_MEASURE,
                    returned,
                    'Postal votes returned',
                ),
            ):
                observations.append(turnout_data.OperationalObservation(
                    election_code=election.election_code,
                    source_id=source_id,
                    measure=measure,
                    observed_at=observed_date.isoformat(),
                    count=count,
                    geography_basis='elector_division',
                    observation_status=status,
                    seat_name=seat_name,
                    source_category=category,
                ))
        _validate_division_coverage(
            represented_seats, seats, '{} postal file'.format(observed_date)
        )
    return observations


def build_observations(election, downloaded_files, dataset):
    """Parse downloaded files and return validated sources and observations."""
    seats = _seat_index(dataset)
    prepoll_source_id = 'aec-{}-prepoll-operational'.format(
        election.election_code[:4]
    )
    postal_source_id = 'aec-{}-postal-operational'.format(
        election.election_code[:4]
    )
    sources = [turnout_data.SourceDefinition(
        source_id=prepoll_source_id,
        election_code=election.election_code,
        authority='Australian Electoral Commission',
        locator=election.prepoll_url,
        adapter=ADAPTER_ID,
        status='operational',
        category_regime='aec-prepoll-administering-division-v1',
        notes=(
            'Final-reconciled daily votes issued by PPVCs administered under '
            'the named division; this is not the elector home division.'
        ),
    )]
    if election.prepoll_layout == 'wide':
        observations = _parse_wide_prepoll(
            election, downloaded_files['prepoll'], seats, prepoll_source_id
        )
    elif election.prepoll_layout == 'long':
        observations = _parse_long_prepoll(
            election, downloaded_files['prepoll'], seats, prepoll_source_id
        )
    else:
        raise turnout_data.TurnoutDataError(
            'unsupported pre-poll layout {}'.format(election.prepoll_layout)
        )

    sources.append(turnout_data.SourceDefinition(
        source_id=postal_source_id,
        election_code=election.election_code,
        authority='Australian Electoral Commission',
        locator=election.postal_url,
        adapter=ADAPTER_ID,
        status='operational',
        category_regime='aec-postal-elector-division-v1',
        notes=(
            'Cumulative valid applications and, where published, returned '
            'postal votes for electors enrolled in the named division.'
        ),
    ))
    if election.postal_layout.startswith('wide-'):
        observations.extend(_parse_wide_postal(
            election, downloaded_files['postal'], seats, postal_source_id
        ))
    elif election.postal_layout.startswith('snapshot-'):
        postal_files = {
            key: data for key, data in downloaded_files.items()
            if key.startswith('postal:')
        }
        observations.extend(_parse_postal_snapshots(
            election, postal_files, seats, postal_source_id
        ))
    else:
        raise turnout_data.TurnoutDataError(
            'unsupported postal layout {}'.format(election.postal_layout)
        )
    return sources, observations


def merge_dataset(dataset, election, downloaded_files):
    """Replace prior AEC operational records while retaining final evidence."""
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
    dates = defaultdict(set)
    records = defaultdict(int)
    for observation in dataset.operational_observations:
        dates[observation.measure].add(observation.observed_at)
        records[observation.measure] += 1
    summary = {}
    for measure in sorted(records):
        latest_date = max(dates[measure])
        latest_count = sum(
            observation.count
            for observation in dataset.operational_observations
            if observation.measure == measure
            and observation.observed_at == latest_date
        )
        summary[measure] = {
            'dates': len(dates[measure]),
            'records': records[measure],
            'latest_date': latest_date,
            'latest_count': latest_count,
        }
    return summary


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize operational AEC turnout evidence.'
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
        print('Downloading operational AEC data for {}...'.format(election_code))
        downloaded_files = download_election_files(election)
        dataset = turnout_data.load_dataset(output_path)
        dataset = merge_dataset(dataset, election, downloaded_files)
        if not options.dry_run:
            turnout_data.write_dataset_atomically(output_path, dataset)
        print('{}:'.format(election_code))
        for measure, summary in coverage_summary(dataset).items():
            print(
                '  {}: {} dates, {} division observations; '
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

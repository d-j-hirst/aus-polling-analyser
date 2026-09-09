"""Normalize the final 2015 NSW pre-election pre-poll mark-off count.

The official source contains one depersonalized transaction per elector and
covers 16-27 March 2015, ending the day before polling. This adapter streams
that large CSV and retains only exact electorate and state cumulative totals.

Main functions:
* ``read_transactions`` validates and aggregates the official CSV rows.
* ``build_observations`` converts aggregates to normalized operational records.
* ``merge_dataset`` replaces only records previously written by this adapter.
* ``main`` downloads, validates and atomically publishes the selected dataset.
"""

import argparse
from collections import Counter
import csv
from dataclasses import dataclass
from datetime import date, datetime
import io
from pathlib import Path
from urllib.request import Request, urlopen

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 120
ADAPTER_ID = 'nswec-pre-election-operational-v1'
PREPOLL_MEASURE = 'prepoll_votes_cast_cumulative'


@dataclass(frozen=True)
class NswOperationalElection:
    election_code: str
    election_date: str
    first_markoff_date: str
    final_markoff_date: str
    expected_transactions: int
    transaction_url: str


ELECTIONS = {
    '2015nsw': NswOperationalElection(
        election_code='2015nsw',
        election_date='2015-03-28',
        first_markoff_date='2015-03-16',
        final_markoff_date='2015-03-27',
        expected_transactions=642408,
        transaction_url=(
            'https://data.nsw.gov.au/data/dataset/'
            'f6801505-459a-490e-aee7-7ed563796fb5/resource/'
            '5202e409-242b-4854-bdf1-363281bfc568/download/'
            'sge2015-pre-poll-markoff-v3-depersonalised.csv'
        ),
    ),
}

REQUIRED_COLUMNS = frozenset({'ENROLLED District', 'Mark-off Date'})


def _seat_index(dataset):
    seats = {}
    for seat in dataset.seat_totals:
        key = seat.seat_name.casefold()
        if key in seats:
            raise turnout_data.TurnoutDataError(
                'duplicate final district identity {!r}'.format(seat.seat_name)
            )
        seats[key] = seat.seat_name
    return seats


def _parse_markoff_date(value, label):
    try:
        return datetime.strptime(str(value).strip(), '%Y%m%d').date()
    except ValueError:
        raise turnout_data.TurnoutDataError(
            '{} has invalid mark-off date {!r}'.format(label, value)
        )


def read_transactions(rows, election, dataset):
    """Validate transaction rows and return exact counts by enrolled district."""
    if rows.fieldnames is None:
        raise turnout_data.TurnoutDataError('NSW pre-poll CSV has no header')
    missing_columns = REQUIRED_COLUMNS - set(rows.fieldnames)
    if missing_columns:
        raise turnout_data.TurnoutDataError(
            'NSW pre-poll CSV omits required columns: {}'.format(
                ', '.join(sorted(missing_columns))
            )
        )

    seats = _seat_index(dataset)
    first_date = date.fromisoformat(election.first_markoff_date)
    final_date = date.fromisoformat(election.final_markoff_date)
    counts = Counter()
    transaction_count = 0
    for row_number, row in enumerate(rows, start=2):
        district_value = (row.get('ENROLLED District') or '').strip()
        try:
            district = seats[district_value.casefold()]
        except KeyError:
            raise turnout_data.TurnoutDataError(
                'NSW pre-poll row {} references unknown district {!r}'.format(
                    row_number, district_value
                )
            )
        markoff_date = _parse_markoff_date(
            row.get('Mark-off Date'), 'NSW pre-poll row {}'.format(row_number)
        )
        if not first_date <= markoff_date <= final_date:
            raise turnout_data.TurnoutDataError(
                'NSW pre-poll row {} has out-of-period mark-off date {}'.format(
                    row_number, markoff_date.isoformat()
                )
            )
        counts[district] += 1
        transaction_count += 1

    missing_districts = set(seats.values()) - set(counts)
    if missing_districts:
        raise turnout_data.TurnoutDataError(
            'NSW pre-poll transactions omit districts: {}'.format(
                ', '.join(sorted(missing_districts))
            )
        )
    if transaction_count != election.expected_transactions:
        raise turnout_data.TurnoutDataError(
            'NSW pre-poll transaction count is {:,}, expected {:,}'.format(
                transaction_count, election.expected_transactions
            )
        )
    return counts


def build_observations(election, counts):
    source_id = 'nswec-{}-prepoll-transactions'.format(election.election_code)
    source = turnout_data.SourceDefinition(
        source_id=source_id,
        election_code=election.election_code,
        authority='NSW Electoral Commission',
        locator=election.transaction_url,
        adapter=ADAPTER_ID,
        status='operational',
        category_regime='nswec-2015-prepoll-markoffs-v1',
        notes=(
            'Exact final pre-election pre-poll mark-offs, grouped by the '
            'district in which each elector was enrolled.'
        ),
    )
    observed_at = election.final_markoff_date
    observations = [
        turnout_data.OperationalObservation(
            election_code=election.election_code,
            source_id=source_id,
            measure=PREPOLL_MEASURE,
            observed_at=observed_at,
            count=count,
            geography_basis='elector_division',
            observation_status='contemporaneous',
            seat_name=district,
            source_category='Pre-poll mark-off transaction',
        )
        for district, count in sorted(counts.items())
    ]
    observations.append(turnout_data.OperationalObservation(
        election_code=election.election_code,
        source_id=source_id,
        measure=PREPOLL_MEASURE,
        observed_at=observed_at,
        count=sum(counts.values()),
        geography_basis='state',
        observation_status='contemporaneous',
        source_category='Sum of enrolled-district pre-poll mark-offs',
        derivation='sum_published_counts',
    ))
    return source, observations


def merge_dataset(dataset, election, counts):
    """Replace prior NSW operational records while retaining final evidence."""
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
    source, observations = build_observations(election, counts)
    dataset.sources.append(source)
    dataset.operational_observations.extend(observations)
    dataset.operational_observations.sort(key=lambda record: (
        record.observed_at,
        record.measure,
        record.seat_name,
        record.source_id,
    ))
    dataset.validate()
    return dataset


def download_counts(election, dataset):
    request = Request(
        election.transaction_url,
        headers={
            'User-Agent': (
                'AEF turnout research '
                '(https://www.aeforecasts.com/; aeforecasts@gmail.com)'
            )
        },
    )
    with urlopen(request, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response:
        stream = io.TextIOWrapper(response, encoding='utf-8-sig', newline='')
        return read_transactions(csv.DictReader(stream), election, dataset)


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize operational NSW turnout evidence.'
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
        print('Downloading operational NSW data for {}...'.format(election_code))
        dataset = turnout_data.load_dataset(output_path)
        counts = download_counts(election, dataset)
        dataset = merge_dataset(dataset, election, counts)
        if not options.dry_run:
            turnout_data.write_dataset_atomically(output_path, dataset)
        print(
            '  {:,} exact pre-poll marks across {} districts'.format(
                sum(counts.values()), len(counts)
            )
        )
        print('  {}'.format(
            'Validated without writing' if options.dry_run
            else 'Updated {}'.format(output_path)
        ))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

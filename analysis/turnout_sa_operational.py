"""Normalize final pre-election operational turnout evidence from ECSA.

This adapter reads ECSA's 2026 daily-tally page. It records only the final
pre-election early-voting mark-offs and postal applications; neither measure
is treated as a final accepted ballot count.

Main functions:
* ``parse_tally_page`` extracts and reconciles the two relevant HTML tables.
* ``build_observations`` creates state and district operational records.
* ``merge_dataset`` replaces only records previously written by this adapter.
* ``main`` downloads, validates and atomically publishes the normalized file.
"""

import argparse
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path
import re
from urllib.request import Request, urlopen

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 30
ADAPTER_ID = 'ecsa-pre-election-operational-v1'

PREPOLL_MEASURE = 'prepoll_votes_cast_cumulative'
POSTAL_APPLICATION_MEASURE = 'postal_applications_cumulative'

EARLY_HEADING = 'Daily Tally | Early Voting Mark Off Numbers'
POSTAL_HEADING = 'Daily Tally | Postal Vote Applications by District'


@dataclass(frozen=True)
class SaOperationalElection:
    election_code: str
    election_date: str
    tally_url: str
    early_observed_at: str
    postal_observed_at: str
    expected_districts: int
    expected_early_total: int
    expected_postal_total: int
    expected_early_district_residual: int
    expected_postal_district_residual: int


ELECTIONS = {
    '2026sa': SaOperationalElection(
        election_code='2026sa',
        election_date='2026-03-21',
        tally_url='https://www.ecsa.sa.gov.au/se2026-daily-tally',
        early_observed_at='2026-03-20',
        postal_observed_at='2026-03-16',
        expected_districts=47,
        expected_early_total=454862,
        expected_postal_total=174121,
        expected_early_district_residual=2,
        expected_postal_district_residual=0,
    ),
}


class _TallyTableParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.tables = []
        self._heading = ''
        self._heading_parts = None
        self._table_heading = None
        self._rows = None
        self._row = None
        self._cell = None

    def handle_starttag(self, tag, attrs):
        del attrs
        if tag == 'h4':
            self._heading_parts = []
        elif tag == 'table':
            self._table_heading = self._heading
            self._rows = []
        elif tag == 'tr' and self._rows is not None:
            self._row = []
        elif tag in {'td', 'th'} and self._row is not None:
            self._cell = []

    def handle_data(self, data):
        cleaned = ' '.join(data.split())
        if not cleaned:
            return
        if self._heading_parts is not None:
            self._heading_parts.append(cleaned)
        if self._cell is not None:
            self._cell.append(cleaned)

    def handle_endtag(self, tag):
        if tag == 'h4' and self._heading_parts is not None:
            self._heading = ' '.join(self._heading_parts)
            self._heading_parts = None
        elif tag in {'td', 'th'} and self._cell is not None:
            self._row.append(' '.join(self._cell))
            self._cell = None
        elif tag == 'tr' and self._row is not None:
            if self._row:
                self._rows.append(self._row)
            self._row = None
        elif tag == 'table' and self._rows is not None:
            self.tables.append((self._table_heading, self._rows))
            self._table_heading = None
            self._rows = None


def _decode_page(data):
    try:
        return data.decode('utf-8-sig')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            'ECSA daily tally is not valid UTF-8: {}'.format(error)
        )


def _table_for_heading(parser, heading):
    matches = [rows for table_heading, rows in parser.tables
               if table_heading == heading]
    if len(matches) != 1:
        raise turnout_data.TurnoutDataError(
            'ECSA daily tally has {} tables headed {!r}, expected 1'.format(
                len(matches), heading
            )
        )
    return matches[0]


def _count(value, label):
    cleaned = value.strip()
    if not re.fullmatch(r'(?:[0-9]+|[0-9]{1,3}(?:[,.][0-9]{3})+)', cleaned):
        raise turnout_data.TurnoutDataError(
            '{} has invalid count {!r}'.format(label, value)
        )
    return int(cleaned.replace(',', '').replace('.', ''))


def _parse_district_table(rows, heading):
    if not rows or len(rows[0]) < 3:
        raise turnout_data.TurnoutDataError(
            '{} has no usable header'.format(heading)
        )
    header = rows[0]
    if header[0] or header[-1].upper() != 'TOTAL':
        raise turnout_data.TurnoutDataError(
            '{} has an unexpected header'.format(heading)
        )

    district_totals = {}
    state_total = None
    for row_number, row in enumerate(rows[1:], start=2):
        if row and row[0].upper() == 'DISTRICT':
            continue
        if len(row) != len(header):
            raise turnout_data.TurnoutDataError(
                '{} row {} has {} cells, expected {}'.format(
                    heading, row_number, len(row), len(header)
                )
            )
        district = row[0].strip()
        counts = [
            _count(value, '{} row {}'.format(heading, row_number))
            for value in row[1:]
        ]
        # ECSA's final TOTAL can include corrections not allocated back to the
        # displayed daily cells. The final total is the operational control.
        if district:
            if district in district_totals:
                raise turnout_data.TurnoutDataError(
                    '{} repeats district {}'.format(heading, district)
                )
            district_totals[district] = counts[-1]
        elif state_total is None:
            state_total = counts[-1]
        else:
            raise turnout_data.TurnoutDataError(
                '{} has multiple state-total rows'.format(heading)
            )

    if state_total is None:
        raise turnout_data.TurnoutDataError(
            '{} has no state-total row'.format(heading)
        )
    return district_totals, state_total


def parse_tally_page(election, data):
    """Extract exact final early-vote and postal-application totals."""
    parser = _TallyTableParser()
    try:
        parser.feed(_decode_page(data))
        parser.close()
    except turnout_data.TurnoutDataError:
        raise
    except Exception as error:
        raise turnout_data.TurnoutDataError(
            'ECSA daily tally contains invalid HTML: {}'.format(error)
        )

    early = _parse_district_table(
        _table_for_heading(parser, EARLY_HEADING), EARLY_HEADING
    )
    postal = _parse_district_table(
        _table_for_heading(parser, POSTAL_HEADING), POSTAL_HEADING
    )
    early_districts, early_total = early
    postal_districts, postal_total = postal
    if set(early_districts) != set(postal_districts):
        raise turnout_data.TurnoutDataError(
            'ECSA early-vote and postal tables cover different districts'
        )
    if len(early_districts) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} tables cover {} districts, expected {}'.format(
                election.election_code,
                len(early_districts),
                election.expected_districts,
            )
        )
    for label, actual, expected in (
        ('early-vote', early_total, election.expected_early_total),
        ('postal-application', postal_total, election.expected_postal_total),
    ):
        if actual != expected:
            raise turnout_data.TurnoutDataError(
                '{} {} total is {}, expected {}'.format(
                    election.election_code, label, actual, expected
                )
            )
    for label, district_totals, state_total, expected_residual in (
        (
            'early-vote',
            early_districts,
            early_total,
            election.expected_early_district_residual,
        ),
        (
            'postal-application',
            postal_districts,
            postal_total,
            election.expected_postal_district_residual,
        ),
    ):
        residual = state_total - sum(district_totals.values())
        if residual != expected_residual:
            raise turnout_data.TurnoutDataError(
                '{} {} district residual is {}, expected {}'.format(
                    election.election_code,
                    label,
                    residual,
                    expected_residual,
                )
            )
    return early_districts, early_total, postal_districts, postal_total


def build_observations(election, data):
    """Build one validated official source and its operational records."""
    early_districts, early_total, postal_districts, postal_total = (
        parse_tally_page(election, data)
    )
    source_id = 'ecsa-{}-daily-tally'.format(election.election_code)
    source = turnout_data.SourceDefinition(
        source_id=source_id,
        election_code=election.election_code,
        authority='Electoral Commission of South Australia',
        locator=election.tally_url,
        adapter=ADAPTER_ID,
        status='operational',
        category_regime='ecsa-daily-tally-v1',
        notes=(
            'Final pre-election ECSA district and state totals. The official '
            'TOTAL columns are authoritative because some daily cells do not '
            'sum to the corrected final district total. The 47 early-vote '
            'district totals are two votes below the published state total. '
            "Lee's postal total uses a period as its thousands separator. "
            'The official '
            'early-vote total is 454,862; Antony Green reported 466,364 and '
            'noted an approximately 11,000-vote discrepancy with ECSA.'
        ),
    )
    observations = []
    for measure, observed_at, category, districts, state_total in (
        (
            PREPOLL_MEASURE,
            election.early_observed_at,
            'Final Early Voting Mark Off Numbers total',
            early_districts,
            early_total,
        ),
        (
            POSTAL_APPLICATION_MEASURE,
            election.postal_observed_at,
            'Final Postal Vote Applications total',
            postal_districts,
            postal_total,
        ),
    ):
        observations.extend(
            turnout_data.OperationalObservation(
                election_code=election.election_code,
                source_id=source_id,
                measure=measure,
                observed_at=observed_at,
                count=count,
                geography_basis='elector_division',
                observation_status='contemporaneous',
                seat_name=district,
                source_category=category,
            )
            for district, count in sorted(districts.items())
        )
        observations.append(turnout_data.OperationalObservation(
            election_code=election.election_code,
            source_id=source_id,
            measure=measure,
            observed_at=observed_at,
            count=state_total,
            geography_basis='state',
            observation_status='contemporaneous',
            source_category=category,
        ))
    return source, observations


def _new_dataset(election):
    return turnout_data.TurnoutDataset(elections=[
        turnout_data.ElectionDefinition(
            election.election_code,
            election.election_date,
            election.election_code[4:],
        )
    ])


def merge_dataset(dataset, election, data):
    """Replace prior ECSA daily-tally records, retaining other evidence."""
    if len(dataset.elections) != 1 or (
        dataset.elections[0].election_code != election.election_code
        or dataset.elections[0].election_date != election.election_date
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
    source, observations = build_observations(election, data)
    dataset.sources.append(source)
    dataset.operational_observations.extend(observations)
    dataset.operational_observations.sort(key=lambda record: (
        record.observed_at,
        record.measure,
        record.seat_name,
    ))
    dataset.validate()
    return dataset


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


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Normalize ECSA final pre-election turnout evidence.'
    )
    parser.add_argument(
        '--election',
        action='append',
        choices=tuple(ELECTIONS) + ('all',),
        required=True,
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
    for election_code in dict.fromkeys(selected):
        election = ELECTIONS[election_code]
        output_path = options.output_directory / '{}.json'.format(election_code)
        print('Loading ECSA operational evidence for {}...'.format(election_code))
        data = _download(election.tally_url)
        dataset = (
            turnout_data.load_dataset(output_path)
            if output_path.exists()
            else _new_dataset(election)
        )
        dataset = merge_dataset(dataset, election, data)
        if not options.dry_run:
            turnout_data.write_dataset_atomically(output_path, dataset)
        print(
            '  {} exact records across {} districts and the state'.format(
                len(dataset.operational_observations),
                election.expected_districts,
            )
        )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

"""Download and normalize final NSW Legislative Assembly turnout evidence.

This adapter reads final static Virtual Tally Room pages. It does not estimate
turnout or scrape candidate-level values: only the published district totals
and complete vote-type summary rows enter the normalized dataset.

Main functions:
* ``discover_district_urls`` validates the district links on the state page.
* ``download_election_pages`` obtains final district pages concurrently.
* ``build_dataset`` performs the NSWEC-to-normalized-data conversion.
* ``coverage_summary`` reports category shares and reconciliation coverage.
* ``main`` provides the ``--election`` and output-directory command line flow.
"""

import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path
import re
from urllib.parse import urljoin, urlparse
from urllib.request import Request, urlopen

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 30
DOWNLOAD_WORKERS = 6

COMMON_SOURCE_CATEGORIES = (
    'Total Voting Centre Ordinary Votes',
    'Total Early Voting Centre Ordinary Votes',
    'Absent',
    'Enrolment / Provisional',
    'Postal',
)

NSW_2015_SOURCE_CATEGORIES = (
    'Total Polling Place Ordinary Votes',
    'Total Pre-Poll Ordinary Votes',
    'Absent',
    'Enrolment',
    'iVote',
    'Postal',
    'Provisional / Silent',
)

NSW_2019_SOURCE_CATEGORIES = (
    *COMMON_SOURCE_CATEGORIES[:-1],
    'iVote',
    'Postal',
)

CANONICAL_CATEGORIES = {
    'Total Polling Place Ordinary Votes': 'election_day_ordinary',
    'Total Pre-Poll Ordinary Votes': 'early_combined',
    'Total Voting Centre Ordinary Votes': 'election_day_ordinary',
    'Total Early Voting Centre Ordinary Votes': 'early_combined',
    'Absent': 'absent',
    'Enrolment / Provisional': 'enrolment_or_provisional',
    'Enrolment': 'enrolment',
    'iVote': 'remote_electronic',
    'Postal': 'postal',
    'Provisional / Silent': 'provisional',
}

_DISTRICT_LINK_PATTERN = re.compile(
    r'/LA/[^/]+/cc/fp_summary(?:/index\.htm)?/?$',
    re.IGNORECASE,
)
_DISTRICT_NAME_PATTERN = re.compile(
    r'State Electoral District of\s+(.+?)\s+'
    r'(?:LA\s+-\s+Check Count|First Preference Votes)',
    re.IGNORECASE,
)
_ENROLMENT_PATTERN = re.compile(
    r'Electors Enrolled as on [^:]+:\s*([0-9,]+)',
    re.IGNORECASE,
)


@dataclass(frozen=True)
class NswElection:
    election_code: str
    election_date: str
    event_code: str
    expected_districts: int = 93
    results_path: str = 'LA/results'
    source_categories: tuple = COMMON_SOURCE_CATEGORIES

    @property
    def results_url(self):
        return (
            'https://pastvtr.elections.nsw.gov.au/{}/{}'.format(
                self.event_code, self.results_path
            )
        )


ELECTIONS = {
    '2015nsw': NswElection(
        '2015nsw',
        '2015-03-28',
        'SGE2015',
        results_path='la-home.htm',
        source_categories=NSW_2015_SOURCE_CATEGORIES,
    ),
    '2019nsw': NswElection(
        '2019nsw',
        '2019-03-23',
        'SG1901',
        source_categories=NSW_2019_SOURCE_CATEGORIES,
    ),
    '2023nsw': NswElection('2023nsw', '2023-03-25', 'SG2301'),
}


class _VtrHtmlParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.rows = []
        self.text_parts = []
        self._row = None
        self._cell = None

    def handle_starttag(self, tag, attrs):
        if tag == 'a':
            href = dict(attrs).get('href')
            if href:
                self.links.append(href)
        elif tag == 'tr':
            self._row = []
        elif tag in ('td', 'th') and self._row is not None:
            self._cell = []

    def handle_data(self, data):
        cleaned = ' '.join(data.split())
        if cleaned:
            self.text_parts.append(cleaned)
            if self._cell is not None:
                self._cell.append(cleaned)

    def handle_endtag(self, tag):
        if tag in ('td', 'th') and self._cell is not None:
            self._row.append(' '.join(self._cell))
            self._cell = None
        elif tag == 'tr' and self._row is not None:
            if self._row:
                self.rows.append(self._row)
            self._row = None


def _decode_html(data, label):
    try:
        return data.decode('utf-8-sig')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} is not valid UTF-8: {}'.format(label, error)
        )


def _parse_html(data, label):
    parser = _VtrHtmlParser()
    try:
        parser.feed(_decode_html(data, label))
        parser.close()
    except Exception as error:
        raise turnout_data.TurnoutDataError(
            '{} contains invalid HTML: {}'.format(label, error)
        )
    return parser


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


def discover_district_urls(election, results_page):
    """Return and validate final first-preference district page URLs."""
    parser = _parse_html(results_page, '{} results page'.format(
        election.election_code
    ))
    results_identity = urlparse(election.results_url)
    urls = set()
    for href in parser.links:
        url = urljoin(election.results_url, href)
        identity = urlparse(url)
        if (
            identity.scheme == results_identity.scheme
            and identity.netloc == results_identity.netloc
            and _DISTRICT_LINK_PATTERN.search(identity.path)
            and '/{}/LA/'.format(election.event_code).lower()
            in identity.path.lower()
        ):
            urls.add(url)
    urls = sorted(urls)
    if len(urls) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} results page lists {} final district pages, expected {}'.format(
                election.election_code, len(urls), election.expected_districts
            )
        )
    return urls


def download_election_pages(election):
    """Download the state index and all final district summary pages."""
    results_page = _download(election.results_url)
    district_urls = discover_district_urls(election, results_page)
    with ThreadPoolExecutor(max_workers=DOWNLOAD_WORKERS) as executor:
        pages = dict(zip(district_urls, executor.map(_download, district_urls)))
    return results_page, pages


def _count(value, label):
    try:
        count = int(value.replace(',', ''))
    except (AttributeError, ValueError):
        raise turnout_data.TurnoutDataError(
            '{} has invalid count {!r}'.format(label, value)
        )
    if count < 0:
        raise turnout_data.TurnoutDataError(
            '{} has negative count'.format(label)
        )
    return count


def _unique_row(rows, first_cell, label):
    matching = [row for row in rows if row and row[0] == first_cell]
    if len(matching) != 1:
        raise turnout_data.TurnoutDataError(
            '{} has {} {!r} rows, expected 1'.format(
                label, len(matching), first_cell
            )
        )
    return matching[0]


def _column_indices(rows, label):
    header = _unique_row(rows, 'Venue and Vote Types', label)
    required = ('Total Formal', 'Informal', 'Total Votes/ Ballot Papers')
    indices = {}
    for column in required:
        if header.count(column) != 1:
            raise turnout_data.TurnoutDataError(
                '{} has invalid {!r} column'.format(label, column)
            )
        indices[column] = header.index(column)
    return indices


def _row_counts(row, indices, label):
    try:
        formal = _count(row[indices['Total Formal']], '{} formal'.format(label))
        informal = _count(row[indices['Informal']], '{} informal'.format(label))
        total = _count(
            row[indices['Total Votes/ Ballot Papers']],
            '{} total'.format(label),
        )
    except IndexError:
        raise turnout_data.TurnoutDataError(
            '{} is missing summary columns'.format(label)
        )
    if formal + informal != total:
        raise turnout_data.TurnoutDataError(
            '{} formal and informal counts do not equal total'.format(label)
        )
    return formal, informal, total


def _parse_district_page(election, url, page):
    label = '{} {}'.format(election.election_code, url)
    parser = _parse_html(page, label)
    text = ' '.join(parser.text_parts)
    if (
        '/{}/LA/'.format(election.event_code).lower()
        not in urlparse(url).path.lower()
        or 'NSW STATE ELECTION RESULTS {}'.format(
            election.election_code[:4]
        ) not in text
    ):
        raise turnout_data.TurnoutDataError(
            '{} does not identify the requested election'.format(label)
        )
    if 'Check Count Complete.' not in text:
        raise turnout_data.TurnoutDataError(
            '{} is not a completed check count'.format(label)
        )

    district_match = _DISTRICT_NAME_PATTERN.search(text)
    enrolment_match = _ENROLMENT_PATTERN.search(text)
    if district_match is None or enrolment_match is None:
        raise turnout_data.TurnoutDataError(
            '{} is missing district identity or enrolment'.format(label)
        )
    district_name = district_match.group(1).strip()
    enrolment = _count(enrolment_match.group(1), '{} enrolment'.format(label))
    indices = _column_indices(parser.rows, label)

    category_counts = {}
    for source_category in election.source_categories:
        row = _unique_row(parser.rows, source_category, label)
        category_counts[source_category] = _row_counts(
            row, indices, '{} {}'.format(district_name, source_category)
        )

    total_row = _unique_row(parser.rows, 'Total Votes / Ballot Papers', label)
    totals = _row_counts(total_row, indices, '{} total'.format(district_name))
    summed = tuple(
        sum(counts[index] for counts in category_counts.values())
        for index in range(3)
    )
    if summed != totals:
        raise turnout_data.TurnoutDataError(
            '{} vote-type rows total {}, expected {}'.format(
                district_name, summed, totals
            )
        )
    if totals[2] > enrolment:
        raise turnout_data.TurnoutDataError(
            '{} ballots exceed enrolment'.format(district_name)
        )
    seat_match = re.search(r'/LA/([^/]+)/', urlparse(url).path, re.IGNORECASE)
    if seat_match is None:
        raise turnout_data.TurnoutDataError(
            '{} has an invalid district URL'.format(label)
        )
    source_seat_id = seat_match.group(1)
    return district_name, source_seat_id, enrolment, totals, category_counts


def build_dataset(election, district_pages):
    """Convert and fully reconcile one set of final NSWEC district pages."""
    if len(district_pages) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} has {} downloaded district pages, expected {}'.format(
                election.election_code,
                len(district_pages),
                election.expected_districts,
            )
        )

    parsed = [
        _parse_district_page(election, url, page)
        for url, page in sorted(district_pages.items())
    ]
    district_names = [record[0] for record in parsed]
    if len(set(district_names)) != len(district_names):
        raise turnout_data.TurnoutDataError(
            '{} contains duplicate district names'.format(election.election_code)
        )

    source_id = 'nswec-{}-la-final'.format(election.event_code.lower())
    notes = (
        'Early voting is the published aggregate of early voting centres, '
        'declared facilities and any small modes included by NSWEC. '
        'Enrolment / Provisional remains combined.'
    )
    if 'iVote' in election.source_categories:
        notes += ' iVote is retained as remote electronic voting.'
    dataset = turnout_data.TurnoutDataset(
        elections=[
            turnout_data.ElectionDefinition(
                election.election_code, election.election_date, 'nsw'
            )
        ],
        sources=[
            turnout_data.SourceDefinition(
                source_id=source_id,
                election_code=election.election_code,
                authority='NSW Electoral Commission',
                locator=election.results_url,
                adapter='nswec-vtr-final-v1',
                status='final',
                category_regime='nswec-la-vote-types-{}-v1'.format(
                    election.election_code[:4]
                ),
                notes=notes,
            )
        ],
    )

    for district_name, source_seat_id, enrolment, totals, categories in sorted(
        parsed
    ):
        dataset.seat_totals.append(
            turnout_data.SeatTotal(
                election_code=election.election_code,
                seat_name=district_name,
                source_id=source_id,
                enrolment=enrolment,
                formal_votes=totals[0],
                informal_votes=totals[1],
                total_ballots=totals[2],
                source_seat_id=source_seat_id,
                subdivision='nsw',
            )
        )
        for source_category in election.source_categories:
            formal, informal, total = categories[source_category]
            dataset.vote_types.append(
                turnout_data.VoteTypeRecord(
                    election_code=election.election_code,
                    seat_name=district_name,
                    source_id=source_id,
                    partition_id='final-ballot-vote-types',
                    source_category=source_category,
                    canonical_category=CANONICAL_CATEGORIES[source_category],
                    formal_votes=formal,
                    informal_votes=informal,
                    total_ballots=total,
                    coverage='complete',
                    derivation='sum_official_rows',
                )
            )

    dataset.validate()
    return dataset


def coverage_summary(dataset):
    """Return compact election-wide coverage totals for inspection."""
    enrolment = sum(record.enrolment for record in dataset.seat_totals)
    ballots = sum(record.total_ballots for record in dataset.seat_totals)
    informal = sum(record.informal_votes for record in dataset.seat_totals)
    categories = defaultdict(int)
    for record in dataset.vote_types:
        categories[record.source_category] += record.total_ballots
    return {
        'districts': len(dataset.seat_totals),
        'enrolment': enrolment,
        'total_ballots': ballots,
        'turnout_percent': ballots / enrolment * 100.0,
        'informal_percent': informal / ballots * 100.0,
        'category_ballots': dict(categories),
    }


def _print_summary(election, summary, output_path):
    print(
        '{}: {} districts; {:,} ballots from {:,} enrolled '
        '({:.2f}% turnout, {:.2f}% informal)'.format(
            election.election_code,
            summary['districts'],
            summary['total_ballots'],
            summary['enrolment'],
            summary['turnout_percent'],
            summary['informal_percent'],
        )
    )
    for category in election.source_categories:
        ballots = summary['category_ballots'][category]
        print(
            '  {:42} {:>10,} ({:5.2f}%)'.format(
                category,
                ballots,
                ballots / summary['total_ballots'] * 100.0,
            )
        )
    print('  Wrote {}'.format(output_path))


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize final NSW turnout evidence.'
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
        print('Downloading final NSW turnout pages for {}...'.format(election_code))
        _results_page, district_pages = download_election_pages(election)
        dataset = build_dataset(election, district_pages)
        output_path = options.output_directory / '{}.json'.format(election_code)
        turnout_data.write_dataset_atomically(output_path, dataset)
        _print_summary(election, coverage_summary(dataset), output_path)
    return 0


if __name__ == '__main__':
    main()

"""Download and normalize final Victorian Lower House turnout evidence.

This adapter reads final VEC district-result pages. It does not estimate
turnout or retain candidate results: only district totals and the complete
published vote-type rows enter the normalized dataset.

Main functions:
* ``discover_district_urls`` validates and converts district index links.
* ``download_election_pages`` obtains final district pages concurrently.
* ``build_dataset`` performs the VEC-to-normalized-data conversion.
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

SOURCE_CATEGORIES = (
    'Ordinary votes total',
    'Absent votes',
    'Early votes',
    'Marked As Voted votes',
    'Postal votes',
    'Provisional votes',
)

SOURCE_CATEGORIES_2006 = (
    'Ordinary votes total',
    'Postal votes',
    'Early votes',
    'Declaration votes',
    'Absent votes',
)

CANONICAL_CATEGORIES = {
    'ordinary votes total': 'election_day_ordinary',
    'absent votes': 'absent',
    'early votes': 'early_combined',
    'marked as voted votes': 'marked_as_voted',
    'postal votes': 'postal',
    'provisional votes': 'provisional',
    'declaration votes': 'declaration_combined',
}

_TOTAL_PATTERNS = {
    'enrolment': re.compile(
        r'Total Enrolment(?:\s+as at close of rolls)?:\s*([0-9,]+)',
        re.IGNORECASE,
    ),
    'formal': re.compile(r'Formal Votes:\s*([0-9,]+)', re.IGNORECASE),
    'informal': re.compile(r'Informal Votes:\s*([0-9,]+)', re.IGNORECASE),
    'total': re.compile(r'Total Votes:\s*([0-9,]+)', re.IGNORECASE),
}


@dataclass(frozen=True)
class VicElection:
    election_code: str
    election_date: str
    index_url: str
    expected_districts: int
    page_style: str
    districts_without_vote_types: tuple = ()
    summary_only_result_files: tuple = ()


ELECTIONS = {
    '2006vic': VicElection(
        '2006vic',
        '2006-11-25',
        (
            'https://itsitecoreblobvecprd01.blob.core.windows.net/'
            'public-files/historical-results/state2006/'
            'state2006resultsummary.html'
        ),
        88,
        'historical',
        ('Ferntree Gully District',),
        ('state2006resultferntreegullydistrict.html',),
    ),
    '2010vic': VicElection(
        '2010vic',
        '2010-11-27',
        (
            'https://itsitecoreblobvecprd01.blob.core.windows.net/'
            'public-files/historical-results/state2010/'
            'state2010resultsummary.html'
        ),
        88,
        'historical',
    ),
    '2014vic': VicElection(
        '2014vic',
        '2014-11-29',
        (
            'https://itsitecoreblobvecprd01.blob.core.windows.net/'
            'public-files/historical-results/state2014/summary.html'
        ),
        88,
        'historical',
        ('Prahran District',),
    ),
    '2018vic': VicElection(
        '2018vic',
        '2018-11-24',
        (
            'https://itsitecoreblobvecprd01.blob.core.windows.net/'
            'public-files/historical-results/state2018/summary.html'
        ),
        88,
        'historical',
        ('Brunswick District', 'Ripon District'),
        ('brunswickdistrict.html',),
    ),
    '2022vic': VicElection(
        '2022vic',
        '2022-11-26',
        (
            'https://www.vec.vic.gov.au/results/state-election-results/'
            '2022-state-election-results/results-by-district'
        ),
        87,
        'current',
    ),
}


class _TableHtmlParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.headings = []
        self.rows = []
        self.text_parts = []
        self._row = None
        self._cell = None
        self._heading = None

    def handle_starttag(self, tag, attrs):
        if tag == 'a':
            href = dict(attrs).get('href')
            if href:
                self.links.append(href)
        elif tag == 'tr':
            self._row = []
        elif tag in ('h1', 'h2'):
            self._heading = []
        elif tag in ('td', 'th') and self._row is not None:
            self._cell = []

    def handle_data(self, data):
        cleaned = ' '.join(data.split())
        if cleaned:
            self.text_parts.append(cleaned)
            if self._cell is not None:
                self._cell.append(cleaned)
            if self._heading is not None:
                self._heading.append(cleaned)

    def handle_endtag(self, tag):
        if tag in ('td', 'th') and self._cell is not None:
            self._row.append(' '.join(self._cell))
            self._cell = None
        elif tag == 'tr' and self._row is not None:
            if self._row:
                self.rows.append(self._row)
            self._row = None
        elif tag in ('h1', 'h2') and self._heading is not None:
            if self._heading:
                self.headings.append(' '.join(self._heading))
            self._heading = None


def _parse_html(data, label):
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} is not valid UTF-8: {}'.format(label, error)
        )
    parser = _TableHtmlParser()
    try:
        parser.feed(text)
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


def _historical_result_url(index_url, href):
    filename = Path(urlparse(href).path).name
    if not re.fullmatch(r'[a-z0-9-]+district\.html', filename, re.IGNORECASE):
        return None
    old_style = re.fullmatch(
        r'(state(?:2006|2010))result(.+district\.html)',
        filename,
        re.IGNORECASE,
    )
    if old_style:
        result_filename = '{}fpvbyvotingcentre{}'.format(
            old_style.group(1), old_style.group(2)
        )
    else:
        result_filename = 'fpvbyvotingcentre{}'.format(filename)
    return urljoin(index_url, result_filename)


def _current_result_url(index_url, href):
    url = urljoin(index_url, href)
    path = urlparse(url).path.rstrip('/')
    match = re.search(r'/results-by-district/([^/]+-district-results)$', path)
    if match is None:
        return None
    return '{}/{}-by-voting-centre'.format(url.rstrip('/'), match.group(1))


def discover_district_urls(election, index_page):
    """Return and validate final first-preference district page URLs."""
    parser = _parse_html(
        index_page, '{} results index'.format(election.election_code)
    )
    converter = (
        _historical_result_url
        if election.page_style == 'historical'
        else _current_result_url
    )
    index_identity = urlparse(election.index_url)
    urls = set()
    for href in parser.links:
        href_filename = Path(urlparse(href).path).name.lower()
        if href_filename in election.summary_only_result_files:
            url = urljoin(election.index_url, href)
        else:
            url = converter(election.index_url, href)
        if url is None:
            continue
        identity = urlparse(url)
        if (
            identity.scheme == index_identity.scheme
            and identity.netloc == index_identity.netloc
        ):
            urls.add(url)
    urls = sorted(urls)
    if len(urls) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} results index lists {} districts, expected {}'.format(
                election.election_code, len(urls), election.expected_districts
            )
        )
    return urls


def download_election_pages(election):
    """Download the election index and all final district result pages."""
    index_page = _download(election.index_url)
    district_urls = discover_district_urls(election, index_page)
    with ThreadPoolExecutor(max_workers=DOWNLOAD_WORKERS) as executor:
        pages = dict(zip(district_urls, executor.map(_download, district_urls)))
    return index_page, pages


def _count(value, label):
    match = re.match(r'\s*([0-9][0-9,]*)', value or '')
    if match is None:
        raise turnout_data.TurnoutDataError(
            '{} has invalid count {!r}'.format(label, value)
        )
    count = int(match.group(1).replace(',', ''))
    return count


def _unique_row(rows, first_cell, label):
    matching = [
        row for row in rows
        if row and row[0].strip().lower() == first_cell.lower()
    ]
    if len(matching) != 1:
        raise turnout_data.TurnoutDataError(
            '{} has {} {!r} rows, expected 1'.format(
                label, len(matching), first_cell
            )
        )
    return matching[0]


def _district_name(election, parser, label):
    text = ' '.join(parser.text_parts)
    if election.page_style == 'historical':
        pattern = re.compile(
            r'^(.+? District)\s+First Preference Results by Voting Centre$',
            re.IGNORECASE,
        )
        matches = [
            pattern.fullmatch(heading) for heading in parser.headings
        ]
        matches = [match for match in matches if match is not None]
        if len(matches) == 1:
            match = matches[0]
        else:
            summary_headings = [
                heading for heading in parser.headings
                if heading in election.districts_without_vote_types
            ]
            match = (
                re.fullmatch(r'(.+ District)', summary_headings[0])
                if len(summary_headings) == 1 else None
            )
    else:
        pattern = re.compile(
            r'(.+? District) results by voting centre\s*\|\s*'
            r'Victorian Electoral Commission',
            re.IGNORECASE,
        )
        match = pattern.search(text)
    if match is None:
        raise turnout_data.TurnoutDataError(
            '{} is missing its district identity'.format(label)
        )
    return match.group(1).strip()


def _published_totals(text, label):
    totals = {}
    for name, pattern in _TOTAL_PATTERNS.items():
        match = pattern.search(text)
        if match is None:
            raise turnout_data.TurnoutDataError(
                '{} is missing its published {} total'.format(label, name)
            )
        totals[name] = _count(match.group(1), '{} {}'.format(label, name))
    if totals['formal'] + totals['informal'] != totals['total']:
        raise turnout_data.TurnoutDataError(
            '{} published formal and informal totals do not reconcile'.format(
                label
            )
        )
    if totals['total'] > totals['enrolment']:
        raise turnout_data.TurnoutDataError(
            '{} published ballots exceed enrolment'.format(label)
        )
    return totals


def _row_counts(row, candidate_columns, label):
    expected_columns = candidate_columns + 3
    if len(row) != expected_columns:
        raise turnout_data.TurnoutDataError(
            '{} has {} columns, expected {}'.format(
                label, len(row), expected_columns
            )
        )
    formal = sum(
        _count(value, '{} candidate'.format(label))
        for value in row[1:1 + candidate_columns]
    )
    informal = _count(row[-2], '{} informal'.format(label))
    total = _count(row[-1], '{} total'.format(label))
    if formal + informal != total:
        raise turnout_data.TurnoutDataError(
            '{} formal and informal counts do not equal total'.format(label)
        )
    return formal, informal, total


def _parse_district_page(election, url, page):
    label = '{} {}'.format(election.election_code, url)
    parser = _parse_html(page, label)
    text = ' '.join(parser.text_parts)
    final_result_markers = (
        'Recheck first preference votes',
        'Recount first preference votes',
    )
    lowered_text = text.lower()
    has_final_marker = any(
        marker.lower() in lowered_text for marker in final_result_markers
    )
    if election.election_code == '2006vic':
        has_final_marker = has_final_marker or 'primary votes' in lowered_text
    if not has_final_marker:
        raise turnout_data.TurnoutDataError(
            '{} is not a final recheck or recount result'.format(label)
        )
    district_name = _district_name(election, parser, label)
    published = _published_totals(text, district_name)
    if district_name in election.districts_without_vote_types:
        return district_name, published, {}

    header = _unique_row(parser.rows, 'Voting Centres', district_name)
    if len(header) < 4 or [cell.lower() for cell in header[-2:]] != [
        'informal votes', 'total votes polled'
    ]:
        raise turnout_data.TurnoutDataError(
            '{} has invalid result summary columns'.format(district_name)
        )
    candidate_columns = len(header) - 3
    categories = {}
    source_categories = (
        SOURCE_CATEGORIES_2006
        if election.election_code == '2006vic'
        else SOURCE_CATEGORIES
    )
    for source_category in source_categories:
        row = _unique_row(parser.rows, source_category, district_name)
        categories[row[0]] = _row_counts(
            row, candidate_columns, '{} {}'.format(district_name, row[0])
        )

    total_row = _unique_row(parser.rows, 'Total', district_name)
    row_total = _row_counts(total_row, candidate_columns, '{} total'.format(
        district_name
    ))
    published_tuple = (
        published['formal'], published['informal'], published['total']
    )
    if row_total != published_tuple:
        raise turnout_data.TurnoutDataError(
            '{} table total {} differs from published {}'.format(
                district_name, row_total, published_tuple
            )
        )
    category_total = tuple(
        sum(counts[index] for counts in categories.values())
        for index in range(3)
    )
    if category_total != row_total:
        if (
            district_name in election.districts_without_vote_types
            and category_total == (0, 0, 0)
        ):
            categories = {}
        else:
            raise turnout_data.TurnoutDataError(
                '{} vote-type rows total {}, expected {}'.format(
                    district_name, category_total, row_total
                )
            )
    return district_name, published, categories


def build_dataset(election, district_pages):
    """Convert and fully reconcile one set of final VEC district pages."""
    if len(district_pages) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} has {} district pages, expected {}'.format(
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
    missing_vote_types = {
        district_name for district_name, _totals, categories in parsed
        if not categories
    }
    expected_missing = set(election.districts_without_vote_types)
    if missing_vote_types != expected_missing:
        raise turnout_data.TurnoutDataError(
            '{} has unavailable vote types for {}, expected {}'.format(
                election.election_code,
                ', '.join(sorted(missing_vote_types)) or 'none',
                ', '.join(sorted(expected_missing)) or 'none',
            )
        )

    source_id = 'vec-{}-lower-house-final'.format(election.election_code[:4])
    category_regime = (
        'vec-lower-house-vote-types-2006'
        if election.election_code == '2006vic'
        else 'vec-lower-house-vote-types-v1'
    )
    source_notes = (
        'Early votes retain the VEC aggregate. The VEC Declaration Votes '
        'category remains combined.'
        if election.election_code == '2006vic'
        else (
            'Early votes retain the VEC aggregate, including small '
            'early-voting modes. Marked As Voted remains separate.'
        )
    )
    if election.election_code == '2014vic':
        source_notes += (
            ' Prahran has final recount totals but no recount vote-type '
            'breakdown, as documented by VEC.'
        )
    elif election.election_code == '2018vic':
        source_notes += (
            ' Ripon has final recount totals but no recount vote-type '
            'breakdown. The linked Brunswick voting-centre page contains '
            'stale totals, so its current district-summary totals are used '
            'without that unreliable partition.'
        )
    elif election.election_code == '2022vic':
        source_notes += (
            ' Narracan is excluded because its November 2022 district '
            'election failed; the replacement contest was held in 2023.'
        )
    dataset = turnout_data.TurnoutDataset(
        elections=[
            turnout_data.ElectionDefinition(
                election.election_code, election.election_date, 'vic'
            )
        ],
        sources=[
            turnout_data.SourceDefinition(
                source_id=source_id,
                election_code=election.election_code,
                authority='Victorian Electoral Commission',
                locator=election.index_url,
                adapter='vec-final-vote-types-v1',
                status='final',
                category_regime=category_regime,
                notes=source_notes,
            )
        ],
    )
    for district_name, totals, categories in sorted(parsed):
        source_seat_id = (
            district_name[:-len(' District')]
            if district_name.endswith(' District') else district_name
        )
        dataset.seat_totals.append(
            turnout_data.SeatTotal(
                election_code=election.election_code,
                seat_name=district_name,
                source_id=source_id,
                enrolment=totals['enrolment'],
                formal_votes=totals['formal'],
                informal_votes=totals['informal'],
                total_ballots=totals['total'],
                source_seat_id=source_seat_id,
                subdivision='vic',
            )
        )
        for source_category, counts in categories.items():
            dataset.vote_types.append(
                turnout_data.VoteTypeRecord(
                    election_code=election.election_code,
                    seat_name=district_name,
                    source_id=source_id,
                    partition_id='final-ballot-vote-types',
                    source_category=source_category,
                    canonical_category=CANONICAL_CATEGORIES[
                        source_category.lower()
                    ],
                    formal_votes=counts[0],
                    informal_votes=counts[1],
                    total_ballots=counts[2],
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
        categories[record.canonical_category] += record.total_ballots
    return {
        'districts': len(dataset.seat_totals),
        'districts_with_vote_types': len({
            record.seat_name for record in dataset.vote_types
        }),
        'enrolment': enrolment,
        'total_ballots': ballots,
        'turnout_percent': ballots / enrolment * 100.0,
        'informal_percent': informal / ballots * 100.0,
        'category_ballots': dict(categories),
        'category_coverage_percent': sum(categories.values()) / ballots * 100.0,
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
    if summary['districts_with_vote_types'] != summary['districts']:
        print(
            '  Vote-type coverage: {}/{} districts ({:.2f}% of ballots)'.format(
                summary['districts_with_vote_types'],
                summary['districts'],
                summary['category_coverage_percent'],
            )
        )
    for category, ballots in sorted(summary['category_ballots'].items()):
        print(
            '  {:24} {:>10,} ({:5.2f}%)'.format(
                category, ballots, ballots / summary['total_ballots'] * 100.0
            )
        )
    print('  Wrote {}'.format(output_path))


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize final Victorian turnout evidence.'
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
        print('Downloading final VEC turnout pages for {}...'.format(election_code))
        _index_page, district_pages = download_election_pages(election)
        dataset = build_dataset(election, district_pages)
        output_path = options.output_directory / '{}.json'.format(election_code)
        turnout_data.write_dataset_atomically(output_path, dataset)
        _print_summary(election_code, coverage_summary(dataset), output_path)
    return 0


if __name__ == '__main__':
    main()

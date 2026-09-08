"""Download and normalize final Western Australian turnout evidence.

This adapter reads the Legislative Assembly "Types of Votes by District"
tables in WAEC's final results and statistics reports. For 2025, it also uses
the final verbose results XML to separate the report's combined ordinary and
early total into election-day, early in-person and mobile polling.

Main functions:
* ``extract_table_text`` reads the configured report pages with pypdf.
* ``parse_report_table`` handles contiguous and split-column PDF layouts.
* ``parse_verbose_results`` reconstructs the exact 2025 polling-place split.
* ``build_dataset`` validates report totals and builds normalized records.
* ``coverage_summary`` reports category shares and reconciliation coverage.
* ``main`` provides the ``--election`` and output-directory command line flow.
"""

import argparse
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
import re
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 60


@dataclass(frozen=True)
class WaElection:
    election_code: str
    election_date: str
    report_url: str
    table_pages: tuple
    expected_districts: int
    layout: str
    expected_totals: tuple
    district_names: tuple = ()
    ordinary_combines_early: bool = False
    verbose_results_url: str = ''


@dataclass(frozen=True)
class DistrictTurnout:
    name: str
    enrolment: int
    ordinary: int
    absent: int
    postal: int
    early: int
    provisional: int
    formal: int
    informal: int
    total: int


@dataclass(frozen=True)
class PollingPlaceSplit:
    election_day: int
    early: int
    mobile: int


_2017_DISTRICTS = (
    'Albany', 'Armadale', 'Balcatta', 'Baldivis', 'Bassendean', 'Bateman',
    'Belmont', 'Bicton', 'Bunbury', 'Burns Beach', 'Butler', 'Cannington',
    'Carine', 'Central Wheatbelt', 'Churchlands', 'Cockburn',
    'Collie-Preston', 'Cottesloe', 'Darling Range', 'Dawesville',
    'Forrestfield', 'Fremantle', 'Geraldton', 'Girrawheen', 'Hillarys',
    'Jandakot', 'Joondalup', 'Kalamunda', 'Kalgoorlie', 'Kimberley',
    'Kingsley', 'Kwinana', 'Mandurah', 'Maylands', 'Midland', 'Mirrabooka',
    'Moore', 'Morley', 'Mount Lawley', 'Murray-Wellington', 'Nedlands',
    'North West Central', 'Perth', 'Pilbara', 'Riverton', 'Rockingham', 'Roe',
    'Scarborough', 'South Perth', 'Southern River', 'Swan Hills', 'Thornlie',
    'Vasse', 'Victoria Park', 'Wanneroo', 'Warnbro', 'Warren-Blackwood',
    'West Swan', 'Willagee',
)

_2021_DISTRICTS = tuple(
    name for name in _2017_DISTRICTS if name != 'Girrawheen'
)
_2021_DISTRICTS = tuple(sorted(_2021_DISTRICTS + ('Landsdale',)))

_REPORT_ROOT = 'https://www.elections.wa.gov.au/sites/default/files/'
_2025_VERBOSE_RESULTS_URL = (
    'http://media.waec.wa.gov.au/Archive/2025%20SGE%20Final/'
    '8%20March%202025%20State%20General%20Election%20-%20LA%20VERBOSE%20RESULTS.xml'
)
ELECTIONS = {
    '2005wa': WaElection(
        '2005wa',
        '2005-02-26',
        _REPORT_ROOT + 'content/documents/2005_SGE_Results_and_Stats.pdf',
        (31,),
        57,
        'contiguous',
        (1259262, 882252, 118424, 37651, 33098, 528, 1071953, 59316, 1131269),
    ),
    '2008wa': WaElection(
        '2008wa',
        '2008-09-06',
        _REPORT_ROOT + 'content/documents/2008_SGE_Results_Stats_Part3.pdf',
        (17,),
        59,
        'contiguous',
        (1330399, 865000, 110883, 54686, 57497, 1191, 1089257, 61240, 1150497),
    ),
    '2013wa': WaElection(
        '2013wa',
        '2013-03-09',
        _REPORT_ROOT + 'content/documents/2013_SGE_Results_Stats_Part3.pdf',
        (18,),
        59,
        'contiguous',
        (1412533, 910379, 119155, 74493, 79193, 1212, 1184432, 75657, 1260089),
    ),
    '2017wa': WaElection(
        '2017wa',
        '2017-03-11',
        _REPORT_ROOT + 'content/documentssge2017/2017_Legislative%20Assembly_online.pdf',
        (20,),
        59,
        'split-ordinary',
        (1593222, 847640, 147088, 111761, 214242, 909, 1321640, 62860, 1384500),
        _2017_DISTRICTS,
    ),
    '2021wa': WaElection(
        '2021wa',
        '2021-03-13',
        (
            _REPORT_ROOT
            + 'content/SGE%202021/SGE%202021%20Reports/'
            + '3_21_Res_Stats_Leg_Assem_0.pdf'
        ),
        (24,),
        59,
        'split-ordinary',
        (1716732, 537446, 97047, 212872, 564510, 115, 1411990, 55183, 1467173),
        _2021_DISTRICTS,
    ),
    '2025wa': WaElection(
        '2025wa',
        '2025-03-08',
        (
            _REPORT_ROOT
            + 'SGE2025/Reports/'
            + 'WAEC9467%20State%20Election%20Stats%2BResults%20Report%20WEB.pdf'
        ),
        (36, 37),
        59,
        'contiguous-combined',
        (1881825, 1049968, 288130, 168561, 0, 21309, 1527968, 69071, 1597039),
        ordinary_combines_early=True,
        verbose_results_url=_2025_VERBOSE_RESULTS_URL,
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
        return response.read()


def extract_table_text(pdf_data, election):
    """Extract only the configured Legislative Assembly table pages."""
    try:
        from pypdf import PdfReader
    except ImportError:
        raise turnout_data.TurnoutDataError(
            'turnout_wa.py requires pypdf; install analysis/requirements.txt'
        )

    try:
        reader = PdfReader(BytesIO(pdf_data))
        pages = []
        for page_index in election.table_pages:
            if page_index >= len(reader.pages):
                raise turnout_data.TurnoutDataError(
                    '{} report has {} pages, expected page {}'.format(
                        election.election_code, len(reader.pages), page_index + 1
                    )
                )
            pages.append(reader.pages[page_index].extract_text() or '')
    except turnout_data.TurnoutDataError:
        raise
    except Exception as error:
        raise turnout_data.TurnoutDataError(
            '{} is not a readable PDF: {}'.format(election.election_code, error)
        )

    text = '\n'.join(pages)
    normalized_text = ' '.join(text.split()).casefold()
    year = election.election_code[:4]
    election_title = (
        '{} WA State Election'.format(year)
        if election.ordinary_combines_early
        else '{} State General Election'.format(year)
    )
    required_fragments = (
        election_title,
        'Legislative Assembly',
        'Types of Votes by District',
    )
    if any(fragment.casefold() not in normalized_text for fragment in required_fragments):
        raise turnout_data.TurnoutDataError(
            '{} configured pages do not identify the expected table'.format(
                election.election_code
            )
        )
    return text


def _number(value):
    return int(value.replace(',', ''))


def _xml_count(node, attribute, context):
    value = node.attrib.get(attribute)
    if value is None or not value.isdigit():
        raise turnout_data.TurnoutDataError(
            '{} has invalid {}={!r}'.format(context, attribute, value)
        )
    return int(value)


def _row_pattern(count_columns, include_name):
    numbers = r'\d[\d,]*' + (r'\s+\d[\d,]*' * (count_columns - 1))
    prefix = r'(?P<name>.+?)\s+' if include_name else ''
    return re.compile(
        r'^{}(?P<counts>{})\s+(?P<turnout>\d+\.\d+%)$'.format(
            prefix, numbers
        )
    )


def _normalized_lines(text):
    # WAEC's 2025 text layer wraps Warren-Blackwood within the table row.
    text = re.sub(r'([A-Za-z]+-)\s*\n\s*([A-Za-z]+)(?=\s+\d)', r'\1\2', text)
    return [' '.join(line.split()) for line in text.splitlines() if line.strip()]


def _validate_row(row):
    categories = row.ordinary + row.absent + row.postal + row.early + row.provisional
    if categories != row.formal:
        raise turnout_data.TurnoutDataError(
            '{} vote types total {}, expected {}'.format(
                row.name, categories, row.formal
            )
        )
    if row.formal + row.informal != row.total:
        raise turnout_data.TurnoutDataError(
            '{} formal and informal votes do not equal total votes'.format(row.name)
        )
    if row.total > row.enrolment:
        raise turnout_data.TurnoutDataError(
            '{} total votes exceed enrolment'.format(row.name)
        )


def _contiguous_rows(text, election):
    count_columns = 8 if election.ordinary_combines_early else 9
    pattern = _row_pattern(count_columns, include_name=True)
    records = []
    for line in _normalized_lines(text):
        match = pattern.fullmatch(line)
        if match is None or match.group('name') in ('Total', 'Percentage'):
            continue
        values = [_number(value) for value in match.group('counts').split()]
        if election.ordinary_combines_early:
            enrolment, ordinary, absent, postal, provisional, formal, informal, total = values
            early = 0
        else:
            (
                enrolment,
                ordinary,
                absent,
                postal,
                early,
                provisional,
                formal,
                informal,
                total,
            ) = values
        record = DistrictTurnout(
            match.group('name'), enrolment, ordinary, absent, postal, early,
            provisional, formal, informal, total,
        )
        _validate_row(record)
        records.append(record)
    return records


def _compact_identity(value):
    return ''.join(character for character in value.casefold() if character.isalnum())


def _split_ordinary_rows(text, election):
    if len(election.district_names) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} has no complete district identity list'.format(election.election_code)
        )
    compact_text = _compact_identity(text)
    missing = [
        name for name in election.district_names
        if _compact_identity(name) not in compact_text
    ]
    if missing:
        raise turnout_data.TurnoutDataError(
            '{} report is missing district labels: {}'.format(
                election.election_code, ', '.join(missing)
            )
        )

    pattern = _row_pattern(8, include_name=False)
    numeric_rows = []
    for line in _normalized_lines(text):
        match = pattern.fullmatch(line)
        if match is not None:
            numeric_rows.append(
                [_number(value) for value in match.group('counts').split()]
            )
    if len(numeric_rows) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} contains {} numeric district rows, expected {}'.format(
                election.election_code,
                len(numeric_rows),
                election.expected_districts,
            )
        )

    records = []
    for name, values in zip(election.district_names, numeric_rows):
        enrolment, absent, postal, early, provisional, formal, informal, total = values
        ordinary = formal - absent - postal - early - provisional
        record = DistrictTurnout(
            name, enrolment, ordinary, absent, postal, early, provisional,
            formal, informal, total,
        )
        _validate_row(record)
        records.append(record)
    return records


def parse_report_table(text, election):
    """Parse one configured WAEC district vote-type table."""
    if election.layout in ('contiguous', 'contiguous-combined'):
        records = _contiguous_rows(text, election)
    elif election.layout == 'split-ordinary':
        records = _split_ordinary_rows(text, election)
    else:
        raise turnout_data.TurnoutDataError(
            'unsupported WA report layout {!r}'.format(election.layout)
        )
    if len(records) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} contains {} district rows, expected {}'.format(
                election.election_code, len(records), election.expected_districts
            )
        )
    names = [record.name for record in records]
    if len(set(names)) != len(names):
        raise turnout_data.TurnoutDataError(
            '{} contains duplicate district names'.format(election.election_code)
        )
    return records


def parse_verbose_results(xml_data, election, report_records):
    """Split 2025 combined ordinary votes using final polling-place rows."""
    try:
        root = ET.fromstring(xml_data)
    except (ET.ParseError, TypeError, ValueError) as error:
        raise turnout_data.TurnoutDataError(
            '{} verbose results XML is invalid: {}'.format(
                election.election_code, error
            )
        )

    if (
        root.attrib.get('ElectionDate') != election.election_date
        or 'State General Election' not in root.attrib.get('Name', '')
    ):
        raise turnout_data.TurnoutDataError(
            '{} verbose results XML identifies the wrong election'.format(
                election.election_code
            )
        )

    expected_records = {record.name: record for record in report_records}
    splits = {}
    for district in root.findall('.//{*}ElectionDistrict'):
        name = district.attrib.get('Name', '')
        if not name or name in splits:
            raise turnout_data.TurnoutDataError(
                '{} verbose results XML has a missing or duplicate district'.format(
                    election.election_code
                )
            )

        assembly_results = district.findall('./{*}LA')
        if (
            len(assembly_results) != 1
            or assembly_results[0].attrib.get('ElectionStatus') != 'Results Declared'
        ):
            raise turnout_data.TurnoutDataError(
                '{} {} does not contain one declared Assembly result'.format(
                    election.election_code, name
                )
            )
        primary_counts = assembly_results[0].findall(
            './{*}DistrictVotes[@CountDefinitionCode="LAPC"]'
        )
        if len(primary_counts) != 1:
            raise turnout_data.TurnoutDataError(
                '{} {} has {} primary counts, expected one'.format(
                    election.election_code, name, len(primary_counts)
                )
            )
        primary = primary_counts[0]
        if primary.attrib.get('DistrictName') != name:
            raise turnout_data.TurnoutDataError(
                '{} primary-count district identity does not match {}'.format(
                    election.election_code, name
                )
            )

        election_day = 0
        early = 0
        place_names = set()
        for place in primary.findall('./{*}OrdinaryPollingPlaceVotes'):
            place_name = place.attrib.get('OrdinaryPollingPlaceName', '')
            if not place_name or place_name in place_names:
                raise turnout_data.TurnoutDataError(
                    '{} {} has a missing or duplicate polling-place name'.format(
                        election.election_code, name
                    )
                )
            place_names.add(place_name)
            votes = _xml_count(
                place, 'FormalVotes', '{} {}'.format(name, place_name)
            )
            # Match the WAEC designation, not ordinary venues such as an
            # "Early Learning Centre" whose name happens to contain "Early".
            if 'early polling place' in place_name.casefold():
                early += votes
            else:
                election_day += votes

        categories = {}
        expected_category_names = {
            'SIR': 'Mobile Polling',
            'AV': 'Absent Votes',
            'POV': 'Postal Votes',
            'PRV': 'Provisional Votes',
        }
        for category in primary.findall('./{*}CategoryVotes'):
            code = category.attrib.get('CategoryCode', '')
            category_name = category.attrib.get('CategoryName', '')
            if code not in expected_category_names:
                raise turnout_data.TurnoutDataError(
                    '{} {} has unsupported category {!r}'.format(
                        election.election_code, name, category_name or code
                    )
                )
            if category_name != expected_category_names[code] or code in categories:
                raise turnout_data.TurnoutDataError(
                    '{} {} has inconsistent category {}'.format(
                        election.election_code, name, code
                    )
                )
            categories[code] = _xml_count(
                category, 'FormalVotes', '{} {}'.format(name, category_name)
            )
        missing_categories = {'AV', 'POV', 'PRV'} - set(categories)
        if missing_categories:
            raise turnout_data.TurnoutDataError(
                '{} {} is missing categories: {}'.format(
                    election.election_code,
                    name,
                    ', '.join(sorted(missing_categories)),
                )
            )

        xml_formal = election_day + early + sum(categories.values())
        primary_formal = _xml_count(
            primary, 'FormalVotes', '{} primary count'.format(name)
        )
        if xml_formal != primary_formal:
            raise turnout_data.TurnoutDataError(
                '{} XML categories total {}, expected primary formal {}'.format(
                    name, xml_formal, primary_formal
                )
            )

        report_record = expected_records.get(name)
        if report_record is None:
            raise turnout_data.TurnoutDataError(
                '{} verbose results XML has unexpected district {}'.format(
                    election.election_code, name
                )
            )
        mobile = categories.get('SIR', 0)
        split_total = election_day + early + mobile
        if split_total != report_record.ordinary:
            raise turnout_data.TurnoutDataError(
                '{} polling-place split totals {}, expected combined ordinary {}'.format(
                    name, split_total, report_record.ordinary
                )
            )
        splits[name] = PollingPlaceSplit(election_day, early, mobile)

    missing_districts = set(expected_records) - set(splits)
    if missing_districts:
        raise turnout_data.TurnoutDataError(
            '{} verbose results XML is missing districts: {}'.format(
                election.election_code, ', '.join(sorted(missing_districts))
            )
        )
    return splits


def _aggregate_totals(records):
    fields = (
        'enrolment', 'ordinary', 'absent', 'postal', 'early', 'provisional',
        'formal', 'informal', 'total',
    )
    return tuple(sum(getattr(record, field) for record in records) for field in fields)


def build_dataset(election, table_text, verbose_results_xml=None):
    """Convert and fully reconcile one final WAEC report table."""
    records = parse_report_table(table_text, election)
    totals = _aggregate_totals(records)
    if totals != election.expected_totals:
        raise turnout_data.TurnoutDataError(
            '{} aggregate totals {} differ from published {}'.format(
                election.election_code, totals, election.expected_totals
            )
        )

    report_source_id = 'waec-{}-assembly-final'.format(election.election_code[:4])
    polling_place_splits = None
    if election.ordinary_combines_early:
        if verbose_results_xml is None:
            raise turnout_data.TurnoutDataError(
                '{} requires final verbose results XML to split ordinary votes'.format(
                    election.election_code
                )
            )
        polling_place_splits = parse_verbose_results(
            verbose_results_xml, election, records
        )

    sources = [
        turnout_data.SourceDefinition(
            source_id=report_source_id,
            election_code=election.election_code,
            authority='Western Australian Electoral Commission',
            locator=election.report_url,
            adapter='waec-results-report-pdf-v1',
            status='final',
            category_regime=(
                'waec-ordinary-includes-early-v1'
                if election.ordinary_combines_early
                else 'waec-separated-early-v1'
            ),
            notes=(
                'Formal vote-type counts come from the Legislative Assembly '
                'Types of Votes by District table. The 2025 table supplies final '
                'district, absent, postal and provisional controls but combines '
                'ordinary, early and mobile polling. For 2017 and 2021, ordinary '
                'is reconstructed exactly from the official formal total because '
                'the PDF text layer separates its column.'
            ),
        )
    ]
    xml_source_id = None
    if polling_place_splits is not None:
        xml_source_id = 'waec-2025-assembly-final-xml'
        sources.append(
            turnout_data.SourceDefinition(
                source_id=xml_source_id,
                election_code=election.election_code,
                authority='Western Australian Electoral Commission',
                locator=election.verbose_results_url,
                adapter='waec-la-verbose-results-xml-v1',
                status='final',
                category_regime='waec-polling-place-name-split-v1',
                notes=(
                    'Final primary-count polling places whose names contain the '
                    'exact phrase Early Polling Place supply early in-person votes; '
                    'other polling places supply election-day votes, and Mobile '
                    'Polling remains separate. Every district split reconciles to '
                    'the later statistical report combined ordinary/early total. '
                    'Declaration categories remain sourced from that report; its '
                    'Riverton provisional total is three below the XML value.'
                ),
            )
        )

    dataset = turnout_data.TurnoutDataset(
        elections=[
            turnout_data.ElectionDefinition(
                election.election_code, election.election_date, 'wa'
            )
        ],
        sources=sources,
    )
    categories = (
        ('Ordinary', 'ordinary', (
            'ordinary_combined'
            if election.ordinary_combines_early
            else 'election_day_ordinary'
        )),
        ('Absent', 'absent', 'absent'),
        ('Early Votes (by Post)', 'postal', 'postal'),
        ('Early Votes (in Person)', 'early', 'early_in_person'),
        ('Provisional', 'provisional', 'provisional'),
    )
    for record in sorted(records, key=lambda item: item.name):
        dataset.seat_totals.append(
            turnout_data.SeatTotal(
                election_code=election.election_code,
                seat_name=record.name,
                source_id=report_source_id,
                enrolment=record.enrolment,
                formal_votes=record.formal,
                informal_votes=record.informal,
                total_ballots=record.total,
                subdivision='wa',
            )
        )
        record_categories = categories
        if polling_place_splits is not None:
            split = polling_place_splits[record.name]
            record_categories = (
                (
                    'Election-day polling places',
                    split.election_day,
                    'election_day_ordinary',
                    xml_source_id,
                    'sum_official_rows',
                ),
                (
                    'Early Polling Place',
                    split.early,
                    'early_in_person',
                    xml_source_id,
                    'sum_official_rows',
                ),
                (
                    'Mobile Polling',
                    split.mobile,
                    'mobile_or_institution',
                    xml_source_id,
                    'sum_official_rows',
                ),
                ('Absent', record.absent, 'absent', report_source_id, 'direct'),
                (
                    'Postal Votes', record.postal, 'postal',
                    report_source_id, 'direct',
                ),
                (
                    'Provisional', record.provisional, 'provisional',
                    report_source_id, 'direct',
                ),
            )
        else:
            record_categories = tuple(
                (
                    source_category,
                    getattr(record, field),
                    canonical_category,
                    report_source_id,
                    (
                        'difference_official_total'
                        if field == 'ordinary'
                        and election.layout == 'split-ordinary'
                        else 'direct'
                    ),
                )
                for source_category, field, canonical_category in categories
            )

        for (
            source_category,
            formal_votes,
            canonical_category,
            category_source_id,
            derivation,
        ) in record_categories:
            dataset.vote_types.append(
                turnout_data.VoteTypeRecord(
                    election_code=election.election_code,
                    seat_name=record.name,
                    source_id=category_source_id,
                    partition_id='final-formal-vote-types',
                    source_category=source_category,
                    canonical_category=canonical_category,
                    formal_votes=formal_votes,
                    informal_votes=None,
                    total_ballots=None,
                    coverage='complete',
                    derivation=derivation,
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
        categories.setdefault(record.canonical_category, 0)
        categories[record.canonical_category] += record.formal_votes
    return {
        'districts': len(dataset.seat_totals),
        'enrolment': enrolment,
        'total_ballots': ballots,
        'turnout_percent': ballots / enrolment * 100.0,
        'informal_percent': informal / ballots * 100.0,
        'category_formal_votes': categories,
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
    for category, count in sorted(summary['category_formal_votes'].items()):
        print('  {:24} {:>10,}'.format(category, count))
    print('  Wrote {}'.format(output_path))


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize final Western Australian turnout evidence.'
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
        print('Downloading final WAEC results report for {}...'.format(election_code))
        report_data = _download(election.report_url)
        table_text = extract_table_text(report_data, election)
        verbose_results_xml = None
        if election.verbose_results_url:
            print(
                'Downloading final WAEC verbose results for {}...'.format(
                    election_code
                )
            )
            verbose_results_xml = _download(election.verbose_results_url)
        dataset = build_dataset(election, table_text, verbose_results_xml)
        output_path = options.output_directory / '{}.json'.format(election_code)
        turnout_data.write_dataset_atomically(output_path, dataset)
        _print_summary(election_code, coverage_summary(dataset), output_path)
    return 0


if __name__ == '__main__':
    main()

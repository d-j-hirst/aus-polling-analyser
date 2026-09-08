"""Download and normalize final Queensland state-election turnout evidence.

This adapter reads ECQ's retained final XML media-feed archives. It does not
estimate turnout or retain candidate results: only district totals and the
final first-preference booth totals enter the normalized dataset.

Main functions:
* ``read_archive`` validates and extracts one ECQ ``publicResults.xml`` file.
* ``build_dataset`` selects the requested election and reconciles every seat.
* ``classify_booth`` maps ECQ vote types without inventing unavailable splits.
* ``coverage_summary`` reports category shares and reconciliation coverage.
* ``main`` provides the ``--election`` and output-directory command line flow.
"""

import argparse
from collections import defaultdict
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET
from zipfile import BadZipFile, ZipFile

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 60


@dataclass(frozen=True)
class QldElection:
    election_code: str
    election_date: str
    archive_url: str
    election_name: str
    expected_districts: int
    feed_version: str


_HISTORICAL_ARCHIVE = (
    'https://results.ecq.qld.gov.au/elections/state/{}/results/public.zip'
)
ELECTIONS = {
    '2006qld': QldElection(
        '2006qld',
        '2006-09-09',
        _HISTORICAL_ARCHIVE.format('state2006'),
        '2006 State General Election',
        89,
        'legacy',
    ),
    '2009qld': QldElection(
        '2009qld',
        '2009-03-21',
        _HISTORICAL_ARCHIVE.format('state2009'),
        '2009 State General Election',
        89,
        'legacy',
    ),
    '2012qld': QldElection(
        '2012qld',
        '2012-03-24',
        _HISTORICAL_ARCHIVE.format('State2012'),
        '2012 State General Election',
        89,
        'legacy',
    ),
    '2015qld': QldElection(
        '2015qld',
        '2015-01-31',
        _HISTORICAL_ARCHIVE.format('State2015'),
        '2015 State General Election',
        89,
        'legacy',
    ),
    '2017qld': QldElection(
        '2017qld',
        '2017-11-25',
        _HISTORICAL_ARCHIVE.format('State2017'),
        '2017 State General Election',
        93,
        'legacy',
    ),
    '2020qld': QldElection(
        '2020qld',
        '2020-10-31',
        (
            'https://resultsdata.elections.qld.gov.au/XMLData/'
            'publicResults_State2020_aurukun2020_Final.zip'
        ),
        '2020 State General Election',
        93,
        'current',
    ),
    '2024qld': QldElection(
        '2024qld',
        '2024-10-26',
        (
            'https://resultsdata.elections.qld.gov.au/XMLData/'
            'publicResults_SGE2024_ICCDiv4_Final.zip'
        ),
        '2024 State General Election',
        93,
        'current',
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


def read_archive(archive_data, label):
    """Return the sole public-results XML member from an ECQ archive."""
    try:
        with ZipFile(BytesIO(archive_data)) as archive:
            members = [
                member for member in archive.infolist()
                if not member.is_dir()
                and Path(member.filename).name.lower() == 'publicresults.xml'
            ]
            if len(members) != 1:
                raise turnout_data.TurnoutDataError(
                    '{} contains {} publicResults.xml files, expected 1'.format(
                        label, len(members)
                    )
                )
            return archive.read(members[0])
    except BadZipFile as error:
        raise turnout_data.TurnoutDataError(
            '{} is not a valid ZIP archive: {}'.format(label, error)
        )


def _parse_xml(xml_data, label):
    try:
        return ET.fromstring(xml_data)
    except ET.ParseError as error:
        raise turnout_data.TurnoutDataError(
            '{} contains invalid XML: {}'.format(label, error)
        )


def _count(value, label):
    try:
        count = int(value)
    except (TypeError, ValueError):
        raise turnout_data.TurnoutDataError(
            '{} has invalid count {!r}'.format(label, value)
        )
    if count < 0:
        raise turnout_data.TurnoutDataError(
            '{} has negative count'.format(label)
        )
    return count


def _required_text(node, path, label):
    child = node.find(path)
    if child is None or child.text is None or not child.text.strip():
        raise turnout_data.TurnoutDataError(
            '{} is missing {}'.format(label, path)
        )
    return child.text.strip()


def _select_election_root(election, root):
    if election.feed_version == 'legacy':
        if root.tag != 'election':
            raise turnout_data.TurnoutDataError(
                '{} expected a legacy election root, found {!r}'.format(
                    election.election_code, root.tag
                )
            )
        name = root.get('name')
        election_date = root.get('date')
        selected = root
    else:
        if root.tag != 'ecq':
            raise turnout_data.TurnoutDataError(
                '{} expected an ECQ feed root, found {!r}'.format(
                    election.election_code, root.tag
                )
            )
        matching = [
            node for node in root.findall('./election')
            if node.get('electionName') == election.election_name
        ]
        if len(matching) != 1:
            raise turnout_data.TurnoutDataError(
                '{} contains {} matching elections, expected 1'.format(
                    election.election_code, len(matching)
                )
            )
        selected = matching[0]
        name = selected.get('electionName')
        election_date = selected.get('electionDay')
    if name != election.election_name or election_date != election.election_date:
        raise turnout_data.TurnoutDataError(
            '{} archive identifies {!r} on {!r}'.format(
                election.election_code, name, election_date
            )
        )
    return selected


def _district_nodes(election, election_root):
    districts = election_root.findall('./districts/district')
    if len(districts) != election.expected_districts:
        raise turnout_data.TurnoutDataError(
            '{} contains {} districts, expected {}'.format(
                election.election_code,
                len(districts),
                election.expected_districts,
            )
        )
    return districts


def _legacy_district(district):
    name = district.get('name')
    enrolment = _count(district.get('enrolment'), '{} enrolment'.format(name))
    formal = _count(
        _required_text(district, './formalVotes/count', name),
        '{} formal'.format(name),
    )
    informal = _count(
        _required_text(district, './informalVotes/count', name),
        '{} informal'.format(name),
    )
    total = _count(
        _required_text(district, './totalBallots', name),
        '{} total'.format(name),
    )
    return name, district.get('number', ''), enrolment, formal, informal, total, (
        district.findall('./booths/booth')
    )


def _current_district(district):
    name = district.get('districtName')
    rounds = [
        node for node in district.findall('./countRound')
        if node.get('id') == '3'
        and node.get('countName') == 'Official First Preference Count'
        and node.get('unofficial') == 'NO'
        and node.get('preferences') == 'NO'
    ]
    if len(rounds) != 1:
        raise turnout_data.TurnoutDataError(
            '{} has {} final first-preference rounds, expected 1'.format(
                name, len(rounds)
            )
        )
    final_round = rounds[0]
    enrolment = _count(district.get('enrolment'), '{} enrolment'.format(name))
    formal = _count(
        _required_text(final_round, './totalFormalVotes/count', name),
        '{} formal'.format(name),
    )
    informal = _count(
        _required_text(final_round, './totalInformalVotes/count', name),
        '{} informal'.format(name),
    )
    total = _count(
        _required_text(final_round, './totalVotes', name),
        '{} total'.format(name),
    )
    return name, district.get('number', ''), enrolment, formal, informal, total, (
        final_round.findall('./booths/booth')
    )


def _booth_counts(booth, district_name):
    booth_name = booth.get('name') or '<unnamed>'
    label = '{} {}'.format(district_name, booth_name)
    formal = _count(
        _required_text(booth, './formalVotes', label),
        '{} formal'.format(label),
    )
    informal = _count(
        _required_text(booth, './informalVotes', label),
        '{} informal'.format(label),
    )
    total = _count(
        _required_text(booth, './ballots', label),
        '{} total'.format(label),
    )
    if formal + informal != total:
        raise turnout_data.TurnoutDataError(
            '{} formal and informal counts do not equal ballots'.format(label)
        )
    return formal, informal, total


def _derived_category(type_code, type_description, qualifier):
    return '{} {} ({})'.format(type_code, type_description, qualifier)


def classify_booth(booth, feed_version):
    """Return a transparent source category and normalized category."""
    type_code = booth.get('typeCode')
    description = booth.get('typeDescription')
    name = booth.get('name', '')
    lowered_name = name.casefold()

    if type_code == 'PB':
        if 'telephone' in lowered_name:
            return (
                _derived_category(type_code, description, 'telephone'),
                'telephone',
            )
        if 'eassist' in lowered_name:
            return (
                _derived_category(type_code, description, 'electronically assisted'),
                'remote_electronic',
            )
        if 'mobile team' in lowered_name:
            return (
                _derived_category(type_code, description, 'mobile team'),
                'mobile_or_institution',
            )
        if 'pre-poll' in lowered_name:
            return (
                _derived_category(type_code, description, 'pre-poll venue'),
                'early_in_person',
            )
        return (
            _derived_category(type_code, description, 'election-day venue'),
            'election_day_ordinary',
        )

    if type_code == 'EV' and feed_version == 'current':
        if 'telephone' in lowered_name:
            return _derived_category(type_code, description, 'telephone'), 'telephone'
        if 'mobile' in lowered_name:
            return (
                _derived_category(type_code, description, 'mobile polling'),
                'mobile_or_institution',
            )
        return type_code + ' ' + description, 'early_in_person'

    mappings = {
        'PO': 'postal',
        'PA': 'absent',
        'DI': 'mobile_or_institution',
        'EV': 'mobile_or_institution',
        'PR': 'enrolment',
        'UI': 'provisional',
        'DV1': 'postal',
        'DV2': 'declaration_combined',
        'AB1': 'absent',
        'AB2': 'declaration_early',
    }
    if type_code == 'PP':
        canonical = (
            'declaration_early'
            if 'absent' in (description or '').casefold()
            else 'early_in_person'
        )
    else:
        canonical = mappings.get(type_code)
    if canonical is None or not description:
        raise turnout_data.TurnoutDataError(
            'unsupported ECQ booth type {!r} {!r} for {!r}'.format(
                type_code, description, name
            )
        )
    return '{} {}'.format(type_code, description), canonical


def _parse_district(election, district):
    values = (
        _legacy_district(district)
        if election.feed_version == 'legacy'
        else _current_district(district)
    )
    name, source_id, enrolment, formal, informal, total, booths = values
    if not name:
        raise turnout_data.TurnoutDataError(
            '{} contains a district without a name'.format(election.election_code)
        )
    if formal + informal != total:
        raise turnout_data.TurnoutDataError(
            '{} published formal and informal totals do not reconcile'.format(name)
        )
    if total > enrolment:
        raise turnout_data.TurnoutDataError(
            '{} ballots exceed enrolment'.format(name)
        )

    categories = defaultdict(lambda: [0, 0, 0, None])
    seen_booth_ids = set()
    for booth in booths:
        booth_identity = (booth.get('id'), booth.get('name'))
        if booth_identity in seen_booth_ids:
            raise turnout_data.TurnoutDataError(
                '{} repeats booth {}'.format(name, booth_identity)
            )
        seen_booth_ids.add(booth_identity)
        source_category, canonical = classify_booth(booth, election.feed_version)
        counts = _booth_counts(booth, name)
        category = categories[source_category]
        for index, count in enumerate(counts):
            category[index] += count
        if category[3] not in (None, canonical):
            raise turnout_data.TurnoutDataError(
                '{} maps inconsistently'.format(source_category)
            )
        category[3] = canonical

    category_total = tuple(
        sum(counts[index] for counts in categories.values())
        for index in range(3)
    )
    if category_total != (formal, informal, total):
        raise turnout_data.TurnoutDataError(
            '{} booth totals {} differ from published {}'.format(
                name, category_total, (formal, informal, total)
            )
        )
    return name, source_id, enrolment, formal, informal, total, categories


def build_dataset(election, xml_data):
    """Convert and fully reconcile one final ECQ XML archive."""
    root = _parse_xml(xml_data, election.election_code)
    election_root = _select_election_root(election, root)
    parsed = [
        _parse_district(election, district)
        for district in _district_nodes(election, election_root)
    ]
    district_names = [record[0] for record in parsed]
    if len(set(district_names)) != len(district_names):
        raise turnout_data.TurnoutDataError(
            '{} contains duplicate district names'.format(election.election_code)
        )

    source_id = 'ecq-{}-state-final'.format(election.election_code[:4])
    dataset = turnout_data.TurnoutDataset(
        elections=[
            turnout_data.ElectionDefinition(
                election.election_code, election.election_date, 'qld'
            )
        ],
        sources=[
            turnout_data.SourceDefinition(
                source_id=source_id,
                election_code=election.election_code,
                authority='Electoral Commission of Queensland',
                locator=election.archive_url,
                adapter='ecq-final-xml-v1',
                status='final',
                category_regime='ecq-{}-vote-types-v1'.format(
                    election.feed_version
                ),
                notes=(
                    'ECQ booth type codes and explicit venue labels determine '
                    'the normalized category. Detailed source categories are '
                    'retained and no unavailable category split is estimated.'
                ),
            )
        ],
    )
    for record in sorted(parsed):
        (
            name,
            source_seat_id,
            enrolment,
            formal,
            informal,
            total,
            categories,
        ) = record
        dataset.seat_totals.append(
            turnout_data.SeatTotal(
                election_code=election.election_code,
                seat_name=name,
                source_id=source_id,
                enrolment=enrolment,
                formal_votes=formal,
                informal_votes=informal,
                total_ballots=total,
                source_seat_id=source_seat_id,
                subdivision='qld',
            )
        )
        for source_category, counts in sorted(categories.items()):
            dataset.vote_types.append(
                turnout_data.VoteTypeRecord(
                    election_code=election.election_code,
                    seat_name=name,
                    source_id=source_id,
                    partition_id='final-ballot-vote-types',
                    source_category=source_category,
                    canonical_category=counts[3],
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
        'enrolment': enrolment,
        'total_ballots': ballots,
        'turnout_percent': ballots / enrolment * 100.0,
        'informal_percent': informal / ballots * 100.0,
        'category_ballots': dict(categories),
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
    for category, ballots in sorted(summary['category_ballots'].items()):
        print(
            '  {:24} {:>10,} ({:5.2f}%)'.format(
                category, ballots, ballots / summary['total_ballots'] * 100.0
            )
        )
    print('  Wrote {}'.format(output_path))


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize final Queensland turnout evidence.'
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
        print('Downloading final ECQ turnout archive for {}...'.format(election_code))
        archive_data = _download(election.archive_url)
        xml_data = read_archive(archive_data, election.archive_url)
        dataset = build_dataset(election, xml_data)
        output_path = options.output_directory / '{}.json'.format(election_code)
        turnout_data.write_dataset_atomically(output_path, dataset)
        _print_summary(election_code, coverage_summary(dataset), output_path)
    return 0


if __name__ == '__main__':
    main()

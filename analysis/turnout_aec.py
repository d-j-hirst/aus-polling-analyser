"""Download and normalize final AEC House turnout evidence.

The AEC adapter performs acquisition and source-specific interpretation only;
it does not estimate turnout. It reconciles candidate, informal, enrolment and
division vote files before atomically publishing one normalized election file.

Main functions:
* ``download_election_files`` obtains the available final AEC CSV inputs.
* ``build_dataset`` performs the actual AEC-to-normalized-data conversion.
* ``coverage_summary`` reports category shares and reconciliation coverage.
* ``main`` provides the ``--election`` and output-directory command line flow.
"""

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
import io
from pathlib import Path
import re
from urllib.request import Request, urlopen

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 30

VOTE_COLUMNS = (
    'OrdinaryVotes',
    'AbsentVotes',
    'ProvisionalVotes',
    'PrePollVotes',
    'PostalVotes',
)

BASE_CANONICAL_CATEGORIES = {
    'AbsentVotes': 'absent',
    'ProvisionalVotes': 'provisional',
    'PrePollVotes': 'declaration_early',
    'PostalVotes': 'postal',
}

FILE_KINDS = {
    'candidate_vote_types': 'HouseFirstPrefsByCandidateByVoteTypeDownload',
    'enrolment': 'GeneralEnrolmentByDivisionDownload',
    'informal': 'HouseInformalByDivisionDownload',
    'division_votes': 'HouseVotesCountedByDivisionDownload',
}

REQUIRED_HEADERS = {
    'candidate_vote_types': {
        'StateAb', 'DivisionID', 'DivisionNm', 'CandidateID', 'Surname',
        'PartyNm', 'TotalVotes', *VOTE_COLUMNS,
    },
    'enrolment': {'DivisionID', 'DivisionNm', 'StateAb', 'Enrolment'},
    'informal': {
        'DivisionID', 'DivisionNm', 'StateAb', 'FormalVotes',
        'InformalVotes', 'TotalVotes',
    },
    'division_votes': {
        'DivisionID', 'DivisionNm', 'StateAb', 'Enrolment', 'TotalVotes',
        *VOTE_COLUMNS,
    },
}

_METADATA_EVENT_PATTERN = re.compile(r'\bEvent:([0-9]+)\b')


@dataclass(frozen=True)
class AecElection:
    election_code: str
    election_date: str
    event_id: int
    ordinary_category: str = 'ordinary_combined'
    download_path: str = 'Website/Downloads'
    has_enrolment_file: bool = True
    legacy_metadata: bool = False


ELECTIONS = {
    '2004fed': AecElection(
        '2004fed', '2004-10-09', 12246,
        ordinary_category='election_day_ordinary',
        download_path='results/Downloads',
        has_enrolment_file=False,
        legacy_metadata=True,
    ),
    '2007fed': AecElection(
        '2007fed', '2007-11-24', 13745,
        ordinary_category='election_day_ordinary',
    ),
    '2010fed': AecElection('2010fed', '2010-08-21', 15508),
    '2013fed': AecElection('2013fed', '2013-09-07', 17496),
    '2016fed': AecElection('2016fed', '2016-07-02', 20499),
    '2019fed': AecElection('2019fed', '2019-05-18', 24310),
    '2022fed': AecElection('2022fed', '2022-05-21', 27966),
    '2025fed': AecElection('2025fed', '2025-05-03', 31496),
}


def _file_url(election, file_kind):
    stem = FILE_KINDS[file_kind]
    return (
        'https://results.aec.gov.au/{event}/{path}/'
        '{stem}-{event}.csv'
    ).format(event=election.event_id, path=election.download_path, stem=stem)


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


def download_election_files(election):
    """Return all official CSV inputs required for one election."""
    file_kinds = tuple(FILE_KINDS)
    if not election.has_enrolment_file:
        file_kinds = tuple(kind for kind in file_kinds if kind != 'enrolment')
    return {
        file_kind: _download(_file_url(election, file_kind))
        for file_kind in file_kinds
    }


def _parse_csv(data, file_kind, election):
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} is not valid UTF-8: {}'.format(file_kind, error)
        )
    lines = text.splitlines()
    if len(lines) < 2:
        raise turnout_data.TurnoutDataError('{} is empty'.format(file_kind))

    metadata = lines[0]
    if election.legacy_metadata:
        event_marker = '[{} '.format(election.event_id)
        if (
            not metadata.startswith('{} Federal Election'.format(
                election.election_code[:4]
            ))
            or event_marker not in metadata
        ):
            raise turnout_data.TurnoutDataError(
                '{} metadata does not identify the {} AEC event'.format(
                    file_kind, election.election_code[:4]
                )
            )
    else:
        event_match = _METADATA_EVENT_PATTERN.search(metadata)
        if event_match is None or int(event_match.group(1)) != election.event_id:
            raise turnout_data.TurnoutDataError(
                '{} metadata does not identify AEC event {}'.format(
                    file_kind, election.event_id
                )
            )
        if 'Phase:FinalResults' not in metadata:
            raise turnout_data.TurnoutDataError(
                '{} is not a final-results file'.format(file_kind)
            )

    reader = csv.DictReader(io.StringIO('\n'.join(lines[1:])))
    headers = set(reader.fieldnames or ())
    missing = REQUIRED_HEADERS[file_kind] - headers
    if missing:
        raise turnout_data.TurnoutDataError(
            '{} is missing required columns: {}'.format(
                file_kind, ', '.join(sorted(missing))
            )
        )
    rows = list(reader)
    if not rows:
        raise turnout_data.TurnoutDataError(
            '{} contains no result rows'.format(file_kind)
        )
    return rows


def _count(row, column, label):
    value = row.get(column, '')
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        raise turnout_data.TurnoutDataError(
            '{} has invalid {} {!r}'.format(label, column, value)
        )
    if parsed < 0:
        raise turnout_data.TurnoutDataError(
            '{} has negative {}'.format(label, column)
        )
    return parsed


def _division_identity(row, label):
    division_id = row.get('DivisionID', '').strip()
    division_name = row.get('DivisionNm', '').strip()
    state = row.get('StateAb', '').strip()
    if not division_id or not division_name or not state:
        raise turnout_data.TurnoutDataError(
            '{} has an incomplete division identity'.format(label)
        )
    return division_id, division_name, state


def _index_divisions(rows, file_kind):
    indexed = {}
    for row_number, row in enumerate(rows, start=3):
        identity = _division_identity(row, '{} row {}'.format(file_kind, row_number))
        division_id = identity[0]
        if division_id in indexed:
            raise turnout_data.TurnoutDataError(
                '{} repeats division {}'.format(file_kind, division_id)
            )
        indexed[division_id] = (identity, row)
    return indexed


def _candidate_vote_totals(rows):
    formal = defaultdict(lambda: defaultdict(int))
    informal = defaultdict(lambda: defaultdict(int))
    identities = {}
    informal_rows = defaultdict(int)

    for row_number, row in enumerate(rows, start=3):
        label = 'candidate_vote_types row {}'.format(row_number)
        identity = _division_identity(row, label)
        division_id = identity[0]
        previous_identity = identities.setdefault(division_id, identity)
        if previous_identity != identity:
            raise turnout_data.TurnoutDataError(
                'candidate file gives conflicting identities for division {}'.format(
                    division_id
                )
            )

        category_counts = {
            column: _count(row, column, label) for column in VOTE_COLUMNS
        }
        if sum(category_counts.values()) != _count(row, 'TotalVotes', label):
            raise turnout_data.TurnoutDataError(
                '{} vote types do not equal candidate total'.format(label)
            )

        is_informal = (
            row.get('CandidateID', '').strip() == '999'
            and row.get('Surname', '').strip() == 'Informal'
            and row.get('PartyNm', '').strip() == 'Informal'
        )
        destination = informal if is_informal else formal
        if is_informal:
            informal_rows[division_id] += 1
        for column, count in category_counts.items():
            destination[division_id][column] += count

    for division_id in identities:
        if informal_rows[division_id] != 1:
            raise turnout_data.TurnoutDataError(
                'division {} has {} informal pseudo-candidate rows, expected 1'.format(
                    division_id, informal_rows[division_id]
                )
            )
    return identities, formal, informal


def build_dataset(election, downloaded_files):
    """Convert and fully reconcile one set of final AEC files."""
    required_file_kinds = set(FILE_KINDS)
    if not election.has_enrolment_file:
        required_file_kinds.remove('enrolment')
    missing_files = required_file_kinds - set(downloaded_files)
    if missing_files:
        raise turnout_data.TurnoutDataError(
            'missing AEC input files: {}'.format(', '.join(sorted(missing_files)))
        )
    rows = {
        file_kind: _parse_csv(downloaded_files[file_kind], file_kind, election)
        for file_kind in required_file_kinds
    }

    candidate_identities, formal_by_type, informal_by_type = (
        _candidate_vote_totals(rows['candidate_vote_types'])
    )
    enrolment = (
        _index_divisions(rows['enrolment'], 'enrolment')
        if election.has_enrolment_file else None
    )
    informal = _index_divisions(rows['informal'], 'informal')
    division_votes = _index_divisions(rows['division_votes'], 'division_votes')

    division_sets = {
        'candidate_vote_types': set(candidate_identities),
        'informal': set(informal),
        'division_votes': set(division_votes),
    }
    if enrolment is not None:
        division_sets['enrolment'] = set(enrolment)
    expected_divisions = division_sets['candidate_vote_types']
    for file_kind, divisions in division_sets.items():
        if divisions != expected_divisions:
            missing = expected_divisions - divisions
            extra = divisions - expected_divisions
            raise turnout_data.TurnoutDataError(
                '{} division coverage differs; missing={}, extra={}'.format(
                    file_kind, sorted(missing), sorted(extra)
                )
            )

    source_id = 'aec-{}-final'.format(election.event_id)
    if election.ordinary_category == 'election_day_ordinary':
        category_regime = 'aec-prepoll-declaration-v1'
        category_notes = (
            'OrdinaryVotes contains election-day ordinary votes; '
            'PrePollVotes is declaration pre-poll.'
        )
    else:
        category_regime = 'aec-ordinary-prepoll-v1'
        category_notes = (
            'OrdinaryVotes combines election-day and own-division early '
            'ordinary votes; PrePollVotes is declaration pre-poll.'
        )
    dataset = turnout_data.TurnoutDataset(
        elections=[
            turnout_data.ElectionDefinition(
                election.election_code,
                election.election_date,
                'fed',
            )
        ],
        sources=[
            turnout_data.SourceDefinition(
                source_id=source_id,
                election_code=election.election_code,
                authority='Australian Electoral Commission',
                locator='https://results.aec.gov.au/{event}/{path}/'.format(
                    event=election.event_id,
                    path=election.download_path,
                ),
                adapter='aec-final-vote-types-v1',
                status='final',
                category_regime=category_regime,
                notes=category_notes,
            )
        ],
    )

    for division_id in sorted(expected_divisions, key=int):
        identity = candidate_identities[division_id]
        identity_rows = [informal[division_id], division_votes[division_id]]
        if enrolment is not None:
            identity_rows.append(enrolment[division_id])
        for indexed_identity, _row in identity_rows:
            if indexed_identity != identity:
                raise turnout_data.TurnoutDataError(
                    'division {} identity differs between AEC files'.format(
                        division_id
                    )
                )

        _division_id, division_name, _state = identity
        informal_row = informal[division_id][1]
        votes_row = division_votes[division_id][1]
        final_enrolment = _count(votes_row, 'Enrolment', division_name)
        if enrolment is not None:
            enrolment_row = enrolment[division_id][1]
            if _count(enrolment_row, 'Enrolment', division_name) != final_enrolment:
                raise turnout_data.TurnoutDataError(
                    '{} enrolment differs between AEC files'.format(division_name)
                )

        formal_total = sum(formal_by_type[division_id].values())
        informal_total = sum(informal_by_type[division_id].values())
        total_ballots = formal_total + informal_total
        expected_counts = {
            'FormalVotes': formal_total,
            'InformalVotes': informal_total,
            'TotalVotes': total_ballots,
        }
        for column, expected in expected_counts.items():
            if _count(informal_row, column, division_name) != expected:
                raise turnout_data.TurnoutDataError(
                    '{} {} differs from candidate vote types'.format(
                        division_name, column
                    )
                )
        if _count(votes_row, 'TotalVotes', division_name) != total_ballots:
            raise turnout_data.TurnoutDataError(
                '{} total differs between AEC files'.format(division_name)
            )

        dataset.seat_totals.append(
            turnout_data.SeatTotal(
                election_code=election.election_code,
                seat_name=division_name,
                source_id=source_id,
                enrolment=final_enrolment,
                formal_votes=formal_total,
                informal_votes=informal_total,
                total_ballots=total_ballots,
                source_seat_id=division_id,
                subdivision=_state.lower(),
            )
        )
        for column in VOTE_COLUMNS:
            formal_count = formal_by_type[division_id][column]
            informal_count = informal_by_type[division_id][column]
            total_count = _count(votes_row, column, division_name)
            if formal_count + informal_count != total_count:
                raise turnout_data.TurnoutDataError(
                    '{} {} differs between AEC files'.format(
                        division_name, column
                    )
                )
            dataset.vote_types.append(
                turnout_data.VoteTypeRecord(
                    election_code=election.election_code,
                    seat_name=division_name,
                    source_id=source_id,
                    partition_id='final-formal-vote-types',
                    source_category=column,
                    canonical_category=(
                        election.ordinary_category
                        if column == 'OrdinaryVotes'
                        else BASE_CANONICAL_CATEGORIES[column]
                    ),
                    formal_votes=formal_count,
                    informal_votes=informal_count,
                    total_ballots=total_count,
                    coverage='complete',
                    derivation='sum_official_rows',
                )
            )

    dataset.validate()
    return dataset


def coverage_summary(dataset):
    """Return compact election-wide coverage totals for inspection."""
    enrolment = sum(record.enrolment for record in dataset.seat_totals)
    formal = sum(record.formal_votes for record in dataset.seat_totals)
    informal = sum(record.informal_votes for record in dataset.seat_totals)
    ballots = sum(record.total_ballots for record in dataset.seat_totals)
    categories = defaultdict(int)
    for record in dataset.vote_types:
        categories[record.source_category] += record.total_ballots
    return {
        'divisions': len(dataset.seat_totals),
        'enrolment': enrolment,
        'formal_votes': formal,
        'informal_votes': informal,
        'total_ballots': ballots,
        'turnout_percent': ballots / enrolment * 100.0,
        'informal_percent': informal / ballots * 100.0,
        'category_ballots': dict(categories),
    }


def _print_summary(election_code, summary, output_path):
    print(
        '{}: {} divisions; {:,} ballots from {:,} enrolled '
        '({:.2f}% turnout, {:.2f}% informal)'.format(
            election_code,
            summary['divisions'],
            summary['total_ballots'],
            summary['enrolment'],
            summary['turnout_percent'],
            summary['informal_percent'],
        )
    )
    for column in VOTE_COLUMNS:
        ballots = summary['category_ballots'][column]
        print(
            '  {:18} {:>10,} ({:5.2f}%)'.format(
                column,
                ballots,
                ballots / summary['total_ballots'] * 100.0,
            )
        )
    print('  Wrote {}'.format(output_path))


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Download and normalize final AEC turnout evidence.'
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
        print('Downloading final AEC turnout files for {}...'.format(election_code))
        downloaded_files = download_election_files(election)
        dataset = build_dataset(election, downloaded_files)
        output_path = options.output_directory / '{}.json'.format(election_code)
        turnout_data.write_dataset_atomically(output_path, dataset)
        _print_summary(election_code, coverage_summary(dataset), output_path)
    return 0


if __name__ == '__main__':
    main()

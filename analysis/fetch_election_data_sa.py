"""Convert archived ECSA 2022 booth CSVs into live Booth Results JSON.

Unlike the VIC/NSW/QLD fetchers this script does not scrape a website. The
2022 House of Assembly first-preference and two-candidate tables are already
concatenated under ``downloads/``. The output schema matches those fetchers so
``LivePreparation`` can load ``Booth Results/2022sa.json``.

Main functions:
* ``parse_candidate_label`` splits ``SURNAME, Given (PARTY)`` headers.
* ``convert_election`` reads the FP then TCP CSVs into the shared JSON schema.
* ``write_booth_results`` dumps that mapping with the same ``indent=4`` style
  as the other jurisdiction scripts.
* ``main`` converts the configured election and writes ``Booth Results``.
"""

import argparse
import csv
import io
import json
import re
from pathlib import Path


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DOWNLOADS_DIRECTORY = ANALYSIS_DIRECTORY.parent / 'downloads'
BOOTH_RESULTS_DIRECTORY = ANALYSIS_DIRECTORY / 'Booth Results'

election = '2022sa'

sources = {
    '2022sa': {
        'fp': '2026sa_prev_fp.csv',
        'tcp': '2026sa_prev_tcp.csv',
    },
}

skip_booths = [
    'Ordinary Votes',
    'District Total',
    'Polling Place Totals',
]

tcp_booth_renames = {
    'Declaration Ballot Papers': 'Declaration Votes',
}

FILE_MARKER = re.compile(r'^### FILE: (.+?)\.csv ###\s*$', re.MULTILINE)
CANDIDATE_LABEL = re.compile(r'^(.*) \(([A-Z]+)\)$')
# Hammond's FP export uses the TCP-style "Polling Location" header.
FP_TABLE_HEADERS = {'Polling Place', 'Polling Location'}
FP_CANDIDATE_STOP = {'Formal Votes', 'Informal Votes', 'Total Votes'}
TCP_HEADER_SKIP = {'Polling Location', 'Percentage', 'Total', 'Total Votes'}


class SaBoothResultsError(ValueError):
    """Raised when an ECSA booth CSV cannot be converted."""


def parse_candidate_label(label):
    match = CANDIDATE_LABEL.fullmatch(label.strip())
    if not match:
        raise SaBoothResultsError(f'Could not parse candidate label: {label!r}')
    return {'name': match.group(1), 'party': match.group(2)}


def parse_vote_count(cell):
    text = cell.strip().replace(',', '')
    if not text:
        raise SaBoothResultsError('Empty vote count')
    return int(text)


def replace_booth_name(booth_name):
    return tcp_booth_renames.get(booth_name, booth_name)


def iter_district_chunks(text):
    parts = FILE_MARKER.split(text)
    if len(parts) < 3:
        raise SaBoothResultsError('No district FILE markers were found')
    for index in range(1, len(parts), 2):
        yield parts[index], parts[index + 1]


def read_csv_rows(chunk):
    return list(csv.reader(io.StringIO(chunk)))


def first_cell(row):
    return row[0].strip() if row else ''


def load_concatenated_csv(path):
    return Path(path).read_text(encoding='utf-8-sig')


def fp_candidate_labels(header_row):
    labels = []
    for cell in header_row[1:]:
        name = cell.strip()
        if not name:
            continue
        if name in FP_CANDIDATE_STOP:
            break
        labels.append(name)
    if not labels:
        raise SaBoothResultsError('No first-preference candidates were found')
    return labels


def tcp_candidate_labels(header_row):
    labels = [
        cell.strip() for cell in header_row[1:]
        if cell.strip() and cell.strip() not in TCP_HEADER_SKIP
    ]
    if len(labels) != 2:
        raise SaBoothResultsError(
            f'Expected two TCP candidates, found {labels!r}')
    return labels


def candidate_index_by_label(candidates, label):
    parsed = parse_candidate_label(label)
    for index, candidate in candidates.items():
        if candidate['name'] == parsed['name'] and candidate['party'] == parsed['party']:
            return index
    raise SaBoothResultsError(
        f'Could not match TCP candidate {label!r} to first-preference candidates')


def get_fps(fp_path):
    all_results = {}
    for seat_name, chunk in iter_district_chunks(load_concatenated_csv(fp_path)):
        rows = read_csv_rows(chunk)
        header_index = next(
            (index for index, row in enumerate(rows)
             if first_cell(row) in FP_TABLE_HEADERS),
            None,
        )
        if header_index is None or header_index == 0:
            raise SaBoothResultsError(
                f'No first-preference polling-place header for {seat_name}')
        labels = fp_candidate_labels(rows[header_index - 1])
        candidates = {
            index: parse_candidate_label(label)
            for index, label in enumerate(labels)
        }
        booths = {}
        for row in rows[header_index + 1:]:
            booth_name = first_cell(row)
            if not booth_name or booth_name in skip_booths:
                continue
            booth_name = replace_booth_name(booth_name)
            vote_cells = row[1::2][:len(candidates)]
            if len(vote_cells) < len(candidates):
                raise SaBoothResultsError(
                    f'{seat_name} booth {booth_name!r} has too few vote columns')
            booths[booth_name] = {
                'fp': {
                    index: parse_vote_count(cell)
                    for index, cell in enumerate(vote_cells)
                },
                'tcp': {},
            }
        all_results[seat_name] = {
            'candidates': candidates,
            'booths': booths,
        }
    return all_results


def add_tcps(tcp_path, all_results):
    seen_seats = set()
    for seat_name, chunk in iter_district_chunks(load_concatenated_csv(tcp_path)):
        if seat_name not in all_results:
            raise SaBoothResultsError(
                f'TCP district {seat_name!r} has no first-preference results')
        seen_seats.add(seat_name)
        rows = read_csv_rows(chunk)
        header_index = next(
            (index for index, row in enumerate(rows) if first_cell(row) == 'Polling Location'),
            None,
        )
        if header_index is None:
            raise SaBoothResultsError(
                f'No TCP polling-location header for {seat_name}')
        candidates = all_results[seat_name]['candidates']
        tcp_indices = [
            candidate_index_by_label(candidates, label)
            for label in tcp_candidate_labels(rows[header_index])
        ]
        booths = all_results[seat_name]['booths']
        for row in rows[header_index + 1:]:
            booth_name = first_cell(row)
            if not booth_name or booth_name in skip_booths:
                continue
            booth_name = replace_booth_name(booth_name)
            if booth_name not in booths:
                raise SaBoothResultsError(
                    f'TCP booth {booth_name!r} in {seat_name} was not in the FP table')
            vote_cells = row[1::2][:2]
            if len(vote_cells) < 2:
                raise SaBoothResultsError(
                    f'{seat_name} TCP booth {booth_name!r} has too few vote columns')
            booths[booth_name]['tcp'] = {
                tcp_indices[index]: parse_vote_count(cell)
                for index, cell in enumerate(vote_cells)
            }
    missing_seats = [name for name in all_results if name not in seen_seats]
    if missing_seats:
        raise SaBoothResultsError(
            f'TCP table missing districts: {", ".join(missing_seats)}')
    for seat_name, seat_info in all_results.items():
        for booth_name, booth in seat_info['booths'].items():
            if len(booth['tcp']) != 2:
                raise SaBoothResultsError(
                    f'{seat_name} booth {booth_name!r} is missing TCP votes')
    return all_results


def source_paths(election_code, fp_path=None, tcp_path=None):
    if election_code not in sources:
        raise SaBoothResultsError(f'Unknown election {election_code!r}')
    configured = sources[election_code]
    return (
        Path(fp_path) if fp_path is not None
        else DOWNLOADS_DIRECTORY / configured['fp'],
        Path(tcp_path) if tcp_path is not None
        else DOWNLOADS_DIRECTORY / configured['tcp'],
    )


def convert_election(election_code=election, fp_path=None, tcp_path=None):
    fp_source, tcp_source = source_paths(election_code, fp_path, tcp_path)
    all_results = get_fps(fp_source)
    return add_tcps(tcp_source, all_results)


def booth_results_path(election_code=election):
    return BOOTH_RESULTS_DIRECTORY / f'{election_code}.json'


def write_booth_results(all_results, output_path):
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open('w', encoding='utf-8', newline='\n') as file:
        json.dump(all_results, file, indent=4)
        file.write('\n')


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Convert archived ECSA booth CSVs to live-results JSON.')
    parser.add_argument('--election', choices=sorted(sources), default=election)
    args = parser.parse_args(argv)
    all_results = convert_election(args.election)
    output_path = booth_results_path(args.election)
    write_booth_results(all_results, output_path)
    print(output_path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

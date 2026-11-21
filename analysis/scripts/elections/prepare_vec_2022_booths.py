"""Build the previous booth and declaration counts used by the 2026 Victorian forecast.

Each first-preference column represents one candidate. The two-candidate count
has its own columns and can represent an earlier stage of counting. Reconcile
each count with its own published candidate totals before installing the file.
Run from analysis with python -m scripts.elections.prepare_vec_2022_booths.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import tempfile

from lib.paths import REPOSITORY_DIRECTORY
from scripts.turnout import turnout_vic as vec


SUPPLEMENT = ('https://www.vec.vic.gov.au/results/state-election-results/'
              'state-by-elections-timeline/narracan-district-supplementary-election-results')
PRIMARY = ('https://itsitecoreblobvecprd01.blob.core.windows.net/public-files/'
           'State/Reports/Narracan%20District-Results%20by%20Voting%20Centre.xls')
CATEGORIES = {'early votes total': 'Early Votes', 'early votes': 'Early Votes',
              'postal votes': 'Postal Votes', 'absent votes': 'Absent Votes',
              'marked as voted votes': 'Marked As Voted Votes', 'provisional votes': 'Provisional Votes'}


def cache_pages(cache):
    """Save both public count tables for the 87 November districts and the supplementary contest."""
    election = vec.ELECTIONS['2022vic']
    pages = vec.discover_district_urls(election, vec._download(election.index_url))
    urls = {'narracan-fp.html': SUPPLEMENT + '/district-results-by-voting-centre',
            'narracan-tcp.html': SUPPLEMENT + '/2cp-results-by-voting-centre',
            'narracan-primary.xls': PRIMARY}
    for page in pages:
        slug = page.rsplit('/', 1)[1].removesuffix('-district-results-by-voting-centre')
        urls[slug + '-fp.html'] = page
        urls[slug + '-tcp.html'] = page.replace(slug + '-district-results-by-voting-centre',
                                                slug + '-2cp-results-by-voting-centre')
    cache.mkdir(parents=True, exist_ok=True)
    def fetch(item):
        filename, url = item
        if not (cache / filename).exists():
            save(cache / filename, vec._download(url))
    with ThreadPoolExecutor(max_workers=5) as pool:
        list(pool.map(fetch, urls.items()))


def save(path, data):
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as tmp:
        temporary = Path(tmp.name)
        tmp.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def candidate_counts(values, ids):
    return {i: int(str(v).replace(',', '')) for i, v in zip(ids, values, strict=True)}


def checked(records, totals, label):
    """A published district total independently checks that no row was lost or counted twice."""
    for candidate, expected in totals.items():
        actual = sum(counts[candidate] for counts in records.values())
        if actual != expected:
            raise ValueError(f'{label}: candidate {candidate} has {actual} allocated votes, expected {expected}')
    return records


def first_preferences(rows, ids):
    records = {}
    ordinary = False
    for row in rows[4:]:
        label = row[0].casefold()
        if label == 'ordinary votes':
            ordinary = True
        elif label == 'ordinary votes total':
            ordinary = False
        elif ordinary or (label in CATEGORIES and row[1]):
            name = row[0] if ordinary else CATEGORIES[label]
            if name in records:
                raise ValueError(f'Duplicate first-preference row: {name}')
            records[name] = candidate_counts(row[1:-2], ids)
    return checked(records, candidate_counts(rows[2][1:-2], ids), 'First preferences')


def narracan_primary(path, candidates):
    """Use one internally consistent FP count for the postponed January contest.

    The final recheck webpage omits the candidate split for declarations.
    The VEC's primary workbook gives every booth and declaration at the same
    earlier count, together with its own candidate totals (37,503 formal).
    """
    import xlrd
    sheet = xlrd.open_workbook(path).sheet_by_index(0)
    ids_by_name = {c['name']: i for i, c in candidates.items()}
    columns = {ids_by_name[name]: col for col, header in enumerate(sheet.row_values(12))
               if (name := str(header).split('\n')[0]) in ids_by_name}
    if set(columns) != set(candidates):
        raise ValueError('Narracan primary and recheck candidate rosters differ')
    official_types = {'Early Vote': 'Early Votes', 'Postal Vote': 'Postal Votes',
                      'Marked as Voted': 'Marked As Voted Votes', 'Provisional': 'Provisional Votes'}
    records = {}
    ordinary = True
    for i in range(14, sheet.nrows):
        row = sheet.row_values(i)
        name = row[1]
        if name == 'Total Ordinary Votes':
            ordinary = False
        elif name == 'TOTAL ALL VOTE TYPES':
            totals = {i: int(row[col]) for i, col in columns.items()}
            return checked(records, totals, 'Narracan primary')
        elif name and (ordinary or name in official_types):
            records[official_types.get(name, name)] = {i: int(row[col]) for i, col in columns.items()}
    raise ValueError('Missing Narracan primary totals')


def two_candidate(rows, candidates, booth_names):
    ids_by_name = {c['name']: i for i, c in candidates.items()}
    ids = [ids_by_name[name] for name in rows[0][1:3]]
    records = {}
    for row in rows[5:]:
        name = CATEGORIES.get(row[0].casefold(), row[0])
        if name in booth_names or name in CATEGORIES.values():
            if name in records:
                raise ValueError(f'Duplicate two-candidate row: {name}')
            records[name] = candidate_counts(row[1:3], ids)
    return checked(records, candidate_counts(rows[3][1:3], ids), 'Two-candidate votes')


def district(cache, fp_file):
    fp = vec._parse_html(fp_file.read_bytes(), fp_file.name)
    suffix = ' results by voting centre'
    title = next(h for h in fp.headings if h.casefold().endswith(suffix))
    seat = title[:-len(suffix)].split(' District')[0]
    candidates = {str(i): dict(name=name, party=party.upper() or 'INDEPENDENT')
                  for i, (name, party) in enumerate(zip(fp.rows[0][1:-2], fp.rows[1][1:-2], strict=True))}
    first = (narracan_primary(cache / 'narracan-primary.xls', candidates) if seat == 'Narracan'
             else first_preferences(fp.rows, candidates))
    tcp_path = cache / fp_file.name.replace('-fp.html', '-tcp.html')
    tcp = two_candidate(vec._parse_html(tcp_path.read_bytes(), tcp_path.name).rows, candidates, first)
    booths = {name: dict(fp=first.get(name, {}), tcp=tcp.get(name, {})) for name in sorted(first.keys() | tcp.keys())}
    return seat, dict(candidates=candidates, booths=booths)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cached', action='store_true', help='Use the already downloaded VEC tables.')
    args = parser.parse_args()
    cache = REPOSITORY_DIRECTORY / 'downloads/turnout/vec-2022-public'
    if not args.cached:
        cache_pages(cache)
    records = dict(district(cache, path) for path in sorted(cache.glob('*-fp.html')))
    if len(records) != 88:
        raise ValueError(f'Expected 88 districts including Narracan, found {len(records)}')
    target = REPOSITORY_DIRECTORY / 'analysis/Booth Results/2022vic.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    save(target, json.dumps(records, ensure_ascii=False).encode('utf-8'))
    print(f'Checked and saved candidate and booth counts for {len(records)} districts.')


if __name__ == '__main__':
    main()

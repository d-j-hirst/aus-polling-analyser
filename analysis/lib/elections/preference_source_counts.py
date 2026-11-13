"""Translate cached commission preference tables into the common evidence format.

Federal original-source tables identify the FP candidate behind each transfer.
SA and Victorian full distributions identify an excluded pile, which can include
other candidates' ballots. Separate readers retain that distinction, the full
candidate roster and the actual count sequence. Every cached artifact is checked
against the maintained source registry before use; these readers never download
data or change the underlying source files.
"""

from collections import defaultdict
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import zipfile

from lib.elections.preference_evidence import (
    add_original_observation, attach_distribution, candidate_record, contest_record,
    name_tokens, party_group, require,
)


class SourceCache:
    """Read bounded, hash-identified source files without trusting local filenames."""

    def __init__(self, folder, registry):
        self.folder = Path(folder)
        self.registry = registry
        self.checked = set()
        self.csv_repairs = defaultdict(int)

    def read(self, name):
        require(Path(name).name == name and name in self.registry['files'],
                f'Unregistered preference source: {name}')
        path = self.folder / name
        metadata = self.registry['files'][name]
        require(path.stat().st_size <= 16 * 1024 * 1024, f'Source exceeds read budget: {name}')
        raw = path.read_bytes()
        if metadata.get('verification') == 'uncompressed_member_hashes':
            # Ordinary downloads must match their exact recorded bytes. The
            # small 2001 cache is locally repackaged official material instead:
            # changing a ZIP/GZIP timestamp or compression level must not make
            # identical source tables impossible to reconstruct. Its authoritative
            # identities are the uncompressed member names and content hashes.
            self._verify_member_payloads(raw, metadata, name)
        else:
            require(len(raw) == metadata['bytes'], f'Source size changed: {name}')
            require(hashlib.sha256(raw).hexdigest() == metadata['sha256'], f'Source hash changed: {name}')
        self.checked.add(name)
        return raw

    def _verify_member_payloads(self, raw, metadata, name):
        if 'members' in metadata:
            expected = {member['member']: member for member in metadata['members']}
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                require(len(archive.namelist()) == len(expected) and set(archive.namelist()) == set(expected),
                        f'Derived source archive members changed: {name}')
                for member, description in expected.items():
                    digest = hashlib.sha256(archive.read(member)).hexdigest()
                    require(digest == description['sha256_uncompressed'], f'Source member hash changed: {name}/{member}')
        else:
            digest = hashlib.sha256(gzip.decompress(raw)).hexdigest()
            require(digest == metadata['sha256_uncompressed'], f'Source member hash changed: {name}')

    def table(self, name):
        return csv_rows(self.read(name), name, self.csv_repairs)

    def json(self, name):
        return json.loads(self.read(name))


def csv_rows(raw, source_name='', repairs=None):
    """Read an AEC table after its descriptive first line, retaining strict columns.

    One published 2025 supplemental table has unquoted commas in the Shooters
    party name. Only that exact extra-field pattern is repaired. Ordinary tables
    follow the header as printed; other column shifts remain errors.
    """
    lines = raw.decode('utf-8-sig').splitlines()
    start = next((i for i, line in enumerate(lines) if line.startswith('StateAb,')), None)
    require(start is not None, f'No AEC CSV header in {source_name}')
    rows = csv.reader(io.StringIO('\n'.join(lines[start:])))
    header = next(rows)
    output = []
    for values in rows:
        if not values:
            continue
        if (source_name == 'aec-2025-nonclassic-tpp-flow.csv'
                and len(values) == len(header) + 1
                and values[5:7] == ['Shooters', ' Fishers and Farmers Party']):
            values[5:7] = ['Shooters, Fishers and Farmers Party']
            if repairs is not None:
                repairs[source_name] += 1
        require(len(values) == len(header), f'Unexpected CSV columns in {source_name}: {values}')
        output.append(dict(zip((key.lower() for key in header), values)))
    return output


def modern_identity(row, prefix):
    return candidate_record(
        row[prefix + 'candidateid'],
        (row[prefix + 'candidatesurname'] + ', ' + row[prefix + 'candidategivennm']).strip(', '),
        row[prefix + 'candidatepartyab'], 0, row[prefix + 'candidatepartynm'])


def check_identity(expected, printed, where):
    require(expected['id'] == printed['id']
            and name_tokens(expected['name']) == name_tokens(printed['name'])
            and expected['party_group'] == printed['party_group'],
            f'{where}: FP and transfer candidate identities disagree: '
            f'{expected["id"]} {expected["name"]} ({expected["party_code"]}) / '
            f'{printed["id"]} {printed["name"]} ({printed["party_code"]})')


def modern_primaries(cache, specification):
    """Use all formal candidate primaries, not just candidates distributing votes."""
    by_division = defaultdict(list)
    for row in cache.table(specification['primary_file']):
        if int(row['candidateid']) <= 0 or row['surname'] == 'Informal':
            continue
        by_division[row['divisionid']].append(row)
    contests = {}
    for division, rows in by_division.items():
        candidates = [candidate_record(
            row['candidateid'], row['surname'] + ', ' + row['givennm'],
            row['partyab'], int(row['totalvotes']), row['partynm']) for row in rows]
        first = rows[0]
        contests[division] = contest_record(
            f'{specification["year"]}fed', specification['event'],
            first['divisionnm'], first['stateab'], candidates,
            [specification['primary_file']])
    return contests


def add_modern_flows(table, contests, source, kind='tcp', quarantine_rules=()):
    """Match each original-source pair to its complete district FP population."""
    by_source = defaultdict(list)
    for row in table:
        if int(row['fromcandidateid']) != 0:
            by_source[(row['divisionid'], row['fromcandidateid'])].append(row)
    for (division, source_id), rows in by_source.items():
        require(division in contests, f'{source}: no FP district for {division}')
        contest = contests[division]
        identities = {c['id']: c for c in contest['candidates']}
        require(len(rows) == 2, f'{contest["id"]}: source does not have two destinations')
        require(source_id in identities, f'{contest["id"]}: original candidate absent from FP')
        printed_source = modern_identity(rows[0], 'from')
        check_identity(identities[source_id], printed_source, source)
        reported = {}
        if printed_source['party_code'] != identities[source_id]['party_code']:
            reported[source_id] = printed_source['party_code']
        destinations = []
        for row in rows:
            target = modern_identity(row, 'to')
            require(target['id'] in identities, f'{contest["id"]}: recipient absent from FP')
            check_identity(identities[target['id']], target, source)
            if target['party_code'] != identities[target['id']]['party_code']:
                reported[target['id']] = target['party_code']
            destinations.append(dict(candidate=target['id'], votes=int(row['transfercount'])))
        require(sum(d['votes'] for d in destinations) == identities[source_id]['fp_votes'],
                f'{contest["id"]}: original transfer total differs from FP')
        reason = next((r['reason'] for r in quarantine_rules
                       if (r['election'], r['seat'], r['kind'])
                       == (contest['election'], contest['seat'], kind)), None)
        add_original_observation(
            contest, source_id, destinations, [source], kind,
            status='quarantined' if reason else 'accepted', reason=reason,
            reported_parties={source: reported} if reported else None)


def aggregate_booth_flows(cache, filename):
    """Sum booth/category rows once, retaining finalists' own-FP rows as well.

    Declaration categories can share polling-place ID zero. Grouping by that ID
    would lose categories; the original source and destination candidate IDs
    instead identify the totals needed here. The archive stays compressed.
    """
    totals = defaultdict(int)
    examples = {}
    with zipfile.ZipFile(io.BytesIO(cache.read(filename))) as archive:
        require(archive.testzip() is None, f'Invalid ZIP checksum: {filename}')
        for member in archive.namelist():
            if not member.lower().endswith('.csv'):
                continue
            for row in csv_rows(archive.read(member), filename):
                key = row['divisionid'], row['fromcandidateid'], row['tocandidateid']
                totals[key] += int(row['transfercount'])
                examples[key] = row
    require(bool(totals), f'No booth flow records in {filename}')
    return [dict(examples[key], transfercount=str(votes)) for key, votes in sorted(totals.items())]


def by_election_counts(cache, specification):
    """Recover the complete FP roster from a by-election original-source table.

    Donor totals supply non-finalists' FP counts. Rows labelled First Preferences
    (source ID zero) supply finalists' own primaries. This is a complete formal
    population, but not an independent FP-table verification; fp_basis records
    that distinction for later calibration and source auditing.
    """
    table = aggregate_booth_flows(cache, specification['flow_file'])
    candidates = {}
    original_totals = defaultdict(int)
    for row in table:
        is_own = row['fromcandidateid'] == '0'
        candidate = modern_identity(row, 'to' if is_own else 'from')
        if candidate['id'] in candidates:
            check_identity(candidates[candidate['id']], candidate, specification['flow_file'])
        candidates[candidate['id']] = candidate
        original_totals[candidate['id']] += int(row['transfercount'])
    for candidate in candidates.values():
        candidate['fp_votes'] = original_totals[candidate['id']]
    divisions = {row['divisionid'] for row in table}
    require(len(divisions) == 1, f'Unexpected multi-division by-election: {specification}')
    contest = contest_record(
        f'{specification["year"]}fed', specification['event'], specification['seat'],
        table[0]['stateab'], list(candidates.values()), [specification['flow_file']],
        fp_basis='reconstructed_from_original_source_table', by_election=True)
    add_modern_flows(table, {next(iter(divisions)): contest}, specification['flow_file'])
    return contest


def html_tables(raw):
    """Read table cell text without interpreting a blank affiliation as a party."""
    from bs4 import BeautifulSoup
    soup = BeautifulSoup(raw, 'html.parser')
    return [[[cell.get_text(' ', strip=True) for cell in row.find_all(['td', 'th'])]
             for row in table.find_all('tr')] for table in soup.find_all('table')
            if table.find('table') is None]


def integer(value):
    return int(value.replace(',', '')) if value else 0


def werriwa_counts(cache, specification):
    """Read the older Werriwa table, whose candidates have no published IDs."""
    primary_file = 'aec-werriwa-2005-fp.html'
    primary = next(t for t in html_tables(cache.read(primary_file))
                   if t and t[0] == ['Candidate', 'Party', 'Votes', '%', 'Swing (%)'])
    rows = {row[0]: row for row in primary[1:]
            if row[0] not in ['......', 'FORMAL', 'INFORMAL', 'TOTAL'] and len(row) == 5}
    identities = {name: candidate_record(None, name, row[1], integer(row[2]),
                                        row[1], id_scheme='printed_name')
                  for name, row in rows.items()}
    contest = contest_record('2005fed', specification['event'], 'Werriwa', 'NSW',
                             list(identities.values()), [primary_file], by_election=True)
    flows = next(t for t in html_tables(cache.read(specification['flow_file']))
                 if t and t[0][:2] == ['Candidate', 'Party'])
    targets = []
    for heading in flows[0][2:]:
        matches = [c for name, c in identities.items() if heading.startswith(name + ' ')]
        require(len(matches) == 1, 'Ambiguous Werriwa recipient')
        targets.append(matches[0])
    require(len(targets) == 2, 'Werriwa final pair is missing')
    for row in flows[1:]:
        if len(row) == 6 and row[0] in identities:
            add_original_observation(
                contest, identities[row[0]]['id'],
                [dict(candidate=c['id'], votes=integer(value))
                 for c, value in zip(targets, [row[2], row[4]])],
                [specification['flow_file']])
    return contest


def legacy_2001_counts(cache, specification):
    """Read division summaries of original-source tables from the AEC 2001 CD.

    The compact cache contains selected original archive members. Individual
    member hashes are checked as well as the local container. Division Summary
    rows are used once; polling-place rows and subtotals are not added again.
    Separate candidate import records supply the whole formal FP population.
    """
    compressed = cache.read(specification['primary_file'])
    primary_raw = gzip.decompress(compressed)
    metadata = cache.registry['files'][specification['primary_file']]
    require(hashlib.sha256(primary_raw).hexdigest() == metadata['sha256_uncompressed'],
            'Legacy FP member hash differs')
    by_seat = defaultdict(list)
    for row in csv.DictReader(io.StringIO(primary_raw.decode('cp1252')), delimiter=';'):
        if row['Candidate'] and int(row['Ballot Position']) < 100:
            by_seat[row['Division']].append(row)
    contests = {}
    for seat, rows in by_seat.items():
        candidates = [candidate_record(
            row['Ballot Position'], row['Candidate'], row['Party'], int(row['Vote']),
            id_scheme='ballot_position') for row in rows]
        contests[seat] = contest_record('2001fed', specification['event'], seat,
                                        rows[0]['State'], candidates, [specification['primary_file']])
    members = {m['member']: m for m in cache.registry['files'][specification['flow_file']]['members']}
    seen_seats = set()
    with zipfile.ZipFile(io.BytesIO(cache.read(specification['flow_file']))) as archive:
        require(set(archive.namelist()) == set(members), 'Unexpected legacy archive members')
        for member in archive.namelist():
            raw = archive.read(member)
            require(hashlib.sha256(raw).hexdigest() == members[member]['sha256_uncompressed'],
                    f'Legacy source member hash differs: {member}')
            table = list(csv.reader(io.StringIO(raw.decode('cp1252')), delimiter='\t'))
            header = next([v.strip() for v in row if v.strip()] for row in table
                          if any(v.strip() == 'Candidate' for v in row))
            require(header[3:] in [['Total'], ['Exhausted', 'Total']], f'Unexpected legacy header: {member}')
            titles = {match[1] for row in table for value in row
                      if (match := re.fullmatch(r'(.+)\s+\([\d,]+\)', value.strip()))
                      and match[1] in contests}
            require(len(titles) == 1, f'Ambiguous legacy district: {member}')
            seat = next(iter(titles))
            require(seat not in seen_seats, f'Duplicate legacy district summary: {seat}')
            seen_seats.add(seat)
            contest = contests[seat]
            targets = []
            for printed in header[1:3]:
                matches = [c for c in contest['candidates']
                           if set(name_tokens(printed)).issubset(name_tokens(c['name']))]
                require(len(matches) == 1, f'Ambiguous legacy finalist: {member}')
                targets.append(matches[0])
            in_summary = False
            for row in table:
                values = [v.strip() for v in row if v.strip()]
                if 'Division Summary' in values:
                    in_summary = True
                    continue
                if not in_summary or not values:
                    continue
                printed = re.fullmatch(r'(.+) \(([^()]*)\)', values[0])
                name = printed[1] if printed else values[0]
                matches = [c for c in contest['candidates'] if name_tokens(c['name']) == name_tokens(name)]
                if not matches:
                    continue
                require(len(matches) == 1 and len(values) in [7, 8], f'Invalid legacy source row: {member}')
                candidate = matches[0]
                require(int(values[1]) == int(values[-1]) == candidate['fp_votes'],
                        f'Legacy source FP differs: {seat}/{name}')
                add_original_observation(
                    contest, candidate['id'],
                    [dict(candidate=target['id'], votes=int(value))
                     for target, value in zip(targets, [values[2], values[4]])],
                    [specification['flow_file']], exhausted=int(values[6]) if len(values) == 8 else 0,
                    locator=dict(member=member, section='Division Summary'))
            require(len(contest['original_source_observations']) == len(contest['candidates']) - 2,
                    f'Incomplete legacy district summary: {seat}')
    require(seen_seats == set(contests), 'Missing legacy division source table')
    return list(contests.values())


def sa_counts(cache, specification):
    """Retain the actual excluded piles and progressive totals in every SA seat."""
    static = cache.json(specification['static_file'])
    result = cache.json(specification['distribution_file'])
    settings = {d['districtName']: d for d in static['districts']}
    output = []
    for district in result['districts']:
        seat = district['districtId']
        rounds = district['finalDistribution']
        require(bool(rounds) and rounds[0]['roundNumber'] == 0, f'Missing SA initial count: {seat}')
        mapping = {name_tokens(c['candidateName']): c for c in settings[seat]['candidates']}
        require(len(mapping) == len(settings[seat]['candidates']), f'Ambiguous SA candidate names: {seat}')
        candidates = []
        for value in rounds[0]['candidateResults']:
            identity = mapping[name_tokens(value['candidateName'])]
            candidates.append(candidate_record(str(value['candidateId']), value['candidateName'],
                                                identity['partyId'], value['progressiveTotal']))
        contest = contest_record(specification['election'], 'sa-2026', seat, 'SA', candidates,
                                  [specification['static_file'], specification['distribution_file']])
        stages = []
        for value in rounds[1:]:
            excluded = str(value['excludedCandidateId'])
            transfers = [dict(candidate=str(c['candidateId']), votes=c['voteChange'])
                         for c in value['candidateResults']
                         if str(c['candidateId']) != excluded and not c['isExcluded']]
            stages.append(dict(round=value['roundNumber'], excluded_candidate=excluded,
                               pile_votes=value['excludedCandidateVotes'], transfers=transfers,
                               progressive_totals={str(c['candidateId']): c['progressiveTotal'] or 0
                                                   for c in value['candidateResults']}))
        continuing = [dict(candidate=str(c['candidateId']), votes=c['progressiveTotal'])
                      for c in rounds[-1]['candidateResults'] if not c['isExcluded']]
        attach_distribution(contest, stages, continuing, [specification['distribution_file']])
        output.append(contest)
    return output


def vic_counts(cache, specification):
    """Read a VEC distribution without inventing transfers after its actual stop."""
    primary = next(t for t in html_tables(cache.read(specification['primary_file']))
                   if t and t[0] == ['Candidate', 'Party', '1st pref. votes', '% votes'])
    identities = {name_tokens(row[0]): row for row in primary[1:]
                  if len(row) == 4 and row[0] not in ['Formal', 'Informal', 'Total']}
    table = next(t for t in html_tables(cache.read(specification['distribution_file']))
                 if len(t) > 1 and t[1][0] == 'Total first preference votes recorded for each candidate')
    candidates = []
    for position, name in enumerate(table[0][1:-1], 1):
        identity = identities[name_tokens(name)]
        votes = integer(table[1][position])
        require(votes == integer(identity[2]), f'Vectorian initial count differs: {specification["seat"]}')
        candidates.append(candidate_record(str(position), name, identity[1], votes,
                                           identity[1], id_scheme='ballot_position'))
    contest = contest_record(specification['election'], 'vic-2022', specification['seat'], 'VIC',
                              candidates, [specification['primary_file'], specification['distribution_file']])
    require(contest['formal_fp_votes'] == integer(table[1][-1]), 'Victorian formal total differs')
    current = {c['id']: c['fp_votes'] for c in candidates}
    excluded = set()
    pending = None
    stages = []
    for row in table[2:]:
        if row[0].startswith('Transfer of '):
            require(pending is None, 'Missing Victorian progressive count')
            match = re.fullmatch(r'Transfer of ([\d,]+) ballot papers of (.+) \(.+ excluded candidate\)', row[0])
            require(match is not None, f'Unknown Victorian exclusion label: {row[0]}')
            matches = [c for c in candidates if name_tokens(c['name']) == name_tokens(match[2])]
            require(len(matches) == 1, 'Ambiguous Victorian excluded candidate')
            source = matches[0]['id']
            pile = integer(match[1])
            require(source not in excluded and current[source] == pile == integer(row[-1]),
                    'Victorian excluded pile differs')
            excluded.add(source)
            transfers = [dict(candidate=c['id'], votes=integer(row[int(c['id'])]))
                         for c in candidates if c['id'] not in excluded]
            current[source] = 0
            for transfer in transfers:
                current[transfer['candidate']] += transfer['votes']
            pending = dict(round=len(stages) + 1, excluded_candidate=source, pile_votes=pile,
                           transfers=transfers, progressive_totals=dict(current))
            stages.append(pending)
        elif row[0] in ['Progressive Total', 'FINAL TOTAL']:
            require(pending is not None, 'Victorian progressive count without exclusion')
            require(all(integer(row[int(key)]) == votes for key, votes in current.items())
                    and integer(row[-1]) == contest['formal_fp_votes'],
                    'Victorian progressive count disagrees with transfers')
            pending = None
    require(pending is None, 'Missing final Victorian progressive count')
    continuing = [dict(candidate=key, votes=votes) for key, votes in current.items() if key not in excluded]
    attach_distribution(contest, stages, continuing, [specification['distribution_file']])
    return contest


def read_all_counts(cache, registry, supplements):
    """Outline source preparation; each reader above handles one reporting format."""
    contests = legacy_2001_counts(cache, registry['legacy_2001'])
    modern = {}
    for specification in registry['general_elections']:
        current = modern_primaries(cache, specification)
        add_modern_flows(cache.table(specification['flow_file']), current, specification['flow_file'])
        modern[specification['year']] = current
        contests.extend(current.values())
    for observation in supplements['records']:
        contest = modern[observation['year']][observation['division_id']]
        require(contest['seat'] == observation['seat'], 'Transcribed alternative-TPP district differs')
        candidate = next(c for c in contest['candidates'] if c['id'] == observation['source_candidate_id'])
        require(candidate['fp_votes'] == observation['original_fp_votes'],
                'Transcribed alternative-TPP original FP differs')
        add_original_observation(
            contest, candidate['id'],
            [dict(candidate=d['candidate_id'], votes=d['votes']) for d in observation['destinations']],
            [observation['source_publication']], kind='alternative_tpp')
    supplemental_file = 'aec-2025-nonclassic-tpp-flow.csv'
    add_modern_flows(cache.table(supplemental_file), modern[2025], supplemental_file,
                     kind='alternative_tpp', quarantine_rules=registry['quarantine_rules'])
    require(cache.csv_repairs[supplemental_file] == 6, 'The known supplemental CSV formatting changed')
    for specification in registry['by_elections']:
        if specification['event'] == '12426':
            contests.append(werriwa_counts(cache, specification))
        else:
            contests.append(by_election_counts(cache, specification))
    contests.extend(sa_counts(cache, registry['sa_distribution']))
    contests.extend(vic_counts(cache, specification) for specification in registry['victorian_distributions'])
    # Card contents are curated by manual reading, not inferred by this reader.
    # Still verify that each retained document is the artifact that was reviewed.
    for context in registry['recommendations']:
        if context['source_file'] not in cache.checked:
            cache.read(context['source_file'])
    return contests

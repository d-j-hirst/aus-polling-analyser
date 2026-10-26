"""Prepare ECQ ballot identities and historical booth accounts for live use.

The XML archive is the authority's own results export. Select the state election
explicitly because the same file can also contain a council by-election. Final
historical FP counts come from the official recheck; TCP remains unavailable
where ECQ has not published a two-candidate account for the booth.
"""

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile


def read_state_election(path, year):
    """Read one source and reject an ambiguous or wrong election identity."""
    path = Path(path)
    if path.suffix.lower() == '.zip':
        with ZipFile(path) as archive:
            names = [n for n in archive.namelist() if n.lower().endswith('.xml')]
            if len(names) != 1:
                raise ValueError('Expected one XML document in the ECQ archive.')
            root = ET.fromstring(archive.read(names[0]))
    else:
        root = ET.parse(path).getroot()
    matches = [e for e in root.findall('election')
               if e.get('electionName') == f'{year} State General Election']
    if len(matches) != 1:
        raise ValueError('Expected exactly one matching Queensland state election.')
    return root, matches[0]


def candidate_roster(election):
    """Retain every ballot entry, including candidates grouped as Other in forecasts."""
    rows = []
    for district in election.findall('districts/district'):
        for candidate in district.findall('candidates/candidate'):
            rows.append(dict(district=district.get('districtName'),
                district_id=int(district.get('number')),
                ballot_order=int(candidate.get('ballotOrderNumber')),
                ballot_name=candidate.get('ballotName'), party=candidate.get('party'),
                party_code=candidate.get('partyCode')))
    return rows


def _candidate_counts(booth, field, candidate_ids):
    """A missing account stays missing; a published zero is a genuine integer zero."""
    result = {}
    for candidate in booth.findall(field + '/candidate'):
        identity = candidate_ids[candidate.get('ballotName')]
        count = int(candidate.findtext('count'))
        if count < 0 or identity in result:
            raise ValueError('Invalid historical candidate-count vector.')
        result[str(identity)] = count
    return result


def _historical_district(district):
    """Pair official FP with published final TCP without inventing preferences."""
    roster = district.findall('candidates/candidate')
    ids = {candidate.get('ballotName'): i for i, candidate in enumerate(roster)}
    if len(ids) != len(roster):
        raise ValueError('Duplicate candidate in historical district.')
    candidates = {str(i): dict(name=c.get('ballotName'), party=c.get('party'))
                  for i, c in enumerate(roster)}
    rounds = {r.get('countName'): r for r in district.findall('countRound')}
    fp_round = rounds['Official First Preference Count']
    tcp_round = rounds.get('Official Distribution of Preferences Count')
    booths = {}
    for booth in fp_round.findall('booths/booth'):
        name = booth.get('name').replace('CBD Early Voting', 'CBD Early Voting Centre')
        # Some archives already use the full venue name.
        name = name.replace('Centre Centre', 'Centre')
        if name in booths:
            raise ValueError('Duplicate historical booth name: ' + name)
        counts = _candidate_counts(booth, 'primaryVoteResults', ids)
        if sum(counts.values()) != int(booth.findtext('formalVotes')):
            raise ValueError('Historical booth FP does not reconcile: ' + name)
        booths[name] = dict(fp=counts, tcp={})
    if tcp_round is not None:
        for booth in tcp_round.findall('booths/booth'):
            name = booth.get('name').replace('CBD Early Voting', 'CBD Early Voting Centre').replace('Centre Centre', 'Centre')
            counts = _candidate_counts(booth, 'twoCandidateVotes', ids)
            if name in booths and len(counts) == 2:
                booths[name]['tcp'] = counts
    expected = int(fp_round.findtext('totalFormalVotes/count'))
    if sum(sum(b['fp'].values()) for b in booths.values()) != expected:
        raise ValueError('Historical district FP does not reconcile.')
    return dict(candidates=candidates, booths=booths)


def historical_results(election):
    """Produce the existing shared booth-history format, preserving party names."""
    districts = election.findall('districts/district')
    return {d.get('districtName'): _historical_district(d) for d in districts}


def data_fingerprint(election):
    """Ignore timestamps and derived percentages, retaining all count revisions.

    Compare consecutive states, not every state ever seen: A -> B -> A is a
    meaningful correction. Official rounds, candidates, roll and availability
    remain part of the comparison even before the live reader uses them.
    """
    import hashlib
    ignored_attributes = {'lastUpdated', 'percentRollCounted'}
    # ECQ repeats the export timestamp inside each election as well as at the
    # document root. Neither timestamp is a new count observation by itself.
    ignored_elements = {'percentage', 'generationDateTime'}

    def canonical(element):
        attributes = sorted((k, v) for k, v in element.attrib.items() if k not in ignored_attributes)
        children = [canonical(c) for c in element if c.tag not in ignored_elements]
        return element.tag, attributes, (element.text or '').strip(), children

    return hashlib.sha256(json.dumps(canonical(election), separators=(',', ':')).encode()).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--year', type=int, required=True)
    parser.add_argument('--historical-output', type=Path, required=True)
    args = parser.parse_args(argv)
    _, election = read_state_election(args.source, args.year)
    result = historical_results(election)
    temporary = args.historical_output.with_suffix('.tmp')
    temporary.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    temporary.replace(args.historical_output)
    print(f'Prepared {len(result)} historical districts.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

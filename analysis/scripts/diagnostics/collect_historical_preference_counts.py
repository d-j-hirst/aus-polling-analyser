"""Prepare matched historical notional TPP for live preference estimation.

Ordinary historical TCP usually supplies the comparison already. This collector
adds actual Labor/Coalition counts for previous NON-classic districts, preserving
the exact original candidate FP population beside each count. It does not fit
preference rates or read current-election forecasts. Sources must already be
cached unless --download is explicitly supplied; commission feed archives remain
unchanged. --check verifies the maintained output without writing it.
"""
import argparse
from collections import defaultdict
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

ANALYSIS = Path(__file__).resolve().parents[2]
ROOT = ANALYSIS.parent
sys.path.insert(0, str(ANALYSIS))
from scripts.diagnostics.category_sources import fetch
from scripts.diagnostics.category_tpp_history import csv_rows

EVENTS = {2010:15508, 2013:17496, 2016:20499, 2019:24310, 2022:27966, 2025:31496}
DECLARATIONS = {'Absent':2, 'Provisional':3, 'PrePoll':4, 'Postal':5}
# These are commission affiliation codes, not distinct forecast parties. The
# 2010 Queensland results use LNQ for the same LNP that later feeds call LNP.
# Recognise both when identifying classic contests and checking that Coalition
# TPP cannot be smaller than Coalition primaries. Otherwise Kennedy's separate
# historical TPP is wrongly excluded as having no Coalition candidate.
COALITION = {'LIB', 'LP', 'NAT', 'NP', 'LNP', 'LNQ', 'CLP'}
LABOR = {'ALP', 'CLR'}
HEADER = ['term', 'seat', 'kind', 'id', 'fp', 'labor', 'coalition']


def csv_table(year, stem, download, sources):
    event = EVENTS[year]
    url = f'https://results.aec.gov.au/{event}/Website/Downloads/{stem}Download-{event}.csv'
    raw = fetch(url, download=download)
    sources[url] = dict(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw))
    return csv_rows(raw)


def last_feed(folder):
    candidates = [(match[1], path) for path in folder.glob('*.zip')
                  if (match := re.search(r'-(\d{14})\.zip$', path.name))]
    if not candidates:
        raise FileNotFoundError(f'No timestamped commission feed in {folder}')
    return max(candidates)[1]


def raw_primaries(path, eligible):
    """Read original polling-place FP, independently checking district totals.

    The light feed omits candidate names/parties; the official candidate CSV
    supplies those identities. Zero placeholders can be absent from either
    representation, but every positive candidate count must agree.
    """
    result = {}
    with zipfile.ZipFile(path) as archive:
        xml = next(n for n in archive.namelist() if n.endswith('.xml') and 'mediafeed' in n.lower())
        with archive.open(xml) as stream:
            for _, contest in ET.iterparse(stream, events=('end',)):
                if contest.tag.rsplit('}', 1)[-1] != 'Contest':
                    continue
                name = contest.findtext('{*}ContestIdentifier/{*}ContestName')
                if name in eligible:
                    def counts(parent):
                        if parent is None:
                            return {}
                        return {int(c.find('{*}CandidateIdentifier').get('Id')):int(c.findtext('{*}Votes') or 0)
                                for c in parent.findall('{*}Candidate')}
                    full = counts(contest.find('{*}FirstPreferences'))
                    if {c:v for c,v in full.items() if v} != eligible[name]['fp']:
                        raise ValueError(f'{path.name}: {name} XML/CSV FP totals disagree')
                    places = {}
                    for place in contest.findall('{*}PollingPlaces/{*}PollingPlace'):
                        pid = int(place.find('{*}PollingPlaceIdentifier').get('Id'))
                        places[pid] = counts(place.find('{*}FirstPreferences'))
                    accounted = sum(sum(fp.values()) for fp in places.values())
                    accounted += sum(sum(fp.values()) for fp in eligible[name]['declarations'].values())
                    if accounted != sum(full.values()):
                        raise ValueError(f'{path.name}: {name} category totals disagree')
                    result[name] = places
                contest.clear()
    if set(result) != set(eligible):
        raise ValueError(f'{path.name}: missing eligible districts {set(eligible)-set(result)}')
    return result


def matched_record(term, seat, kind, identifier, fp, tpp, candidates):
    """Accept counts only when FP and TPP describe one feasible population.

    Missing TPP is not zero. Nor can inconsistent denominators be rescaled to
    manufacture a precise historical preference account. Such records stay
    outside this additional evidence; the live model retains its usual fallback.
    """
    if tpp is None:
        return None, 'no notional TPP'
    labor, coalition = tpp
    positive = {candidate:votes for candidate,votes in fp.items() if votes > 0}
    total = sum(positive.values())
    if total <= 0:
        return None, 'empty FP'
    if any(v < 0 for v in fp.values()) or labor < 0 or coalition < 0 or labor+coalition != total:
        return None, 'different or invalid FP/TPP totals'
    own_labor = sum(v for c,v in positive.items() if candidates[c] in LABOR)
    own_coalition = sum(v for c,v in positive.items() if candidates[c] in COALITION)
    if labor < own_labor or coalition < own_coalition:
        return None, 'TPP smaller than finalist primary'
    encoded = ';'.join(f'{candidate}:{votes}' for candidate,votes in sorted(positive.items()))
    if ',' in seat or '"' in seat:
        raise ValueError('The maintained unquoted location format requires a comma-free seat name')
    return [term, seat, kind, identifier, encoded, labor, coalition], None


def collect_election(year, feeds_root, download=False):
    sources = {}
    roster = defaultdict(lambda:dict(fp={}, candidates={}, declarations=defaultdict(dict)))
    for row in csv_table(year, 'HouseFirstPrefsByCandidateByVoteType', download, sources):
        if row['PartyNm'].strip().lower() == 'informal':
            continue
        seat = roster[row['DivisionNm']]
        cid = int(row['CandidateID'])
        seat['candidates'][cid] = row['PartyAb']
        if int(row['TotalVotes']):
            seat['fp'][cid] = int(row['TotalVotes'])
        for label, code in DECLARATIONS.items():
            seat['declarations'][code][cid] = int(row[label+'Votes'])
    tcp = defaultdict(list)
    for row in csv_table(year, 'HouseTcpByCandidateByVoteType', download, sources):
        tcp[row['DivisionNm']].append(row['PartyAb'])
    eligible, omitted = {}, {}
    for name, seat in roster.items():
        pair = tcp[name]
        parties = set(seat['candidates'].values())
        classic = len(pair) == 2 and any(p in LABOR for p in pair) and any(p in COALITION for p in pair)
        dual = bool(parties & {'LIB','LP'}) and bool(parties & {'NAT','NP'})
        if classic:
            omitted[name] = 'classic TCP already supplies the normal comparison'
        elif dual:
            omitted[name] = 'dual Liberal/National comparison outside tested addition'
        elif not parties & LABOR or not parties & COALITION:
            omitted[name] = 'no Labor/Coalition primary pair'
        else:
            eligible[name] = seat
    places = {(r['DivisionNm'], int(r['PollingPlaceID'])):
              (int(r['Australian Labor Party Votes']), int(r['Liberal/National Coalition Votes']))
              for r in csv_table(year, 'HouseTppByPollingPlace', download, sources)}
    declarations = {}
    if year >= 2016:
        for row in csv_table(year, 'HouseTppByDivisionByVoteType', download, sources):
            for label, code in DECLARATIONS.items():
                field = 'DeclarationPrePoll' if label == 'PrePoll' else label
                declarations[(row['DivisionNm'], code)] = (
                    int(row['Australian Labor Party '+field+'Votes']),
                    int(row['Liberal/National Coalition '+field+'Votes']))
    path = last_feed(feeds_root/f'{year}fed/live-snapshots/feeds')
    raw = raw_primaries(path, eligible)
    records, rejected = [], defaultdict(int)
    for name, seat in eligible.items():
        accounts = [('booth', pid, fp, places.get((name,pid))) for pid,fp in raw[name].items()]
        accounts += [('declaration', code, fp, declarations.get((name,code)))
                     for code,fp in seat['declarations'].items()]
        for kind, identifier, fp, target in accounts:
            record, reason = matched_record(f'{year}fed',name,kind,identifier,fp,target,seat['candidates'])
            if record is None:
                rejected[reason] += 1
            else:
                records.append(record)
    manifest = dict(event=EVENTS[year], feed_archive=path.name,
        feed_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), sources=sources,
        eligible_seats=sorted(eligible), omitted_seats=omitted,
        accepted_records=len(records), rejected_records=dict(rejected))
    return records, manifest


def serialize(records):
    stream = io.StringIO(newline='')
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(HEADER)
    writer.writerows(sorted(records, key=lambda r:(r[0],r[1],r[2],r[3])))
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--feeds-root', type=Path, default=ROOT/'forecasts')
    parser.add_argument('--output-dir', type=Path, default=ANALYSIS/'Data')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    records, provenance = [], {}
    for year in EVENTS:
        rows, sources = collect_election(year,args.feeds_root,args.download)
        records.extend(rows)
        provenance[year] = sources
        print(f'{year}: {len(rows)} matched notional accounts', flush=True)
    outputs = {'historical-notional-tpp.csv':serialize(records),
               'historical-notional-tpp-sources.json':json.dumps(provenance,indent=2,sort_keys=True)+'\n'}
    if not args.check:
        args.output_dir.mkdir(parents=True,exist_ok=True)
    for name, text in outputs.items():
        path = args.output_dir/name
        if args.check:
            if path.read_bytes() != text.encode('utf-8'):
                raise ValueError(f'Maintained output differs: {path}')
        else:
            path.write_bytes(text.encode('utf-8'))
    print(f'{"Verified" if args.check else "Wrote"} {len(records)} accounts; no forecast fitting performed.')


if __name__ == '__main__':
    main()

"""Record the geographic predictors, final booth counts and first reported returns.

Run this only to rebuild the evidence from the source files. The analysis can
be repeated offline once the operator has recorded the South Australian returns.
"""

import argparse
import json
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

from evidence import (FIRST_RETURNS, HERE, SA_COORDS, VIC_BOOK, key, save_rows, source,
                      south_australia, victoria)


def count_at_update(data, roster):
    """Read first preferences published so far at named ordinary and early booths.

    The same venue can issue ballots for several districts. Match the district
    as well as the name. The XML candidate tables exclude informal ballots.
    Missing or zero returns at a rostered booth mean it has supplied no usable
    party count at this time. All candidates' published ballots form the base.
    """
    root = ET.fromstring(data)
    updated = root.findtext('{*}last_updated')
    if not updated:
        raise ValueError('ECSA snapshot has no source time')
    if updated < '2026-03-21 18:00:00':
        return updated, {}
    counts = defaultdict(lambda: [0, 0])
    listed = set()
    for district in root.findall('.//{*}district'):
        seat = district.findtext('{*}district_name')
        candidates = district.findall('{*}first_preferences/{*}candidate')
        if sum(c.findtext('{*}affiliation') == 'ONP' for c in candidates) != 1:
            raise ValueError(f'Missing or ambiguous One Nation candidate in the live feed for {seat}')
        for candidate in candidates:
            is_on = candidate.findtext('{*}affiliation') == 'ONP'
            for booth in candidate.findall('{*}polling_places/{*}polling_place'):
                name = booth.findtext('{*}polling_place_name')
                identity = seat, key(name)
                number = int(booth.findtext('{*}ballot_papers'))
                if is_on:
                    listed.add(identity)
                if number and identity in roster:
                    mode, published_name = roster[identity]
                    pair = counts[seat, mode, published_name]
                    pair[1] += number
                    if is_on:
                        pair[0] += number
    if roster.keys() - listed:
        raise ValueError(f'Booth roster differs in update {updated}')
    return updated, counts


def record_first_returns(folder, rows, manifest):
    """Capture the first observed update with one or three substantial booths.

    At least 100 formal ballots at a booth qualifies it for the threshold.
    Include *every* positive return in the qualifying update, including ties,
    small booths and partial counts. Only booths still on zero are targets.
    The threshold limits the influence of the first handful of ballots. The
    final state outcomes play no part in selecting an update or a return.
    """
    roster = {(r['seat'], key(r['booth'])): (r['mode'], r['booth']) for r in rows}
    if len(roster) != len(rows):
        raise ValueError('Ambiguous seat/name in the replay roster')
    files = [p for p in sorted(folder.glob('el2026260321??????.xml'))
             if p.stem[-6:] >= '180000']
    if not files:
        raise FileNotFoundError(f'No SA election-night captures in {folder}')
    seen, records = set(), []
    for path in files:
        updated, counts = count_at_update(source(path, manifest, path.name), roster)
        groups = defaultdict(list)
        for (seat, mode, name), (on, formal) in counts.items():
            groups[seat, mode].append((name, on, formal))
        for (seat, mode), booths in sorted(groups.items()):
            substantial = sum(formal >= 100 for _, _, formal in booths)
            for stage in ((1, 3) if mode == 'ordinary' else (1,)):
                identity = seat, mode, stage
                if substantial < stage or identity in seen:
                    continue
                seen.add(identity)
                for name, on, formal in sorted(booths):
                    records.append(dict(seat=seat, mode=mode, stage=stage, capture=path.name, source_time=updated,
                                        booth=name, reported_on=on, reported_formal=formal))
    if not records:
        raise ValueError('No substantial first-preference reports matched the booth roster')
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--feeds', type=Path, required=True, help='Directory of SA polling-night XML captures')
    parser.add_argument('--sa-places', type=Path, default=HERE / 'raw' / SA_COORDS)
    parser.add_argument('--vic-venues', type=Path, default=HERE.parent / '2026vic-early-centres/raw' / VIC_BOOK)
    parser.add_argument('--out', type=Path, default=HERE)
    args = parser.parse_args()

    historical, operational, captures = {}, {}, {}
    sa = south_australia(args.sa_places, historical)
    vic = victoria(args.vic_venues, operational)
    returns = record_first_returns(args.feeds, sa, captures)
    save_rows(args.out / 'sa-booths.csv', sa)
    save_rows(args.out / FIRST_RETURNS, returns)
    save_rows(args.out / 'operator/vic-booths.csv', vic)
    path = args.out / 'sources.json'
    path.write_text(json.dumps(dict(sa=historical, victoria=operational, captures=captures),
                               sort_keys=True, indent=2) + '\n', encoding='utf-8')
    print(f'Recorded {len(sa)} SA booth rows, {len(captures)} election-night captures '
          f'and {len(vic)} Victorian district/venue rows.')


if __name__ == '__main__':
    main()

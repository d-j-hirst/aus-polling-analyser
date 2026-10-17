"""Refresh the AEC TPP input used to fill gaps in historical seat swing analysis.

By default, read already cached public downloads. --download permits missing
downloads; --output-dir writes a review copy instead of replacing maintained
inputs. This performs no fitting and does not change election result caches.
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys

ANALYSIS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ANALYSIS))
from lib.elections.historical_tpp import parse_aec_tpp, apply_reviewed_corrections
from category_sources import fetch

EVENTS = {2007: 13745, 2010: 15508, 2013: 17496, 2016: 20499,
          2019: 24310, 2022: 27966, 2025: 31496}


def collect(download=False):
    records, sources = [], {}
    for year, event in EVENTS.items():
        url = f'https://results.aec.gov.au/{event}/Website/Downloads/HouseTppByDivisionDownload-{event}.csv'
        raw = fetch(url, download=download)
        results = apply_reviewed_corrections(year, parse_aec_tpp(raw.decode('utf-8-sig')))
        if len(results) < 150:
            raise ValueError(f'Incomplete division table for {year}.')
        sources[year] = dict(url=url, sha256=hashlib.sha256(raw).hexdigest(), divisions=len(results))
        records.extend((year, seat, value.percent, value.swing) for seat, value in sorted(results.items()))
    return records, sources


def write_history(directory, records, sources):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / 'federal-tpp-history.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(('year', 'seat', 'alp_percent', 'alp_swing'))
        writer.writerows(records)
    (directory / 'federal-tpp-history-sources.json').write_text(
        json.dumps(sources, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--output-dir', type=Path, default=ANALYSIS / 'Data')
    args = parser.parse_args()
    records, sources = collect(args.download)
    write_history(args.output_dir, records, sources)
    print(f'Wrote {len(records)} division TPP results from {len(sources)} elections to {args.output_dir}')


if __name__ == '__main__':
    main()

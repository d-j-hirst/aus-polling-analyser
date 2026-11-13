"""Prepare the Victorian One Nation district trial from retained AEC data.

The forecast uses these counts to estimate differences in support between
districts of the same geographic type. Each record contains the actual One
Nation and formal first-preference votes from mapped election-day booths in
2025. No current statewide vote level or betting price enters the input.
"""

import argparse
import json
import math
from pathlib import Path
from tempfile import NamedTemporaryFile

from lib.turnout.aec_live import NS, booth_metadata, read_house, xml_root
from scripts.elections.federal_state import parse_booth_mapping


ANALYSIS = Path(__file__).resolve().parents[2]
REPOSITORY = ANALYSIS.parent
AEC_EVENT = '31496'
FINAL_RESULTS = (
    REPOSITORY / 'forecasts/2025fed/live-snapshots/feeds'
    / 'aec-mediafeed-Detailed-Light-31496-20250616104059.zip'
)
PRELOAD = (
    REPOSITORY / 'downloads'
    / 'ftp$$$mediafeed$aec$gov$au$31496$Detailed$Preload$'
      'aec-mediafeed-Detailed-Preload-31496-20250501101148$zip.xml'
)


def one_nation_candidates(preload_path):
    """Identify the current One Nation candidate IDs in each federal division."""
    root = xml_root(preload_path)
    event = root.find('a:Results/e:EventIdentifier', NS)
    if event is None or event.get('Id') != AEC_EVENT:
        raise ValueError('The candidate preload must be from the 2025 federal election.')

    candidates = {}
    for contest in root.findall('a:Results/a:Election/a:House/a:Contests/a:Contest', NS):
        division = contest.findtext('e:ContestIdentifier/e:ContestName', namespaces=NS)
        candidates[division] = {
            candidate.find('e:CandidateIdentifier', NS).get('Id')
            for candidate in contest.findall('a:FirstPreferences/a:Candidate', NS)
            if (affiliation := candidate.find('e:AffiliationIdentifier', NS)) is not None
            and affiliation.get('ShortCode') == 'ON'
            and candidate.get('Ghost') != 'true'
        }
    return candidates


def district_booth_counts(mapping, house, candidates):
    """Sum the two FP counts for each district using its authored booth map."""
    # Match by federal division and booth name together. Two federal divisions
    # may have booths with the same name, with separate ballots and candidates.
    mapped_keys = set().union(*mapping.values())
    if len(mapped_keys) != sum(len(keys) for keys in mapping.values()):
        raise ValueError('The booth map assigns a federal booth to more than one district.')
    booths = {}
    for booth in house['booths'].values():
        key = (booth['seat_name'], booth['name'])
        if key in mapped_keys:
            if key in booths:
                raise ValueError(f'More than one AEC booth matches {key}.')
            booths[key] = booth

    districts = {}
    for district, keys in mapping.items():
        one_nation_votes = formal_votes = 0
        for key in sorted(keys):
            if key not in booths:
                raise ValueError(f'{district} maps to a booth absent from the AEC results: {key}.')
            booth = booths[key]
            # Election-day booths have a more local catchment than the early
            # centres in the same map. Skip early, hospital and mobile votes.
            if booth['classification'] != 'Normal' or booth['counted'] == 0:
                continue
            candidate_ids = candidates.get(booth['seat_name'], set())
            if not candidate_ids:
                raise ValueError(f'{district} maps to a division without a One Nation candidate: {key}.')
            one_nation_votes += sum(
                vote['value'] for vote in booth['candidate_votes']
                if vote['candidate_id'] in candidate_ids
            )
            formal_votes += booth['counted']
        if not formal_votes:
            raise ValueError(f'{district} has no mapped election-day formal votes.')
        districts[district] = dict(one_nation_votes=one_nation_votes, formal_votes=formal_votes)
    return districts


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--results', type=Path, default=FINAL_RESULTS,
                        help='Final 2025 federal detailed results XML or ZIP')
    parser.add_argument('--preload', type=Path, default=PRELOAD,
                        help='2025 federal candidate preload XML')
    parser.add_argument('--polling-places', type=Path,
                        help='Polling places preload XML, normally beside the candidate preload')
    parser.add_argument('--transfer-weight', type=float, default=0.5,
                        help='Fraction of each local log-odds deviation to use, from 0 to 1')
    args = parser.parse_args(argv)
    if not math.isfinite(args.transfer_weight) or not 0 <= args.transfer_weight <= 1:
        parser.error('--transfer-weight must be between 0 and 1.')

    folder = ANALYSIS / 'Federal-State'
    mapping_path = folder / 'booths-2026vic.txt'
    polling_places = args.polling_places or args.preload.with_name(
        f'{args.preload.stem}@polling_places.xml'
    )
    try:
        mapping = parse_booth_mapping(mapping_path)
        results = xml_root(args.results).find('a:Results', NS)
        if results is None or results.get('Phase') != 'FinalResults':
            raise ValueError('Use an AEC detailed results capture from the final count.')
        candidates = one_nation_candidates(args.preload)
        metadata = booth_metadata(polling_places, AEC_EVENT)
        house = read_house(args.results, metadata, AEC_EVENT)
        districts = district_booth_counts(mapping, house, candidates)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    output = folder / '2026vic-one-nation-fp.json'
    document = dict(election='2026vic', federal_event=AEC_EVENT,
                    transfer_weight=args.transfer_weight, districts=districts,
                    source_results=args.results.name, source_mapping=mapping_path.name)
    # Preparation reads the complete file at the start of a run. Publish a
    # finished replacement so an interrupted write cannot leave half an input.
    temporary = None
    try:
        with NamedTemporaryFile('w', dir=folder, encoding='utf-8', newline='\n',
                                suffix='.tmp', delete=False) as handle:
            temporary = Path(handle.name)
            json.dump(document, handle, indent=2, sort_keys=True)
            handle.write('\n')
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f'Wrote {len(districts)} districts to {output}.')


if __name__ == '__main__':
    main()

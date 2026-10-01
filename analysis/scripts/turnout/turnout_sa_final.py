"""Acquire reviewed SA 2026 final turnout counts from ECSA's results website.

The public website uses separate election-status, district-structure and vote
JSON endpoints. This adapter combines those sources into a complete category
partition while retaining the existing pre-election operational observations.
Raw source revisions are retained locally so a later ECSA update is reproducible.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
from urllib.request import Request, urlopen

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.shared import turnout_data


ELECTION_CODE = '2026sa'
ELECTION_DATE = '2026-03-21'
ADAPTER_ID = 'ecsa-final-results-json-v1'
SOURCE_ID = 'ecsa-2026-assembly-final-json'
BASE_URL = 'https://apim-ecsa-production.azure-api.net/results-display/'
SOURCE_URLS = {
    'elections': BASE_URL + 'ElectionDates',
    'static': BASE_URL + 'HAStatic/' + ELECTION_DATE,
    'results': BASE_URL + 'HAChange/' + ELECTION_DATE + '/0',
}
REVIEW_URL = 'https://ecsa.sa.gov.au/se2026news/se2026-results-review-complete'
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data/Turnout'
SNAPSHOT_DIRECTORY = REPOSITORY_DIRECTORY / 'downloads/turnout/2026sa'

# ECSA separates ordinary polling places, EVCs and mobile teams in its source
# structure. Batch labels below distinguish absent ordinary voting from the
# separate declaration process, even where the current label says Declaration.
BOOTH_CATEGORIES = {
    'PB': 'election_day_ordinary', 'S1': 'election_day_ordinary',
    'S2': 'election_day_ordinary', 'S3': 'election_day_ordinary',
    'PP': 'early_in_person', 'P1': 'early_in_person',
    'P2': 'early_in_person', 'P3': 'early_in_person',
    'MT': 'mobile_or_institution',
}
DECLARATION_CATEGORIES = {
    'Postal': 'postal',
    'Polling Day': 'provisional',
    'Early Voting': 'declaration_early',
    'Early Voting Declaration Votes': 'declaration_early',
    'Electoral Visitor & Mobile Polling': 'mobile_or_institution',
    'Telephone/Interstate/Overseas Declaration Votes': 'other',
}
ABSENT_CATEGORIES = {'Polling Day': 'absent', 'Early Voting': 'early_in_person'}


def count(value, label):
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise turnout_data.TurnoutDataError('{} is not a nonnegative integer count'.format(label))
    return value


def batch_category(label, categories):
    """Remove only the published batch suffix, preserving the voting-mode label."""
    base = re.sub(r'\s+-\s+(?:Absent\s+)?Declaration\s+\d+$', '', label)
    if base not in categories:
        raise turnout_data.TurnoutDataError('Unrecognized ECSA vote category: {}'.format(label))
    return categories[base]


def source_hashes(raw):
    return {name: hashlib.sha256(data).hexdigest() for name, data in sorted(raw.items())}


def build_dataset(raw):
    """Normalize exact first-preference counts and check the source's own totals.

    Ordinary/declaration candidate summaries omit absent-ordinary votes. Those
    distinct batches are added once. TCP and preference-distribution stages do
    not supply turnout totals and are deliberately outside this calculation.
    """
    payloads = {name: json.loads(data) for name, data in raw.items()}
    definitions = [e for e in payloads['elections']['elections'] if e['electionDate'] == ELECTION_DATE]
    results, static = payloads['results'], payloads['static']
    if (len(definitions) != 1 or definitions[0]['electionStatus'] != 'final'
            or results['electionDate'] != ELECTION_DATE or results['electionStatus'] != 'final'
            or static['electionDate'] != ELECTION_DATE):
        raise turnout_data.TurnoutDataError('ECSA sources do not identify reviewed final SA 2026 results.')
    structures = {d['districtName']: d for d in static['districts']}
    districts = {d['districtId']: d for d in results['districts']}
    if (len(structures) != 47 or len(districts) != 47
            or len(static['districts']) != 47 or len(results['districts']) != 47
            or structures.keys() != districts.keys()):
        raise turnout_data.TurnoutDataError('ECSA final sources must identify the same 47 districts.')

    hashes = source_hashes(raw)
    notes = (
        'Reviewed final first-preference counts published by the ECSA results website. '
        'Source lastUpdated={}; dataVersion={}. District totals include polling places, '
        'declaration batches and absent-ordinary batches exactly once. Early in-person '
        'includes named EVCs and early absent-ordinary votes; early declarations remain '
        'separate. Telephone/interstate/overseas is retained as a combined other category. '
        'ECSA acknowledges residual discrepancies between some count stages; TCP and '
        'preference-distribution totals are not substituted for first preferences. Review: {}. '
    ).format(results['lastUpdated'], results['dataVersion'], REVIEW_URL)
    notes += ' '.join('{} URL={} SHA256={}.'.format(name, SOURCE_URLS[name], digest)
                      for name, digest in hashes.items())
    dataset = turnout_data.TurnoutDataset(
        elections=[turnout_data.ElectionDefinition(ELECTION_CODE, ELECTION_DATE, 'sa')],
        sources=[turnout_data.SourceDefinition(
            SOURCE_ID, ELECTION_CODE, 'Electoral Commission of South Australia',
            SOURCE_URLS['results'], ADAPTER_ID, 'final', 'ecsa-2026-detailed-vote-types-v1', notes)],
    )
    for name, district in sorted(districts.items()):
        structure = structures[name]
        booth_names = {p['pollingPlaceName']: p for p in structure['pollingPlaces']}
        if len(booth_names) != len(structure['pollingPlaces']):
            raise turnout_data.TurnoutDataError('{} repeats a polling-place identity.'.format(name))
        if {p['pollingPlaceName'] for p in district['pollingPlaces']} != booth_names.keys():
            raise turnout_data.TurnoutDataError('{} polling-place identities differ.'.format(name))
        grouped = defaultdict(lambda: [0, 0])
        ordinary_formal, declaration_formal, declaration_informal = 0, 0, 0
        for booth in district['pollingPlaces']:
            booth_type = booth['pollingPlaceTypeIdName']
            if booth_type not in BOOTH_CATEGORIES:
                raise turnout_data.TurnoutDataError('Unrecognized ECSA booth type: {}'.format(booth_type))
            formal = sum(count(c['formalVotes'], name + ' booth formal') for c in booth['pollingCandidates'])
            informal = count(booth['informalVotes'], name + ' booth informal')
            category = BOOTH_CATEGORIES[booth_type]
            label = booth_names[booth['pollingPlaceName']]['pollingPlaceType']
            grouped[(label, category)][0] += formal
            grouped[(label, category)][1] += informal
            ordinary_formal += formal

        for section, label_field, category_map in (
                ('declarations', 'declarationType', DECLARATION_CATEGORIES),
                ('absentOrdinary', 'absentOrdinaryType', ABSENT_CATEGORIES)):
            for batch in district[section]:
                label = batch[label_field]
                formal = count(batch['formalVotes'], name + ' batch formal')
                informal = count(batch['informalVotes'], name + ' batch informal')
                if sum(count(c['votes'], name + ' candidate batch') for c in batch['candidateVotes']) != formal:
                    raise turnout_data.TurnoutDataError('{} {} candidate votes do not sum to the batch total.'.format(name, label))
                grouped[(label, batch_category(label, category_map))][0] += formal
                grouped[(label, batch_category(label, category_map))][1] += informal
                if section == 'declarations':
                    declaration_formal += formal
                    declaration_informal += informal
        # These independent source summaries check ingestion without imposing
        # stale hardcoded statewide controls on a source ECSA can revise later.
        if (ordinary_formal != sum(count(c['ordinaryVotes'], name) for c in district['candidates'])
                or declaration_formal != sum(count(c['declarationVotes'], name) for c in district['candidates'])
                or declaration_informal != count(district['informalDeclarationVotes'], name)):
            raise turnout_data.TurnoutDataError('{} candidate/declaration summaries do not reconcile.'.format(name))
        formal, informal = (sum(values[index] for values in grouped.values()) for index in (0, 1))
        dataset.seat_totals.append(turnout_data.SeatTotal(
            ELECTION_CODE, name, SOURCE_ID, count(structure['districtEnrolled'], name + ' enrolment'),
            formal, informal, formal + informal, source_seat_id=name, subdivision='sa'))
        for (label, category), (formal, informal) in sorted(grouped.items()):
            dataset.vote_types.append(turnout_data.VoteTypeRecord(
                ELECTION_CODE, name, SOURCE_ID, 'final-ballot-vote-types', label,
                category, formal, informal, formal + informal, 'complete', 'sum_official_rows'))
    dataset.validate()
    return dataset


def merge_dataset(existing, final):
    """Replace this final source's rows while preserving pre-election evidence."""
    if existing.elections != final.elections:
        raise turnout_data.TurnoutDataError('Existing dataset does not identify SA 2026.')
    replaced = {s.source_id for s in existing.sources if s.adapter == ADAPTER_ID} | {SOURCE_ID}
    for field in ('sources', 'seat_totals', 'vote_types', 'operational_observations'):
        retained = [r for r in getattr(existing, field) if r.source_id not in replaced]
        setattr(final, field, retained + getattr(final, field))
    final.validate()
    return final


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--election', choices=[ELECTION_CODE], default=ELECTION_CODE)
    parser.add_argument('--source-directory', type=Path, help='read retained elections.json, static.json and results.json')
    parser.add_argument('--output-directory', type=Path, default=DEFAULT_OUTPUT_DIRECTORY)
    parser.add_argument('--dry-run', action='store_true', help='download/read and validate without writing files')
    args = parser.parse_args(argv)
    raw = {}
    for name, url in SOURCE_URLS.items():
        if args.source_directory:
            raw[name] = (args.source_directory / (name + '.json')).read_bytes()
        else:
            request = Request(url, headers={'User-Agent': 'AEF turnout research (https://www.aeforecasts.com/)'})
            with urlopen(request, timeout=60) as response:
                raw[name] = response.read()
    final = build_dataset(raw)
    output = args.output_directory / (ELECTION_CODE + '.json')
    if output.exists():
        final = merge_dataset(turnout_data.load_dataset(output), final)
    results = json.loads(raw['results'])
    print('{}: 47 districts; {:,} enrolled; {:,} formal; {:,} informal; {:,} ballots.'.format(
        ELECTION_CODE, *(sum(getattr(r, field) for r in final.seat_totals)
                        for field in ('enrolment', 'formal_votes', 'informal_votes', 'total_ballots'))))
    print('ECSA lastUpdated={}; dataVersion={}.'.format(results['lastUpdated'], results['dataVersion']))
    if args.dry_run:
        print('Dry run: no files written.')
        return 0
    # The version plus combined source hash retains even a correction made
    # without incrementing dataVersion. Snapshot names contain no machine paths.
    revision = hashlib.sha256(json.dumps(source_hashes(raw), sort_keys=True).encode()).hexdigest()[:12]
    snapshot = SNAPSHOT_DIRECTORY / 'version-{}-{}'.format(results['dataVersion'], revision)
    snapshot.mkdir(parents=True, exist_ok=True)
    for name, data in raw.items():
        (snapshot / (name + '.json')).write_bytes(data)
    turnout_data.write_dataset_atomically(output, final)
    print('Wrote {}. Retained source revision: {}'.format(output, snapshot))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

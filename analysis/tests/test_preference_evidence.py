"""Check preference evidence populations, provenance and partial observations."""

import copy
import gzip
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from lib.elections.preference_evidence import (
    add_original_observation, attach_distribution, candidate_record,
    catalogue_summary, contest_record, make_catalogue, party_group,
    read_catalogue, validate_catalogue, write_catalogue,
)
from lib.elections.preference_source_counts import SourceCache, csv_rows, werriwa_counts
from scripts.diagnostics.prepare_preference_evidence import (
    DATA_DIRECTORY, check_maintained, main, maintained_inputs,
)


def registry():
    source = dict(url='https://example.com/official-counts.csv', sha256='1' * 64, bytes=1)
    return dict(schema_version=1, files={'primary': source, 'flow': source, 'copy': source},
                recommendations=[], candidate_contexts=[], supplemental_publications={},
                coverage_limits=[])


def original_contest():
    candidates = [candidate_record(key, key, group, votes) for key, group, votes in [
        ('A', 'ALP', 300), ('L', 'LP', 300), ('G', 'GRN', 200), ('O', 'ON', 200)]]
    contest = contest_record('2022fed', '1', 'Hill', 'VIC', candidates, ['primary'])
    for donor, destinations, kind in [
        ('G', [('A', 160), ('L', 40)], 'tcp'),
        ('O', [('A', 100), ('L', 100)], 'tcp'),
        ('G', [('A', 180), ('O', 20)], 'alternative_tpp'),
        ('L', [('A', 100), ('O', 200)], 'alternative_tpp'),
    ]:
        add_original_observation(contest, donor,
                                 [dict(candidate=c, votes=v) for c, v in destinations],
                                 ['flow'], kind=kind)
    return contest


def distribution_contest(complete=True):
    candidates = [candidate_record(key, key, group, votes) for key, group, votes in [
        ('A', 'ALP', 300), ('L', 'LP', 400), ('G', 'GRN', 200), ('F', 'FFP', 100)]]
    contest = contest_record('2026sa', '2', 'Town', 'SA', candidates, ['primary'])
    stages = [dict(round=1, excluded_candidate='F', pile_votes=100,
                   transfers=[dict(candidate=c, votes=v) for c, v in [('A', 60), ('G', 20), ('L', 20)]],
                   progressive_totals=dict(A=360, L=420, G=220, F=0))]
    if complete:
        stages.append(dict(round=2, excluded_candidate='G', pile_votes=220,
                           transfers=[dict(candidate='A', votes=150), dict(candidate='L', votes=70)],
                           progressive_totals=dict(A=510, L=490, G=0, F=0)))
        continuing = [dict(candidate='A', votes=510), dict(candidate='L', votes=490)]
    else:
        continuing = [dict(candidate=c, votes=v) for c, v in [('A', 360), ('L', 420), ('G', 220)]]
    attach_distribution(contest, stages, continuing, ['flow'])
    return contest


class PreferenceEvidenceTests(unittest.TestCase):
    def test_same_original_ballots_are_linked_across_pairs(self):
        catalogue = make_catalogue([original_contest()], registry(), {})
        contest = catalogue['contests'][0]
        self.assertEqual(len(contest['overlapping_source_views']), 1)
        self.assertEqual(len(next(iter(contest['overlapping_source_views'].values()))), 2)
        summary = catalogue_summary(catalogue)
        self.assertEqual(summary['accepted_original_source_views'], 4)
        self.assertEqual(summary['original_ballot_groups'], 3)

    def test_duplicate_source_table_is_one_observation(self):
        contest = original_contest()
        add_original_observation(contest, 'G', [dict(candidate='L', votes=40), dict(candidate='A', votes=160)],
                                 ['copy'])
        catalogue = make_catalogue([contest], registry(), {})
        self.assertEqual(len(contest['original_source_observations']), 4)
        observation = next(o for o in contest['original_source_observations']
                           if o['source_candidate'] == 'G' and o['views'] == ['tcp'])
        self.assertEqual(observation['source_refs'], ['copy', 'flow'])
        self.assertEqual(catalogue_summary(catalogue)['accepted_original_source_views'], 4)

    def test_conflicting_duplicate_fails(self):
        with self.assertRaisesRegex(ValueError, 'Conflicting duplicate'):
            add_original_observation(original_contest(), 'G',
                                     [dict(candidate='A', votes=150), dict(candidate='L', votes=50)], ['copy'])

    def test_primary_strength_uses_whole_district_denominator(self):
        catalogue = make_catalogue([original_contest()], registry(), {})
        contest = catalogue['contests'][0]
        green = next(c for c in contest['candidates'] if c['id'] == 'G')
        self.assertEqual(contest['formal_fp_votes'], 1000)
        self.assertEqual(green['fp_fraction'], .2)
        green['fp_fraction'] = .5
        with self.assertRaisesRegex(ValueError, 'source-primary denominator'):
            validate_catalogue(catalogue)

    def test_original_vote_conservation_and_complete_population(self):
        catalogue = make_catalogue([original_contest()], registry(), {})
        broken = copy.deepcopy(catalogue)
        broken['contests'][0]['original_source_observations'][0]['destinations'][0]['votes'] += 1
        with self.assertRaisesRegex(ValueError, 'conserve votes'):
            validate_catalogue(broken)
        contest = original_contest()
        contest['original_source_observations'] = [o for o in contest['original_source_observations']
                                                   if o['source_candidate'] != 'O']
        with self.assertRaisesRegex(ValueError, 'incomplete tcp original-source population'):
            make_catalogue([contest], registry(), {})

    def test_mixed_pile_is_not_an_original_green_measurement(self):
        catalogue = make_catalogue([distribution_contest()], registry(), {})
        first, second = catalogue['contests'][0]['distribution']['stages']
        self.assertEqual(first['evidence_kind'], 'original_source_exclusion')
        self.assertEqual(second['evidence_kind'], 'mixed_exclusion')
        self.assertEqual(second['inherited_votes'], 20)
        self.assertEqual(second['denominator_kind'], 'excluded_pile')
        self.assertEqual(second['transfers'][0]['original_source_vote_bounds'], [130, 150])
        self.assertEqual(catalogue_summary(catalogue)['accepted_original_source_views'], 0)

    def test_progressive_totals_and_bounds_are_checked(self):
        catalogue = make_catalogue([distribution_contest()], registry(), {})
        broken = copy.deepcopy(catalogue)
        broken['contests'][0]['distribution']['stages'][1]['progressive_totals']['A'] -= 1
        with self.assertRaisesRegex(ValueError, 'progressive totals'):
            validate_catalogue(broken)
        broken = copy.deepcopy(catalogue)
        broken['contests'][0]['distribution']['stages'][1]['transfers'][0]['original_source_vote_bounds'] = [150, 150]
        with self.assertRaisesRegex(ValueError, 'arithmetic bounds'):
            validate_catalogue(broken)

    def test_count_stopping_with_three_does_not_create_final_pair(self):
        catalogue = make_catalogue([distribution_contest(False)], registry(), {})
        distribution = catalogue['contests'][0]['distribution']
        self.assertFalse(distribution['completed_to_pair'])
        self.assertEqual(len(distribution['continuing']), 3)
        self.assertEqual(len(distribution['stages']), 1)

    def test_unknown_recommendations_are_not_filled_from_results(self):
        catalogue = make_catalogue([original_contest()], registry(), {})
        self.assertTrue(all(c['recommendation'] == dict(status='unknown')
                            for c in catalogue['contests'][0]['candidates']))

    def test_verified_recommendation_matches_exact_context(self):
        sources = registry()
        sources['recommendations'] = [dict(election='2022fed', seat='Hill', source_party='LIB',
                                          order_among_targets=['GRN', 'ALP'], source_file='copy',
                                          page=1, verification='manual_reading_of_registered_card')]
        catalogue = make_catalogue([original_contest()], sources, {})
        candidate = next(c for c in catalogue['contests'][0]['candidates'] if c['id'] == 'L')
        self.assertEqual(candidate['recommendation']['candidate_order'], ['G', 'A'])
        sources['recommendations'][0]['seat'] = 'Another seat'
        with self.assertRaisesRegex(ValueError, 'did not match'):
            make_catalogue([original_contest()], sources, {})

    def test_independents_keep_separate_identities(self):
        first = candidate_record('1', 'One independent', 'IND', 100)
        second = candidate_record('2', 'Another independent', 'IND', 200)
        self.assertEqual(first['party_group'], second['party_group'])
        self.assertNotEqual(first['id'], second['id'])
        self.assertEqual(first['preference_class']['value'], 'unspecified')
        self.assertEqual(candidate_record('3', 'No affiliation', '', 0)['candidate_type'], 'unaffiliated')

    def test_aliases_keep_nationals_distinct_and_raw_codes_available(self):
        self.assertEqual(party_group('LNQ'), 'LIB')
        self.assertEqual(party_group('NP'), 'NAT')
        self.assertEqual(party_group('GVIC'), 'GRN')
        self.assertEqual(party_group('FFPA'), 'FFP')
        self.assertEqual(candidate_record('1', 'Merged candidate', 'LNQ', 100)['party_code'], 'LNQ')

    def test_quarantine_preserves_counts_and_overlap_links(self):
        contest = original_contest()
        observation = next(o for o in contest['original_source_observations']
                           if o['source_candidate'] == 'G' and o['views'] == ['alternative_tpp'])
        observation.update(status='quarantined', exclusion_reason='Not a verified completed count')
        summary = catalogue_summary(make_catalogue([contest], registry(), {}))
        self.assertEqual(summary['accepted_original_source_views'], 3)
        self.assertEqual(summary['quarantined_original_source_views'], 1)
        self.assertEqual(summary['overlapping_accepted_ballot_groups'], 0)

    def test_missing_ballot_links_fail(self):
        catalogue = make_catalogue([original_contest()], registry(), {})
        catalogue['contests'][0]['overlapping_source_views'] = {}
        with self.assertRaisesRegex(ValueError, 'overlapping-ballot links'):
            validate_catalogue(catalogue)

    def test_compressed_catalogue_roundtrip_is_stable(self):
        catalogue = make_catalogue([original_contest(), distribution_contest()], registry(), {})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'evidence.json.gz'
            write_catalogue(path, catalogue)
            original = path.read_bytes()
            self.assertEqual(read_catalogue(path), catalogue)
            write_catalogue(path, catalogue)
            self.assertEqual(original, path.read_bytes())


class PreferenceSourceReadersTests(unittest.TestCase):
    def test_cache_checks_content_and_filename_without_network(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'counts.csv'
            raw = b'count'
            path.write_bytes(raw)
            sources = dict(files={'counts.csv': dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())})
            cache = SourceCache(directory, sources)
            self.assertEqual(cache.read('counts.csv'), raw)
            path.write_bytes(b'wrong')
            with self.assertRaisesRegex(ValueError, 'hash changed'):
                cache.read('counts.csv')
            with self.assertRaisesRegex(ValueError, 'Unregistered'):
                cache.read('../counts.csv')

    def test_csv_repair_is_specific_and_retains_missing_fields(self):
        header = b'StateAb,a,b,c,d,party,e\n'
        malformed = header + b'VIC,1,2,3,4,Shooters, Fishers and Farmers Party,6\n'
        with self.assertRaisesRegex(ValueError, 'Unexpected CSV columns'):
            csv_rows(malformed, 'another.csv')
        rows = csv_rows(malformed, 'aec-2025-nonclassic-tpp-flow.csv')
        self.assertEqual(rows[0]['party'], 'Shooters, Fishers and Farmers Party')
        self.assertEqual(csv_rows(header + b'VIC,1,2,3,4,,6\n')[0]['party'], '')

    def test_legacy_container_changes_preserve_identity_but_payload_changes_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            payload = b'original official table'
            digest = hashlib.sha256(payload).hexdigest()
            metadata = dict(verification='uncompressed_member_hashes', bytes=0, sha256='0' * 64,
                            sha256_uncompressed=digest)
            cache = SourceCache(folder, dict(files={'member.gz': metadata}))
            for timestamp in [1, 2000]:
                raw = gzip.compress(payload, mtime=timestamp)
                (folder / 'member.gz').write_bytes(raw)
                self.assertEqual(cache.read('member.gz'), raw)
            (folder / 'member.gz').write_bytes(gzip.compress(b'different table'))
            with self.assertRaisesRegex(ValueError, 'member hash changed'):
                cache.read('member.gz')
            archive_metadata = dict(metadata, members=[dict(member='data/table.txt', sha256_uncompressed=digest)])
            cache = SourceCache(folder, dict(files={'selected.zip': archive_metadata}))
            for compression in [zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED]:
                with zipfile.ZipFile(folder / 'selected.zip', 'w', compression=compression) as archive:
                    archive.writestr('data/table.txt', payload)
                self.assertTrue(cache.read('selected.zip'))
            with zipfile.ZipFile(folder / 'selected.zip', 'w') as archive:
                archive.writestr('data/table.txt', b'different table')
            with self.assertRaisesRegex(ValueError, 'member hash changed'):
                cache.read('selected.zip')

    def test_werriwa_totals_and_previous_party_placeholders_are_not_candidates(self):
        def table(rows):
            return '<table>' + ''.join('<tr>' + ''.join(f'<td>{v}</td>' for v in row)
                                      + '</tr>' for row in rows) + '</table>'
        primary = table([
            ['Candidate', 'Party', 'Votes', '%', 'Swing (%)'],
            ['A, Ann', 'Australian Labor Party', '600', '60', '0'],
            ['B, Bob', 'Independent', '300', '30', '0'],
            ['G, Green', 'The Greens', '100', '10', '0'],
            ['......', 'Liberal', '0', '0', '0'],
            ['FORMAL', '', '1000', '90', '0'], ['INFORMAL', '', '100', '10', '0'],
            ['TOTAL', '', '1100', '', ''],
        ])
        flows = table([['Candidate', 'Party', 'A, Ann ALP', 'B, Bob IND'],
                       ['G, Green', 'The Greens', '80', '80%', '20', '20%']])
        class Cache:
            def read(self, name):
                return (primary if name == 'aec-werriwa-2005-fp.html' else flows).encode()
        contest = werriwa_counts(Cache(), dict(event='12426', flow_file='flow'))
        self.assertEqual(contest['formal_fp_votes'], 1000)
        self.assertEqual(len(contest['candidates']), 3)
        self.assertEqual(len(contest['original_source_observations']), 1)


class MaintainedPreferenceEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogue = read_catalogue(DATA_DIRECTORY / 'evidence.json.gz')

    def contest(self, election, seat):
        return next(c for c in self.catalogue['contests'] if c['election'] == election and c['seat'] == seat)

    def test_maintained_catalogue_and_coverage(self):
        check_maintained(self.catalogue)
        summary = catalogue_summary(self.catalogue)
        self.assertEqual(summary['accepted_original_source_views'], 7545)
        self.assertEqual(summary['quarantined_original_source_views'], 6)
        self.assertEqual(summary['original_ballot_groups'], 7195)
        self.assertEqual(summary['overlapping_accepted_ballot_groups'], 350)
        self.assertEqual(summary['published_distributions'], 55)
        self.assertEqual(summary['completed_to_pair_distributions'], 54)
        self.assertEqual(summary['exclusion_stages'], 345)
        self.assertEqual(sum(c['election_type'] == 'by_election' for c in self.catalogue['contests']), 25)

    def test_source_fingerprints_ignore_json_line_endings(self):
        with tempfile.TemporaryDirectory() as directory:
            for filename in ['sources.json', 'aec-alternative-tpp-extracts.json']:
                raw = (DATA_DIRECTORY / filename).read_text(encoding='utf-8')
                (Path(directory) / filename).write_bytes(raw.replace('\n', '\r\n').encode())
            self.assertEqual(maintained_inputs(Path(directory))[1], maintained_inputs(DATA_DIRECTORY)[1])

    def test_changed_context_requires_rebuilding_retained_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            for filename in ['sources.json', 'aec-alternative-tpp-extracts.json']:
                data = json.loads((DATA_DIRECTORY / filename).read_text(encoding='utf-8'))
                if filename == 'sources.json':
                    data['recommendations'][0]['page'] += 1
                (Path(directory) / filename).write_text(json.dumps(data), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'inputs changed'):
                check_maintained(self.catalogue, Path(directory))

    def test_unverified_bradfield_rows_cannot_be_promoted_to_observations(self):
        catalogue = copy.deepcopy(self.catalogue)
        bradfield = next(c for c in catalogue['contests']
                         if c['election'] == '2025fed' and c['seat'] == 'Bradfield')
        observation = next(o for o in bradfield['original_source_observations'] if o['status'] == 'quarantined')
        observation['status'] = 'accepted'
        with self.assertRaisesRegex(ValueError, 'exclusion rule'):
            validate_catalogue(catalogue)

    def test_check_does_not_require_downloads(self):
        with patch('sys.stdout', new_callable=io.StringIO):
            self.assertEqual(main(['--check']), 0)

    def test_real_partial_count_and_local_candidate_exception(self):
        brunswick = self.contest('2022vic', 'Brunswick')
        self.assertEqual(len(brunswick['distribution']['continuing']), 3)
        self.assertFalse(brunswick['distribution']['completed_to_pair'])
        for year in [2019, 2022, 2025]:
            sharkie = next(c for c in self.contest(f'{year}fed', 'Mayo')['candidates']
                           if 'SHARKIE' in c['name'])
            self.assertEqual(sharkie['preference_class']['value'], 'independent_like')
            self.assertNotEqual(sharkie['party_group'], 'IND')
        sharkie_2016 = next(c for c in self.contest('2016fed', 'Mayo')['candidates'] if 'SHARKIE' in c['name'])
        self.assertEqual(sharkie_2016['preference_class']['value'], 'unspecified')

    def test_verified_cards_do_not_spread_to_other_seats_or_years(self):
        melbourne = self.contest('2022vic', 'Melbourne')
        liberal = next(c for c in melbourne['candidates'] if c['party_group'] == 'LIB')
        self.assertEqual(liberal['recommendation']['party_order'], ['GRN', 'ALP'])
        self.assertEqual(liberal['recommendation']['registration'], 453)
        self.assertTrue(all(c['recommendation']['status'] == 'unknown'
                            for c in self.contest('2022fed', 'Melbourne')['candidates']))
        self.assertEqual(catalogue_summary(self.catalogue)['verified_recommendations'], 14)

    def test_published_liberal_recommendations_are_matched_to_specific_2025_cases(self):
        seats = ['Canberra', 'Cooper', 'Fraser', 'Grayndler', 'Griffith',
                 'Melbourne', 'Newcastle', 'Sydney', 'Wills']
        for seat in seats:
            liberal = next(c for c in self.contest('2025fed', seat)['candidates'] if c['party_group'] == 'LIB')
            advice = liberal['recommendation']
            self.assertEqual(advice['verification'], 'ABC_VERIFY_published_transcription_of_party_HTV_cards')
            self.assertLess(advice['party_order'].index('ALP'), advice['party_order'].index('GRN'))
        # The same publication year does not assign the recommendation to an
        # unreviewed seat, even where the raw source collection covers it.
        liberal = next(c for c in self.contest('2025fed', 'Kooyong')['candidates'] if c['party_group'] == 'LIB')
        self.assertEqual(liberal['recommendation']['status'], 'unknown')


if __name__ == '__main__':
    unittest.main()

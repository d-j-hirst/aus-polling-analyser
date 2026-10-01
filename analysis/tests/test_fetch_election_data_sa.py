import json
import tempfile
import unittest
from pathlib import Path

import fetch_election_data_sa as sa


FIXTURE_FP = """\
### FILE: Sample.csv ###
"Electoral Commission SA, 2022 State Election, House of Assembly First preference votes by district and polling place",,,,,,,,,,,
,Sample,,,,,,,,,,
,First Preference Votes,,,,,,Ballot Papers,,,,
,"SMITH, Ann (LIB)",,"JONES, Bob (ALP)",,"LEE, Kim (IND)",,Formal Votes,,Informal Votes,,Total Votes
Polling Place,Votes,%,Votes,%,Votes,%,Votes,%,Votes,%,
Town,505,32.5,"1,066",43.4,0,0,"1,571",97.7,36,2.3
Ordinary Votes,505,32.5,"1,066",43.4,0,0,"1,571",97.7,36,2.3
Declaration Votes,"3,753",41.5,3515,38.9,210,2.3,"7,478",97.8,200,2.2
District Total,"4,258",40.5,"4,581",43.6,210,2,"9,049",97.6,236,2.4

### FILE: Other.csv ###
"Electoral Commission SA, 2022 State Election, House of Assembly First preference votes by district and polling place",,,,,,,,,,,
,Other,,,,,,,,,,
,First Preference Votes,,,,,,Ballot Papers,,,,
,"WHITE, Tim (GRN)",,"PEDERICK, Adrian (LIB)",,Formal Votes,,Informal Votes,,Total Votes
Polling Location,Votes,%,Votes,%,Votes,%,Votes,%,
Callington,34,5.2,178,27,212,95.5,31,4.5
Polling Place Totals,34,5.2,178,27,212,95.5,31,4.5
Declaration Ballot Papers,547,5.5,"4,333",43.5,"4,880",96.3,386,3.7
District Total,581,5.4,"4,511",41.9,"5,092",96.2,417,3.8
"""

FIXTURE_TCP = """\
### FILE: Sample.csv ###
"Electoral Commission SA, 2022 State Election, House of Assembly",,,,,
Sample: Two Candidate Preferred by Polling Place,,,,,
Polling Location,"JONES, Bob (ALP)",Percentage,"SMITH, Ann (LIB)",Percentage,Total
Town,1079,0.653,575,0.347,1654
Polling Place Totals,1079,0.653,575,0.347,1654
Declaration Ballot Papers,4906,0.543,4127,0.457,9033
District Total,5985,0.561,4702,0.439,10687

### FILE: Other.csv ###
"Electoral Commission SA, 2022 State Election, House of Assembly",,,,,
Other: Two Candidate Preferred by Polling Place,,,,,
Polling Location,"PEDERICK, Adrian (LIB)",Percentage,"WHITE, Tim (GRN)",Percentage,Total Votes
Callington,178,0.84,34,0.16,212
Polling Place Totals,178,0.84,34,0.16,212
Declaration Ballot Papers,4333,0.888,547,0.112,4880
District Total,4511,0.886,581,0.114,5092
"""

SKIPPED_BOOTH_NAMES = {
    'Ordinary Votes',
    'District Total',
    'Polling Place Totals',
    'Declaration Ballot Papers',
}


class FetchElectionDataSaTests(unittest.TestCase):
    def convert_fixture(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            fp_path = directory / 'fp.csv'
            tcp_path = directory / 'tcp.csv'
            fp_path.write_text(FIXTURE_FP, encoding='utf-8')
            tcp_path.write_text(FIXTURE_TCP, encoding='utf-8')
            return sa.convert_election('2022sa', fp_path, tcp_path)

    def test_fixture_skips_totals_and_renames_declaration_votes(self):
        results = self.convert_fixture()

        self.assertEqual(list(results), ['Sample', 'Other'])
        self.assertNotIn('Frome', results)

        sample = results['Sample']
        self.assertEqual(
            sample['candidates'],
            {
                0: {'name': 'SMITH, Ann', 'party': 'LIB'},
                1: {'name': 'JONES, Bob', 'party': 'ALP'},
                2: {'name': 'LEE, Kim', 'party': 'IND'},
            },
        )
        self.assertEqual(list(sample['booths']), ['Town', 'Declaration Votes'])
        self.assertEqual(
            sample['booths']['Town']['fp'],
            {0: 505, 1: 1066, 2: 0},
        )
        self.assertEqual(
            list(sample['booths']['Town']['tcp']),
            [1, 0],
        )
        self.assertEqual(
            sample['booths']['Town']['tcp'],
            {1: 1079, 0: 575},
        )
        self.assertEqual(
            sample['booths']['Declaration Votes']['fp'][0],
            3753,
        )
        self.assertEqual(
            sample['booths']['Declaration Votes']['tcp'],
            {1: 4906, 0: 4127},
        )

        other = results['Other']
        self.assertEqual(list(other['booths']), ['Callington', 'Declaration Votes'])
        self.assertEqual(other['booths']['Callington']['fp'], {0: 34, 1: 178})
        self.assertEqual(other['booths']['Callington']['tcp'], {1: 178, 0: 34})
        for seat_info in results.values():
            for booth_name in seat_info['booths']:
                self.assertNotIn(booth_name, SKIPPED_BOOTH_NAMES)

    def test_write_matches_json_dump_style(self):
        results = self.convert_fixture()
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_path = Path(temporary_directory) / 'Booth Results' / '2022sa.json'
            sa.write_booth_results(results, output_path)
            dumped = output_path.read_text(encoding='utf-8')
        self.assertTrue(dumped.endswith('\n'))
        self.assertNotIn('\r\n', dumped)
        loaded = json.loads(dumped)
        self.assertEqual(loaded['Sample']['booths']['Town']['fp']['1'], 1066)
        self.assertEqual(list(loaded['Sample']['booths']['Town']['tcp']), ['1', '0'])

    def test_unknown_election_is_rejected(self):
        with self.assertRaisesRegex(sa.SaBoothResultsError, 'Unknown election'):
            sa.convert_election('2099sa')

    def test_real_downloads_have_expected_live_structure(self):
        fp_path, tcp_path = sa.source_paths('2022sa')
        if not fp_path.exists() or not tcp_path.exists():
            self.skipTest('ECSA 2022 concatenated CSVs are not present')

        results = sa.convert_election()
        self.assertEqual(len(results), 47)
        self.assertIn('Frome', results)
        self.assertNotIn('Ngadjuri', results)
        for seat_name, seat_info in results.items():
            with self.subTest(seat=seat_name):
                self.assertIn('Declaration Votes', seat_info['booths'])
                for booth_name, booth in seat_info['booths'].items():
                    self.assertNotIn(booth_name, SKIPPED_BOOTH_NAMES)
                    self.assertEqual(len(booth['fp']), len(seat_info['candidates']))
                    self.assertEqual(len(booth['tcp']), 2)
                    self.assertTrue(set(booth['tcp']).issubset(seat_info['candidates']))

    def test_real_output_matches_existing_json_when_present(self):
        golden_path = sa.booth_results_path()
        if not golden_path.exists():
            self.skipTest('existing 2022sa.json is not present')
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_path = Path(temporary_directory) / '2022sa.json'
            sa.write_booth_results(sa.convert_election(), output_path)
            self.assertEqual(output_path.read_bytes(), golden_path.read_bytes())


if __name__ == '__main__':
    unittest.main()

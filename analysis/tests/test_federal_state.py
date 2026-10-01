import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

try:
    import federal_state
    from bs4 import BeautifulSoup
except ModuleNotFoundError as error:
    OPTIONAL_IMPORT_ERROR = error
else:
    OPTIONAL_IMPORT_ERROR = None


def raw_results(booths):
    results = federal_state.RawResults()
    for booth_key, greens, tpp_swing, tpp_percentage, formal_votes in booths:
        results.greens_swings[booth_key] = greens
        results.tpp_swings[booth_key] = tpp_swing
        results.tpp_percentages[booth_key] = tpp_percentage
        results.vote_totals[booth_key] = formal_votes
    return results


@unittest.skipIf(
    OPTIONAL_IMPORT_ERROR is not None,
    'federal_state tests require optional retrieval dependencies: {}'.format(
        OPTIONAL_IMPORT_ERROR.name if OPTIONAL_IMPORT_ERROR else ''
    ),
)
class FederalStateConfigTests(unittest.TestCase):
    def test_requires_a_known_election(self):
        with mock.patch.object(sys, 'argv', ['federal_state.py']):
            with self.assertRaisesRegex(federal_state.ConfigError, 'must be provided'):
                federal_state.Config()

        with mock.patch.object(sys, 'argv', ['federal_state.py', '--election', '2099-sa']):
            with self.assertRaisesRegex(federal_state.ConfigError, 'Unknown election'):
                federal_state.Config()


@unittest.skipIf(
    OPTIONAL_IMPORT_ERROR is not None,
    'federal_state tests require optional retrieval dependencies: {}'.format(
        OPTIONAL_IMPORT_ERROR.name if OPTIONAL_IMPORT_ERROR else ''
    ),
)
class FederalStateResultsTests(unittest.TestCase):
    def test_fetches_raw_results_without_applying_local_adjustments(self):
        division_page = BeautifulSoup(
            '<table><tr><td class="filterDivision"><a href="division.htm">'
            'Barton</a></td><td>NSW</td></tr></table>',
            'html.parser',
        )
        seat_page = BeautifulSoup(
            '<table><tr><td headers="ppPp"><a href="booth.htm">'
            'Example Booth</a></td></tr></table>',
            'html.parser',
        )
        booth_page = BeautifulSoup(
            '<table>'
            '<tr><td headers="fpCan">Example Candidate</td>'
            '<td headers="fpPty">The Greens</td><td headers="fpSwg">1.25</td></tr>'
            '<tr><td headers="fpCan">Formal</td><td headers="fpVot">100</td></tr>'
            '<tr><td headers="tcpCan">Example Candidate</td>'
            '<td headers="tcpPty">Labor</td><td headers="tcpPct">49.0</td>'
            '<td headers="tcpSwg">2.5</td></tr>'
            '</table>',
            'html.parser',
        )
        booth = ('Barton', 'Example Booth')
        with tempfile.TemporaryDirectory() as temporary_directory:
            cache_path = Path(temporary_directory) / 'cache.pkl'
            with mock.patch.object(
                federal_state,
                'fetch_page',
                side_effect=[division_page, seat_page, booth_page],
            ), mock.patch.object(
                federal_state, 'election_filename', return_value=cache_path
            ):
                raw = federal_state.fetch_results('2027nsw')

        self.assertEqual(raw.greens_swings[booth], 1.25)
        self.assertEqual(raw.tpp_swings[booth], 2.5)
        self.assertEqual(raw.tpp_percentages[booth], 49.0)
        self.assertEqual(raw.vote_totals[booth], 100)

    def test_retains_non_comparable_booth_but_excludes_its_weight(self):
        booth = ('Kingston', 'Hallett Cove')
        raw = raw_results([(booth, 1.25, 69.85, 69.85, 776)])

        processed = federal_state.apply_local_assumptions(raw, '2026sa')

        self.assertIn(booth, processed.vote_totals)
        self.assertEqual(processed.vote_totals[booth], 0)
        self.assertEqual(processed.tpp_swings[booth], 0)

    def test_applies_manual_federal_adjustment_after_loading_raw_data(self):
        booth = ('Barton', 'Example Booth')
        raw = raw_results([(booth, 1.25, 2.5, 49.0, 100)])

        processed = federal_state.apply_local_assumptions(raw, '2027nsw')

        self.assertEqual(processed.greens_swings[booth], 1.25)
        self.assertEqual(processed.tpp_swings[booth], 3.7)
        self.assertEqual(processed.vote_totals[booth], 100)


@unittest.skipIf(
    OPTIONAL_IMPORT_ERROR is not None,
    'federal_state tests require optional retrieval dependencies: {}'.format(
        OPTIONAL_IMPORT_ERROR.name if OPTIONAL_IMPORT_ERROR else ''
    ),
)
class FederalStateMappingTests(unittest.TestCase):
    def test_mapping_parser_rejects_malformed_or_duplicate_entries(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            mapping_path = Path(temporary_directory) / 'booths.txt'
            mapping_path.write_text(
                '#Example\nFederal,Booth\nFederal,Booth\n', encoding='utf-8'
            )
            with self.assertRaisesRegex(federal_state.MappingError, 'repeats booth'):
                federal_state.parse_booth_mapping(mapping_path)

            mapping_path.write_text('Federal,Booth\n', encoding='utf-8')
            with self.assertRaisesRegex(federal_state.MappingError, 'before a state seat'):
                federal_state.parse_booth_mapping(mapping_path)

    def test_mapping_validation_rejects_missing_duplicate_and_zero_weight_data(self):
        booth = ('Federal', 'Comparable')
        processed = federal_state.Results()
        processed.greens_swings[booth] = 1.0
        processed.tpp_swings[booth] = 2.0
        processed.vote_totals[booth] = 100

        with self.assertRaisesRegex(federal_state.MappingError, 'multiple state seats'):
            federal_state.validate_mapping(
                {'State One': {booth}, 'State Two': {booth}}, processed
            )

        with self.assertRaisesRegex(federal_state.MappingError, 'missing federal booth'):
            federal_state.validate_mapping(
                {'State One': {('Federal', 'Missing')}}, processed
            )

        processed.vote_totals[booth] = 0
        with self.assertRaisesRegex(federal_state.MappingError, 'no usable'):
            federal_state.validate_mapping({'State One': {booth}}, processed)


if __name__ == '__main__':
    unittest.main()

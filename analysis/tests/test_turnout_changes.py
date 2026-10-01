"""Check the arithmetic and category safeguards used by the exploratory report."""

import unittest

from lib.shared.turnout_data import SeatTotal, VoteTypeRecord
from scripts.turnout import turnout_changes as report


def election(code, seats, categories=None, jurisdiction='fed'):
    totals = {
        name: SeatTotal(code, name, 'test', enrolled, formal, ballots - formal, ballots,
                        subdivision='nsw')
        for name, enrolled, ballots, formal in seats
    }
    records = {}
    for name, counts in (categories or {}).items():
        records[name] = [VoteTypeRecord(
            code, name, 'test', 'partition', cat, cat, formal,
            None if ballots is None else ballots - formal, ballots, 'complete')
            for cat, formal, ballots in counts]
    return report.Election(code, code[:4] + '-01-01', jurisdiction, totals, records, [])


class TurnoutChangeTests(unittest.TestCase):
    def test_aggregate_rates_use_counts_and_growth_identity(self):
        old = election('2019fed', [('A', 100, 90, 81), ('B', 900, 450, 360)])
        new = election('2022fed', [('A', 110, 99, 90), ('B', 990, 594, 540)])
        row = report.change_row(old, new, 'election', 'fed', ['A', 'B'], ['A', 'B'])
        self.assertAlmostEqual(row['previous_turnout_pct'], 54)
        self.assertAlmostEqual(row['current_turnout_pct'], 63)
        self.assertAlmostEqual(row['turnout_pct_change_pp'], 9)
        self.assertAlmostEqual(row['enrolment_growth_pct'], 10)
        expected_ratio = 1.1 * (63 / 54) * ((630 / 693) / (441 / 540))
        self.assertAlmostEqual(row['formal_votes_growth_pct'], (expected_ratio - 1) * 100)

    def test_federal_2010_break_does_not_create_false_prepoll_decline(self):
        old = election('2007fed', [('A', 100, 90, 90)], {'A': [
            ('election_day_ordinary', 60, 60), ('declaration_early', 20, 20), ('postal', 10, 10)]})
        new = election('2010fed', [('A', 100, 90, 90)], {'A': [
            ('ordinary_combined', 75, 75), ('declaration_early', 5, 5), ('postal', 10, 10)]})
        rows = report.category_rows(old, new, 'seat', 'A', ['A'], ['A'])
        combined = next(r for r in rows if r['category'] == 'ordinary_and_all_prepoll')
        self.assertAlmostEqual(combined['formal_share_pct_change_pp'], 0)
        self.assertEqual(report.category_rows(old, new, 'seat', 'A', ['A'], ['A'], True), [])

    def test_missing_partition_is_excluded_from_share_denominator(self):
        old = election('2018vic', [('A', 100, 90, 90), ('B', 100, 90, 90)],
                       {'A': [('postal', 90, 90)]}, 'vic')
        new = election('2022vic', [('A', 100, 90, 90), ('B', 100, 90, 90)],
                       {'A': [('postal', 90, 90)]}, 'vic')
        row = report.category_rows(old, new, 'election', 'vic', ['A', 'B'], ['A', 'B'])[0]
        self.assertEqual(row['previous_formal_share_pct'], 100)
        self.assertEqual(row['previous_coverage'], 1)
        self.assertIn('incomplete category coverage', row['notes'])
        self.assertEqual(report.category_rows(old, new, 'seat', 'B', ['B'], ['B']), [])

    def test_zero_category_ballots_are_not_a_formality_rate(self):
        old = election('2019fed', [('A', 100, 90, 90)],
                       {'A': [('ordinary_combined', 90, 90), ('postal', 0, 0)]})
        new = election('2022fed', [('A', 100, 90, 90)],
                       {'A': [('ordinary_combined', 90, 90), ('postal', 0, 0)]})
        row = next(r for r in report.category_rows(old, new, 'seat', 'A', ['A'], ['A'])
                   if r['category'] == 'postal')
        self.assertIsNone(row['previous_formality_pct'])
        self.assertIsNone(row['formality_pct_change_pp'])

    def test_matching_does_not_guess_renamed_predecessors(self):
        old = election('2019fed', [('A', 100, 90, 90), ('Old', 100, 90, 90)])
        new = election('2022fed', [('A', 100, 80, 80), ('New', 100, 90, 90)])
        tables = report.build_tables([old, new])
        seats = [r for r in tables['changes'] if r['level'] == 'seat']
        self.assertEqual([r['geography'] for r in seats], ['A'])
        self.assertEqual(tables['matching'][0]['previous_only'], 'Old')
        self.assertEqual(tables['matching'][0]['current_only'], 'New')
        self.assertAlmostEqual(seats[0]['turnout_pct_change_minus_election_pp'], -5)

    def test_wa_mobile_is_combined_with_old_ordinary_definition(self):
        old = election('2021wa', [('A', 100, 90, 90)],
                       {'A': [('election_day_ordinary', 90, None)]}, 'wa')
        new = election('2025wa', [('A', 100, 90, 90)],
                       {'A': [('election_day_ordinary', 80, None), ('mobile_or_institution', 10, None)]}, 'wa')
        row = report.category_rows(old, new, 'seat', 'A', ['A'], ['A'])[0]
        self.assertEqual(row['category'], 'ordinary_including_mobile')
        self.assertEqual(row['formal_share_pct_change_pp'], 0)
        self.assertIsNone(row['previous_ballot_share_pct'])


if __name__ == '__main__':
    unittest.main()

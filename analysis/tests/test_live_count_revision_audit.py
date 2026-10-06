"""Distinguish preference revisions from new votes and delayed preference counts."""

import math
import unittest

from scripts.diagnostics.live_count_revision_audit import log_odds_change, revision_record


class LiveCountRevisionTests(unittest.TestCase):
    def test_opposing_changes_with_small_net_count_correction(self):
        row = revision_record({0: 700, 1: 300}, {0: 698, 1: 299},
                              {0: 700, 1: 300}, {0: 598, 1: 399}, 0)
        self.assertEqual(row['minimum_opposing_revision'], 99)
        self.assertEqual(row['signed_minimum_revision'], -99)
        self.assertEqual(row['tcp_total_change'], -3)
        self.assertFalse(row['pure_preference_revision'])

    def test_delayed_preferences_and_new_votes_are_not_transfers(self):
        row = revision_record({0: 700, 1: 300}, {0: 700, 1: 300},
                              {0: 350, 1: 150}, {0: 700, 1: 300}, 0)
        self.assertEqual(row['minimum_opposing_revision'], 0)
        self.assertFalse(row['pure_preference_revision'])

    def test_fixed_votes_can_have_revised_preferences_but_different_pairs_are_omitted(self):
        row = revision_record({0: 700, 1: 300}, {0: 700, 1: 300},
                              {0: 700, 1: 300}, {0: 600, 1: 400}, 0)
        self.assertTrue(row['pure_preference_revision'])
        self.assertIsNone(revision_record({0: 700}, {0: 700}, {0: 600, 1: 100}, {0: 500, 7: 200}, 0))

    def test_transformed_effect_keeps_the_same_parent_and_handles_possible_zero(self):
        self.assertEqual(log_odds_change({0: 700, 1: 300}, 0, 0), 0)
        effect = log_odds_change({0: 700, 1: 300}, 0, -100)
        self.assertAlmostEqual(effect, 25 * math.log((600.25 / 400.25) / (700.25 / 300.25)))
        self.assertTrue(math.isfinite(log_odds_change({0: 1, 1: 999}, 0, -1)))


if __name__ == '__main__':
    unittest.main()

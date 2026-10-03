"""Check the substantive differences between empty, unavailable and unknown counts."""

import math
from types import SimpleNamespace
from dataclasses import replace
import unittest

from lib.turnout import category_policy as policy
from lib.shared import turnout_data as data
from scripts.turnout import turnout_prior_prototype as prototype
import numpy as np


class CategoryPolicyTests(unittest.TestCase):
    def test_possible_zero_is_finite_but_small_real_counts_remain_small(self):
        weights = policy.possible_proportions([99999, 1, 0])
        self.assertAlmostEqual(weights[1], 1 / 100000.5)
        self.assertAlmostEqual(weights[2], .5 / 100000.5)
        self.assertAlmostEqual(policy.subset_log_odds(1, 100000), math.log(1 / 99999))
        self.assertTrue(math.isfinite(policy.subset_log_odds(0, 100000)))
        self.assertTrue(math.isfinite(policy.subset_log_odds(100000, 100000)))

    def test_unknown_and_whole_empty_groups_do_not_invent_a_division(self):
        self.assertIsNone(policy.possible_proportions([0, 0]))
        self.assertIsNone(policy.possible_proportions([10, None]))
        self.assertIsNone(policy.subset_log_odds(None, 100))
        with self.assertRaises(ValueError):
            prototype.group_weights(np.array([[0., 0.]]), [0, 1])

    def test_definition_boundaries_keep_individual_changes_out_of_training(self):
        for previous, current in [('2006vic', '2010vic'), ('2021wa', '2025wa')]:
            self.assertTrue(policy.merge_other_required(previous, current))
            self.assertIsNotNone(policy.comparison_exclusion(previous, current, 'other'))
        self.assertIsNone(policy.comparison_exclusion('2018vic', '2022vic', 'other'))
        self.assertEqual(policy.merge_other(dict(ordinary=80, other=0, postal=20)),
                         dict(ordinary_other=80, postal=20))

    def test_cancelled_service_suppression_does_not_remove_remote_mobile_votes(self):
        row = SimpleNamespace(canonical_category='mobile_or_institution', formal_votes=0,
            informal_votes=0, total_ballots=0, source_category='EV Early Voting Centre (mobile polling)')
        self.assertTrue(policy.suppressed_record('2020qld', row))
        row.source_category = 'EV Early Voting Centre Qld (mobile polling)'
        row.formal_votes, row.informal_votes, row.total_ballots = 1778, 86, 1864
        self.assertFalse(policy.suppressed_record('2020qld', row))
        self.assertIsNotNone(policy.comparison_exclusion('2020qld', '2024qld', 'mobile_or_institution'))
        row.source_category = 'EV Early Voting Centre (mobile polling)'
        with self.assertRaises(ValueError):
            policy.suppressed_record('2020qld', row)

    def test_aggregation_distinguishes_empty_groups_from_unknown_ballots(self):
        def row(category, formal, ballots):
            return SimpleNamespace(seat_name='A', canonical_category=category,
                formal_votes=formal, total_ballots=ballots, source_category=category)
        dataset = SimpleNamespace(vote_types=[row('postal', 0, 0), row('postal', 30, 31),
            row('provisional', 0, 0), row('absent', 0, None), row('marked_as_voted', 0, 1)])
        groups = {r['category']: r for r in policy.empty_groups({'2022vic': dataset})}
        self.assertEqual(set(groups), {'provisional', 'absent'})
        self.assertEqual(groups['provisional']['status'], 'recorded_empty')
        self.assertEqual(groups['absent']['status'], 'formal_zero_ballots_unknown')

    def test_reviewed_missing_count_preserves_raw_evidence_and_district_total(self):
        ordinary = data.VoteTypeRecord('2010vic', 'Oakleigh District', 'source', 'final',
            'Ordinary', 'election_day_ordinary', 100, 0, 100, 'complete')
        provisional = replace(ordinary, source_category='Provisional', canonical_category='provisional',
                              formal_votes=0, total_ballots=0)
        dataset = data.TurnoutDataset(
            elections=[data.ElectionDefinition('2010vic', '2010-11-27', 'vic')],
            seat_totals=[data.SeatTotal('2010vic', 'Oakleigh District', 'source', 110, 100, 0, 100)],
            vote_types=[ordinary, provisional])
        analytical = policy.apply_missing_count_policy(dataset)
        self.assertIsNone(analytical.vote_types[1].formal_votes)
        self.assertTrue(all(r.coverage == 'partial' for r in analytical.vote_types))
        self.assertEqual(dataset.vote_types[1].formal_votes, 0)
        self.assertEqual(analytical.seat_totals, dataset.seat_totals)
        self.assertEqual(policy.empty_groups({'2010vic': dataset})[0]['status'], 'reviewed_missing_count')
        revised = replace(dataset, vote_types=[replace(ordinary, formal_votes=70, total_ballots=70),
                                              replace(provisional, formal_votes=30, total_ballots=30)])
        self.assertIs(policy.apply_missing_count_policy(revised), revised)


if __name__ == '__main__':
    unittest.main()

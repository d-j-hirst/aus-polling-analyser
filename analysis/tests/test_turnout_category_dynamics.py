"""Check vote conservation, independent training and observable category scope."""

from types import SimpleNamespace
import unittest

from scripts.turnout import turnout_category_dynamics as dynamics
from lib.turnout import category_policy as policy
import math


class CategoryDynamicsTests(unittest.TestCase):
    def setUp(self):
        self.baseline = dict(shares=dict(ordinary=.5, early=.3, postal=.1, absent=.08, other=.02),
                             counts=dict(ordinary=50, early=30, postal=10, absent=8, other=2))

    def test_ordinary_adjustment_preserves_small_categories_while_proportional_does_not(self):
        controls = [(['early'], 40), (['postal'], 10)]
        proportional, _ = dynamics.allocate(100, self.baseline, controls, 'proportional_remainder')
        ordinary, _ = dynamics.allocate(100, self.baseline, controls, 'ordinary_adjustment')
        self.assertAlmostEqual(proportional['ordinary'], 50 * 5 / 6)
        self.assertEqual(ordinary['ordinary'], 40)
        self.assertEqual(ordinary['absent'], 8)
        self.assertEqual(ordinary['other'], 2)
        self.assertAlmostEqual(sum(proportional.values()), 100)
        self.assertEqual(sum(ordinary.values()), 100)

    def test_observed_declaration_early_rate_is_preserved_inside_combined_control(self):
        baseline = dict(shares=dict(ordinary=.5, early_ordinary=.25, early_declaration=.05, postal=.2),
                        counts=dict(ordinary=50, early_ordinary=25, early_declaration=5, postal=20))
        result, _ = dynamics.allocate(100, baseline,
            [(['early_ordinary', 'early_declaration'], 40)], 'ordinary_adjustment')
        self.assertEqual(result['early_declaration'], 5)
        self.assertEqual(result['early_ordinary'], 35)
        self.assertEqual(sum(result.values()), 100)

    def test_conflicting_controls_are_recorded_and_cannot_create_negative_votes(self):
        result, activations = dynamics.allocate(100, self.baseline,
            [(['early'], 120), (['postal'], 10)], 'ordinary_adjustment')
        self.assertIn('controls_exceed_total', activations)
        self.assertTrue(all(v >= 0 for v in result.values()))
        self.assertAlmostEqual(sum(result.values()), 100)

    def test_sa_combined_partition_never_creates_old_postal_or_early_counts(self):
        def dataset(code, values):
            return SimpleNamespace(
                elections=[SimpleNamespace(election_code=code, jurisdiction='sa')],
                seat_totals=[SimpleNamespace(seat_name='A', formal_votes=100)],
                vote_types=[SimpleNamespace(seat_name='A', canonical_category=k,
                    formal_votes=v, coverage='complete') for k, v in values.items()])
        old = dataset('2022sa', dict(election_day_ordinary=60, declaration_combined=40))
        new = dataset('2026sa', dict(election_day_ordinary=50, early_in_person=30, postal=10, absent=10))
        self.assertEqual(dynamics.partition(old, 'sa_combined', {})['A'],
                         dict(ordinary=60, declarations=40))
        self.assertEqual(dynamics.partition(new, 'sa_combined', {})['A'],
                         dict(ordinary=50, declarations=50))

    def test_federal_national_control_uses_all_districts_without_final_target_weights(self):
        pair = dict(current='2025fed', keys=['ordinary', 'early_ordinary', 'early_declaration', 'postal'],
                    seats={'A': None, 'B': None},
                    baseline={'A': dict(counts=dict(early_ordinary=30, early_declaration=10)),
                              'B': dict(counts=dict(early_ordinary=50, early_declaration=10))})
        observation = dict(election_code='2025fed', election_date='2025-05-03', kind='federal',
            family='federal_prepoll_national', eligible=True, seat_name='', members=['A'],
            operational_count=100, observed_at='2025-05-03', observation_status='final_reconciled')
        summary = dict(election_code='2019fed', election_date='2019-05-18', kind='federal',
            family='federal_prepoll_national', eligible=True, conversion=1, conversion_low=1,
            conversion_high=1, local_variance=None)
        result, _ = dynamics.control_estimates(pair, [observation], [summary], 'earlier_only')
        self.assertEqual(result['early']['values'], {'A': 40, 'B': 60})

    def test_rate_training_excludes_test_and_successor_and_prediction_ignores_final_count(self):
        target = dict(level='election', geography='vic', current='2026vic', previous='2022vic',
            current_date='2026-11-28', previous_turnout_pct=95, previous_formality_pct=100,
            current_turnout_pct=90, current_formality_pct=100, current_enrolment=1000,
            current_formal_votes=900, turnout_pct_change_pp=-5, ballot_transition=False)
        rows = [target,
                dict(target, current='2022vic', previous='2018vic', current_date='2022-11-26',
                     turnout_pct_change_pp=-1, current_turnout_pct=94),
                dict(target, current='2025fed', previous='2022fed', current_date='2025-05-03',
                     turnout_pct_change_pp=-3, current_turnout_pct=92),
                dict(target, current='2030vic', previous='2026vic', current_date='2030-11-30',
                     turnout_pct_change_pp=90)]
        pair = dict(current='2026vic', seats={'A': SimpleNamespace(enrolment=1000, formal_votes=900)})
        totals, training = dynamics.total_estimates(pair, rows, 'leave_one_out', 'half_drift')
        self.assertEqual(training, ['2022vic', '2025fed'])
        drift = .25 * (policy.subset_log_odds(94, 100) + policy.subset_log_odds(92, 100)
                       - 2 * policy.subset_log_odds(95, 100))
        expected = 1000 / (1 + math.exp(-policy.subset_log_odds(95, 100) - drift))
        self.assertAlmostEqual(totals['A'], expected)
        pair['seats']['A'].formal_votes = 400
        self.assertEqual(dynamics.total_estimates(pair, rows, 'leave_one_out', 'half_drift')[0], totals)

    def test_reallocation_halves_category_error_only_with_actual_total(self):
        row = dict(current='2025fed', anchor='actual_total', actual_total=100,
                   predicted_total=100, errors=dict(ordinary=-10, early=10), activations=[])
        self.assertEqual(dynamics.score([row])['reallocation_pct'], 10)
        row['anchor'] = 'half_drift'
        self.assertIsNone(dynamics.score([row])['reallocation_pct'])

    def test_joint_allocation_errors_and_covariance_preserve_zero_sum(self):
        rows = [dict(current=code, scheme='leave_one_out', model='ordinary_adjustment',
                     controls='early+postal', anchor='actual_total', training_variant='all_history',
                     enrolment=1000, errors=dict(ordinary=-e, early=e, postal=0, other=0))
                for code, e in [('2022vic', 10), ('2022vic', 30), ('2025fed', -10), ('2025fed', 10)]]
        for result in dynamics.joint_errors(rows):
            self.assertAlmostEqual(sum(result['mean_per_1000'].values()), 0)
            for values in result['covariance_per_1000_squared'].values():
                self.assertAlmostEqual(sum(values.values()), 0)

    def test_broader_observed_groups_are_not_split_for_four_category_diagnostics(self):
        self.assertIsNone(dynamics.collapsed(dict(ordinary_other=-10, early=10, postal=0)))
        self.assertIsNone(dynamics.collapsed(dict(ordinary_early_absent_other=-10, postal=10)))

    def test_covid_training_comparison_only_scores_shared_observations(self):
        row = dict(current='2025fed', seat_name='A', scheme='leave_one_out',
                   model='proportional_remainder', anchor='actual_total', actual_total=100,
                   predicted_total=100, errors=dict(ordinary=-10, early=10), activations=[])
        rows = [dict(row, training_variant='all_history'),
                dict(row, seat_name='B', training_variant='all_history'),
                dict(row, training_variant='without_covid', errors=dict(ordinary=-5, early=5))]
        result = dynamics.paired_covid_scores(rows)[0]
        self.assertEqual(result['all_history']['districts'], 1)
        self.assertEqual(result['without_covid']['districts'], 1)
        self.assertEqual(result['all_history']['reallocation_pct'], 10)
        self.assertEqual(result['without_covid']['reallocation_pct'], 5)


if __name__ == '__main__':
    unittest.main()

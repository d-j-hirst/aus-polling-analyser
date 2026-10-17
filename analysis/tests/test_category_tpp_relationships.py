"""Portable checks for the historical category diagnostic; no private feeds."""

import unittest
from unittest.mock import patch

import numpy as np

from scripts.diagnostics import category_tpp_history as history
from scripts.diagnostics import category_tpp_relationships as analysis
from scripts.diagnostics import audit_category_tpp as audit
from scripts.diagnostics import category_size_effects as sizes
from scripts.diagnostics import declaration_prepoll_relationships as declaration
from scripts.diagnostics.collect_category_sources import nsw_table_url


class CategoryHistoryTests(unittest.TestCase):
    def test_declaration_prepolls_remain_separate_from_both_possible_partners(self):
        raw = {'ordinary': np.array([60., 40.]), 'early': np.array([30., 70.]),
               'early_declaration': np.array([45., 55.]), 'absent': np.array([65., 35.]),
               'postal': np.array([20., 80.])}
        result = declaration.split_account(raw, '2022fed')
        np.testing.assert_array_equal(result['ppvc'], [30, 70])
        np.testing.assert_array_equal(result['early_absent'], [45, 55])
        np.testing.assert_array_equal(result['day_absent'], [65, 35])
        self.assertTrue(declaration.complete(result))
        del raw['early_declaration']
        self.assertFalse(declaration.complete(declaration.split_account(raw, '2022fed')))
        raw['declaration_early'] = np.array([45., 55.])
        self.assertTrue(declaration.complete(declaration.split_account(raw, '2024qld')))

    def test_correct_pooled_total_can_conceal_opposite_component_errors(self):
        previous = {'ordinary': np.array([50., 50.]), 'early_absent': np.array([50., 50.]),
                    'day_absent': np.array([50., 50.])}
        current = {'ordinary': np.array([50., 50.]), 'early_absent': np.array([80., 120.]),
                   'day_absent': np.array([70., 30.])}
        result = declaration.pooling_loss(previous, current, 'day_absent')
        self.assertAlmostEqual(result['component_errors_pp']['early_absent'], -10.)
        self.assertAlmostEqual(result['component_errors_pp']['day_absent'], 20.)
        self.assertAlmostEqual(result['weighted_error_pp'], 0.)
        self.assertAlmostEqual(result['departure'], 0.)

    def test_switching_channels_conserves_party_votes(self):
        after = sizes.transfer_votes(600, 400, .55, .40, 100, .30)
        self.assertAlmostEqual(sum(n*p for n, p in after.values()), 490.)
        self.assertAlmostEqual(sum(n*(1-p) for n, p in after.values()), 510.)
        self.assertAlmostEqual(after['ordinary'][1], .60)
        self.assertAlmostEqual(after['early'][1], .38)
        unchanged_early = sizes.transfer_votes(600, 400, .55, .40, 100, .40)
        self.assertAlmostEqual(unchanged_early['early'][1], .40)

    def test_transfer_cannot_invent_source_party_votes(self):
        with self.assertRaises(ValueError):
            sizes.transfer_votes(600, 400, .10, .40, 100, .90)

    def test_size_regression_predicts_an_unseen_linear_example(self):
        rows = [dict(log_ratio_growth=x, contrast_change=2-3*x) for x in (.1, .3, .5, .7, .9)]
        result = sizes.compare_predictors(rows)
        self.assertAlmostEqual(result['slope'], -3.)
        self.assertAlmostEqual(result['prediction_rmse']['size_growth'], 0.)

    def test_category_mix_recentring_restores_expected_total(self):
        # Moving most voters into a historically lower-Labor category must
        # not silently lower a fixed overall prior. The old method does so.
        old = {'Example': {'ordinary': np.array([60., 40.]),
                           'postal': np.array([40., 60.]), 'early': np.array([50., 50.])}}
        new = {'Example': {'ordinary': np.array([60., 40.]),
                           'postal': np.array([120., 180.]), 'early': np.array([50., 50.])}}
        result = audit.audit_election(old, new, 50., 46.)
        self.assertAlmostEqual(result['old_expected_weighted_total'], 46.)
        self.assertAlmostEqual(result['expected_weighted_total'], 50.)
        self.assertAlmostEqual(result['reconstructed_error'], -4.)
        self.assertLess(result['categories']['ordinary']['error_pp'], 0.)

    def test_remote_channel_bridge_preserves_every_vote(self):
        account = {'ordinary': np.array([60., 40.]), 'early': np.array([30., 20.]),
                   'postal': np.array([10., 20.]), 'ivote': np.array([20., 10.]),
                   'early_declaration': np.array([2., 3.]), 'provisional': np.array([1., 2.])}
        merged = audit.category_partition(account, True)
        np.testing.assert_array_equal(sum(merged.values()), sum(account.values()))
        np.testing.assert_array_equal(merged['postal'], [30, 30])
        np.testing.assert_array_equal(merged['early'], [32, 23])
        self.assertNotIn('ivote', merged)
        self.assertIn('ivote', audit.category_partition(account))

    def test_nsw_early_total_does_not_become_ordinary(self):
        self.assertEqual(history.nsw_category('Total Early Voting Centre Ordinary Votes'), 'early')
        self.assertEqual(history.nsw_category('Total Voting Centre Ordinary Votes'), 'ordinary')
        self.assertIsNone(history.nsw_category('Total Declaration Votes'))

    def test_country_labor_alias_is_confined_to_nsw(self):
        page = b'<select><option value="labor/lib">Person (CLP) - Person (LIB)</option></select>'
        with patch('scripts.diagnostics.collect_category_sources.fetch', return_value=page):
            self.assertEqual(nsw_table_url('https://example/LA/test/TCP'),
                             'https://example/LA/test/TCP/labor/lib/tcp_table.html')
        self.assertEqual(history.side('CLP'), 'coalition')
        self.assertEqual(history.side('LNQ'), 'coalition')
        self.assertIsNone(history.side('GRN'))

    def test_reconciliation_rejects_missing_category(self):
        rows = [['Total', '60', '40']]
        account = [history.record('2000xx', 'Example', 'ordinary', 40, 30, 'fixture')]
        self.assertFalse(history.reconciled_html_account(account, rows, 1, 'total'))
        account.append(history.record('2000xx', 'Example', 'postal', 20, 10, 'fixture'))
        self.assertTrue(history.reconciled_html_account(account, rows, 1, 'total'))

    def test_negative_or_malformed_counts_are_rejected(self):
        for value in ('-2', 'unreported', '3.5'):
            with self.assertRaises(ValueError):
                history.integer(value)

    def test_early_merge_does_not_double_count_total(self):
        account = {'ordinary': np.array([40., 40.]), 'early': np.array([20., 10.]),
                   'early_declaration': np.array([5., 3.])}
        np.testing.assert_array_equal(analysis.category_count(account, 'early'), [25, 13])
        np.testing.assert_array_equal(analysis.category_count(account, 'ppvc'), [20, 10])
        np.testing.assert_array_equal(analysis.aggregate({'Example': account}, ['Example']), [65, 53])

    def test_missing_and_zero_categories_supply_no_transformed_observation(self):
        self.assertIsNone(analysis.share_z(None))
        self.assertIsNone(analysis.share_z(np.zeros(2)))
        self.assertTrue(np.isfinite(analysis.share_z(np.array([0., 100.]))))

    def test_unchanged_leans_carry_whole_election_error(self):
        accounts = {f'Seat {i}': {'ordinary': np.array([600., 400.]),
                                 'postal': np.array([400., 600.])} for i in range(30)}
        result = analysis.comparison(accounts, accounts, 'postal', 52., {'transformed': analysis.z(50.)})
        self.assertAlmostEqual(result['ordinary_error'], analysis.z(52.))
        self.assertAlmostEqual(result['category_error'], result['ordinary_error'])
        self.assertAlmostEqual(result['contrast_change'], 0.)
        self.assertAlmostEqual(result['local_change_rms'], 0.)

    def test_regression_withheld_election_predictions(self):
        rows = [dict(ordinary_error=x, category_error=2+x*.5, local_change_rms=1)
                for x in (-4., -2., 0., 2., 4.)]
        result = analysis.relationship_summary(rows)
        self.assertAlmostEqual(result['slope'], .5)
        self.assertAlmostEqual(result['intercept'], 2.)
        self.assertAlmostEqual(result['predictive_rmse']['fitted'], 0.)


if __name__ == '__main__':
    unittest.main()

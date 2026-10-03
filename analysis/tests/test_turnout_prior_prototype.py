"""Protect training boundaries, shared responses and conserved prior counts."""

import copy
import unittest
import numpy as np

from lib.turnout import prior
from scripts.turnout import turnout_prior_prototype as prototype


def small_case():
    inputs = dict(identity='example/earlier_only', election_code='example',
        seat_names=['A', 'B'], categories=['ordinary', 'early', 'postal', 'other'],
        subdivisions=['state', 'state'], enrolment=[1000, 1500],
        previous_turnout_pct=[90, 90], previous_formality_pct=[95, 95], local_scale=[1, 1],
        controls=[dict(name='early', indices=[1], weights=[[1], [1]], amount=[250, 375],
            common_sd=[10, 15], local_sd=[20, 30], aggregate=True),
            dict(name='postal', indices=[2], weights=[[1], [1]], amount=[100, 150],
                 common_sd=[5, 7.5], local_sd=[5, 7.5], aggregate=False)],
        remainder=dict(name='remainder', indices=[0, 3], weights=[[.9, .1], [.9, .1]]))
    parameters = dict(rates=dict(drift_log_odds=-.04, units='natural log odds',
        common_covariance=[[.01, -.002], [-.002, .01]],
        local_covariance=[[.01, 0], [0, .01]]), compositions={
        'early': dict(common_covariance=[[0]], local_covariance=[[0]]),
        'postal': dict(common_covariance=[[0]], local_covariance=[[0]]),
        'remainder': dict(common_covariance=[[.001, -.001], [-.001, .001]],
                          local_covariance=[[.002, -.002], [-.002, .002]])})
    return inputs, parameters


class PriorPrototypeTests(unittest.TestCase):
    def test_transformed_rates_preserve_informal_votes_and_nonparticipants(self):
        # Large upward and downward draws must taper at the endpoints, while
        # preserving ballots above formal votes and enrolment above ballots.
        inputs, parameters = small_case()
        inputs['previous_turnout_pct'] = [96, 96]
        inputs['previous_formality_pct'] = [98, 98]
        parameters['rates']['common_covariance'] = [[1, 0], [0, 1]]
        reference = prior.draw(inputs, parameters, 256, local=False)
        compact = prior.draw(inputs, parameters, 256, local=False, representation='compact')
        ballots = np.array(inputs['enrolment']) * reference['rates']['turnout_pct'] / 100
        self.assertTrue((compact['totals'] < ballots).all())
        self.assertTrue((ballots < np.array(inputs['enrolment'])).all())
        for values in (reference, compact):
            for metric in ('turnout_pct', 'formality_pct'):
                self.assertTrue((values['rates'][metric] > 0).all())
                self.assertTrue((values['rates'][metric] < 100).all())
            self.assertEqual(values['diagnostics']['rate_endpoint_fraction'], 0)
        np.testing.assert_array_equal(reference['rates']['turnout_pct'], compact['rates']['turnout_pct'])
        np.testing.assert_array_equal(reference['rates']['formality_pct'], compact['rates']['formality_pct'])

    def test_equal_log_odds_changes_taper_near_rate_endpoints(self):
        previous = np.array([1., 50., 99.])
        higher = prior.rate_percent(prior.rate_log_odds(previous) + .5)
        lower = prior.rate_percent(prior.rate_log_odds(previous) - .5)
        self.assertLess(higher[2] - previous[2], (higher[1] - previous[1]) / 10)
        self.assertLess(previous[0] - lower[0], (previous[1] - lower[1]) / 10)
        np.testing.assert_allclose(prior.rate_percent(prior.rate_log_odds(previous)), previous)
        with self.assertRaises(ValueError):
            prior.rate_log_odds([0, 100])

    def test_rate_sensitivities_use_the_same_transformed_units_as_draws(self):
        inputs, parameters = small_case()
        _, central = prior.central_counts(inputs, parameters)
        prepared = prior.prepare_responses(inputs, parameters)
        eps = 1e-5
        for metric in ('turnout', 'formality'):
            perturbed = copy.deepcopy(inputs)
            key = 'previous_' + metric + '_pct'
            perturbed[key] = prior.rate_percent(prior.rate_log_odds(perturbed[key]) + eps).tolist()
            _, changed = prior.central_counts(perturbed, parameters)
            response = prepared['responses'][metric + '_log_odds']
            self.assertIn('log-odds', response['units'])
            np.testing.assert_allclose((changed - central) / eps, response['values'], rtol=1e-5, atol=1e-5)

    def test_qld_detail_training_stays_on_its_side_of_reporting_change(self):
        old = dict(mode='qld_detail', previous='2015qld', current='2017qld',
                   old_parts={'A': {'early_declaration': 100}}, actual={'A': {'early_declaration': 30}})
        new = dict(mode='qld_detail', previous='2020qld', current='2024qld',
                   old_parts={'A': {'early_declaration': 1000}}, actual={'A': {'early_declaration': 1200}})
        self.assertIsNone(prototype.projected_parts(old, new))
        self.assertIsNone(prototype.projected_parts(new, old))
        self.assertEqual(prototype.projected_parts(new, new), (new['old_parts'], new['actual']))
        # Broad combined-early comparisons can still combine an observed split;
        # the restriction is on transferring the incompatible detailed split.
        broad = dict(mode='qld_combined_early', previous='2009qld')
        self.assertEqual(prototype.projected_parts(new, broad)[0]['A']['early'], 1000)

    def test_transformed_categories_stay_positive_and_zero_input_weights_stay_zero(self):
        weights = np.array([[.93, .07, 0], [.7, .3, 0]])
        counts = prior.transformed_partition(np.array([10000., 10000.]), weights,
                                             np.array([[20, -20, 0], [-20, 20, 0]]))
        self.assertTrue((counts[:, :2] > 0).all())
        np.testing.assert_array_equal(counts[:, 2], 0)
        np.testing.assert_allclose(counts.sum(axis=1), 10000)

    def test_transformed_count_response_scales_with_starting_category(self):
        # Identical relative errors should move a 1% category by fewer votes
        # than a 30% category, rather than applying the same absolute share gap.
        weights = np.array([[.99, .01], [.7, .3]])
        error = np.array([[-.1, .1], [-.1, .1]])
        result = prior.transformed_partition(np.array([10000., 10000.]), weights, error)
        self.assertLess(result[0, 1] - 100, (result[1, 1] - 3000) / 10)

    def test_growing_ordinary_prepolls_do_not_train_declaration_uncertainty(self):
        # Declaration votes remain 4% of all formal votes while ordinary early
        # votes grow. Supplying each training election's combined early total
        # should explain this change without creating declaration-rate errors.
        pairs = []
        for year, ordinary_before, ordinary_after in ((2013, 200, 400), (2016, 400, 600)):
            old = dict(early_ordinary=ordinary_before, early_declaration=40,
                       ordinary=960 - ordinary_before)
            new = dict(early_ordinary=ordinary_after, early_declaration=40,
                       ordinary=960 - ordinary_after)
            pairs.append(dict(current=str(year)+'fed', previous=str(year-3)+'fed',
                date=str(year)+'-01-01', usable=True, mode='fed_detail',
                old_parts={'A': old}, actual={'A': new}))
        target = dict(current='2025fed', date='2025-01-01', mode='fed_detail')
        fitted = prototype.fit_composition(pairs, target, 'earlier_only',
            ['early_ordinary', 'early_declaration'], 'early_declaration')
        np.testing.assert_allclose(fitted['common_covariance'], 0, atol=1e-20)
        np.testing.assert_allclose(fitted['local_covariance'], 0, atol=1e-20)

    def test_total_based_declaration_anchor_and_transformed_derivative(self):
        inputs, parameters = small_case()
        inputs['categories'] = ['ordinary', 'early_ordinary', 'postal', 'early_declaration']
        inputs['controls'][0].update(indices=[1, 3], weights=[[.8, .2], [.8, .2]],
                                    total_anchor=dict(position=1, fraction=[.04, .04]))
        inputs['remainder'].update(indices=[0], weights=[[1], [1]])
        parameters['compositions']['early'] = dict(common_covariance=[[.04, -.04], [-.04, .04]],
                                                  local_covariance=[[0, 0], [0, 0]])
        parameters['compositions']['remainder'] = dict(common_covariance=[[0]], local_covariance=[[0]])
        total, central = prior.central_counts(inputs, parameters)
        np.testing.assert_allclose(central[:, 3], total * .04)
        more = copy.deepcopy(inputs)
        more['controls'][0]['amount'] = [400, 600]
        _, new = prior.central_counts(more, parameters)
        np.testing.assert_allclose(new[:, 3], central[:, 3])
        response = np.asarray(prior.prepare_responses(inputs, parameters)['responses']['early_composition_common']['values'])
        amount = np.asarray(inputs['controls'][0]['amount'])
        weights = prior.allocation_weights(inputs['controls'][0], amount, total)
        root = prior.covariance_root(parameters['compositions']['early']['common_covariance'])
        eps = 1e-5
        perturbed = prior.transformed_partition(amount, weights, eps * root[:, 0])
        np.testing.assert_allclose((perturbed - central[:, [1, 3]]) / eps,
                                   response[:, [1, 3], 0], rtol=1e-5, atol=1e-5)

    def test_reference_and_compact_conserve_nonnegative_counts(self):
        inputs, parameters = small_case()
        for representation in ('reference', 'compact'):
            values = prior.draw(inputs, parameters, 256, representation=representation)
            np.testing.assert_allclose(values['counts'].sum(axis=2), values['totals'], atol=1e-8)
            self.assertTrue((values['counts'] >= 0).all())
            self.assertLess(values['diagnostics']['maximum_accounting_error_votes'], 1e-8)
            self.assertTrue(np.isfinite(values['diagnostics']['aggregate_control_maximum_adjustment_votes']['early']))

    def test_conflicting_control_draws_taper_with_a_positive_remainder(self):
        inputs, parameters = small_case()
        inputs['controls'][0]['common_sd'] = [1000, 1500]
        for representation in ('reference', 'compact'):
            values = prior.draw(inputs, parameters, 64, representation=representation)
            self.assertGreater(values['diagnostics']['control_conflict_fraction'], 0)
            self.assertGreater(values['diagnostics']['aggregate_control_maximum_adjustment_votes']['early'], 0)
            self.assertTrue((values['counts'] > 0).all())
            np.testing.assert_allclose(values['counts'].sum(axis=2), values['totals'], atol=1e-8)
        totals, _ = prior.central_counts(inputs, parameters)
        supplied, remaining = prior.reconcile_controls(inputs, totals,
            [np.array([1500, 2250]), np.array([100, 150])], totals)
        self.assertTrue((remaining > 10).all())
        np.testing.assert_allclose(sum(supplied) + remaining, totals)
        inputs['controls'][0]['amount'] = [1500, 2250]
        with self.assertRaisesRegex(ValueError, 'positive expected remainder'):
            prior.central_counts(inputs, parameters)

    def test_common_and_local_streams_remain_separate(self):
        inputs, parameters = small_case()
        values = prior.draw(inputs, parameters, 64, local=False)
        np.testing.assert_allclose(values['totals'][:, 1], 1.5 * values['totals'][:, 0])
        np.testing.assert_allclose(values['counts'][:, 1], 1.5 * values['counts'][:, 0])
        fixed = prior.draw(inputs, parameters, 64, common=False, local=False)
        _, central = prior.central_counts(inputs, parameters)
        np.testing.assert_allclose(fixed['counts'], np.broadcast_to(central, fixed['counts'].shape))

    def test_distinct_responses_include_compensating_remainder(self):
        inputs, parameters = small_case()
        prepared = prior.prepare_responses(inputs, parameters)
        early = np.array(prepared['responses']['early_count']['values'])
        postal = np.array(prepared['responses']['postal_count']['values'])
        np.testing.assert_allclose(early.sum(axis=1), 0, atol=1e-12)
        np.testing.assert_allclose(postal.sum(axis=1), 0, atol=1e-12)
        self.assertFalse(np.array_equal(early, postal))
        self.assertEqual(early[0, 1], 1)
        self.assertEqual(early[0, 0], -.9)

    def test_named_district_draws_survive_input_reordering(self):
        inputs, parameters = small_case()
        original = prior.draw(inputs, parameters, 64)
        swapped = copy.deepcopy(inputs)
        for key in ('seat_names', 'subdivisions', 'enrolment', 'previous_turnout_pct',
                    'previous_formality_pct', 'local_scale'):
            swapped[key].reverse()
        for group in swapped['controls'] + [swapped['remainder']]:
            for key in ('weights', 'amount', 'common_sd', 'local_sd'):
                if key in group:
                    group[key].reverse()
        reordered = prior.draw(swapped, parameters, 64)
        np.testing.assert_allclose(original['counts'], reordered['counts'][:, ::-1], atol=1e-8)

    def test_both_count_interfaces_retain_the_same_transformed_calculation(self):
        inputs, parameters = small_case()
        reference = prior.draw(inputs, parameters, 64)
        compact = prior.draw(inputs, parameters, 64, representation='compact')
        np.testing.assert_array_equal(reference['counts'], compact['counts'])

    def test_rate_training_excludes_held_election_and_successor(self):
        target = dict(level='election', current='2022vic', previous='2018vic', current_date='2022-11-26',
            previous_turnout_pct=90, current_turnout_pct=91, previous_formality_pct=95,
            current_formality_pct=95.1, ballot_transition=False)
        rows = [dict(target, current='2018vic', previous='2014vic', current_date='2018-11-24'),
                dict(target, current='2019fed', previous='2016fed', current_date='2019-05-18'),
                target, dict(target, current='2026vic', previous='2022vic', current_date='2026-11-28',
                             current_turnout_pct=60)]
        rows = prototype.transformed_rate_rows(rows)
        target = rows[2]
        parameters = prototype.fit_rates(rows, target, 'leave_one_out', 'half_drift')
        self.assertEqual(parameters['training_elections'], ['2018vic', '2019fed'])
        self.assertAlmostEqual(parameters['drift_log_odds'], .5 * (prior.rate_log_odds(91) - prior.rate_log_odds(90)))
        target['turnout_change_log_odds'] = 999
        self.assertEqual(prototype.fit_rates(rows, target, 'leave_one_out', 'half_drift'), parameters)

    def test_conversion_width_never_uses_held_conversion(self):
        def summary(code, factor):
            return dict(election_code=code, election_date=code[:4] + '-01-01', family='early', kind='state',
                eligible=True, conversion=factor, conversion_low=factor, conversion_high=factor, local_variance=.001)
        rows = [summary('2014vic', .9), summary('2018vic', .95), summary('2022vic', .1)]
        target = rows[-1]
        first = prototype.fit_control(rows, target, 'earlier_only')
        target['conversion'] = 100
        self.assertEqual(prototype.fit_control(rows, target, 'earlier_only'), first)
        self.assertEqual(first['training_elections'], ['2014vic', '2018vic'])

    def test_category_training_excludes_successor_with_held_old_counts(self):
        target = dict(current='2022vic', previous='2018vic', date='2022-11-26')
        pairs = [dict(target, usable=True),
                 dict(target, current='2026vic', previous='2022vic', date='2026-11-28', usable=True),
                 dict(target, current='2018vic', previous='2014vic', date='2018-11-24', usable=True)]
        self.assertEqual([p['current'] for p in prototype.training_pairs(pairs, target, 'leave_one_out')], ['2018vic'])

    def test_interval_score_penalizes_a_miss(self):
        values = np.full((128, 1), 100.)
        hit = prototype.interval_metrics(values, np.array([100.]), np.array([1000.]), .8)
        miss = prototype.interval_metrics(values, np.array([110.]), np.array([1000.]), .8)
        self.assertEqual(hit['interval_score_per_1000'], 0)
        self.assertAlmostEqual(miss['interval_score_per_1000'], 100)
        self.assertEqual(miss['coverage'], 0)


if __name__ == '__main__':
    unittest.main()

"""Protect pooled election-fold independence, local weights and uncertainty scales."""

from dataclasses import replace
import math
import unittest

from tests.test_turnout_changes import election
import turnout_priors as priors


class TurnoutPriorTests(unittest.TestCase):
    def history(self):
        return [
            election('2004fed', [('A', 1000, 950, 900), ('B', 1000, 900, 850)]),
            election('2007fed', [('A', 1000, 940, 890), ('B', 1000, 900, 850)]),
            election('2010fed', [('A', 1000, 920, 870), ('B', 1000, 900, 850)]),
            election('2013fed', [('A', 1000, 910, 860), ('B', 1000, 900, 850)]),
            election('2016fed', [('A', 1000, 900, 850), ('B', 1000, 900, 850)]),
            election('2006vic', [('X', 1000, 950, 930)], jurisdiction='vic'),
            election('2010vic', [('X', 1000, 930, 910)], jurisdiction='vic'),
        ]

    def test_leave_one_out_pools_jurisdictions_without_held_endpoints(self):
        rows = priors.prepare_rows(self.history())
        target = next(r for r in rows if r['level'] == 'election' and r['current'] == '2010fed')
        training = priors.training_elections(rows, target, 'leave_one_out')
        self.assertEqual({r['current'] for r in training}, {'2007fed', '2016fed', '2010vic'})
        self.assertTrue(all('2010fed' not in (r['previous'], r['current']) for r in training))

    def test_earlier_only_uses_actual_dates_even_in_the_same_year(self):
        history = self.history()
        history[-1].date = '2010-11-01'
        rows = priors.prepare_rows(history)
        target = next(r for r in rows if r['level'] == 'election' and r['current'] == '2010fed')
        self.assertEqual([r['current'] for r in priors.training_elections(rows, target, 'earlier_only')],
                         ['2007fed'])

    def test_leave_jurisdiction_out_fits_only_other_jurisdictions(self):
        rows = priors.prepare_rows(self.history())
        target = next(r for r in rows if r['level'] == 'election' and r['current'] == '2010fed')
        training = priors.training_elections(rows, target, 'leave_jurisdiction_out')
        self.assertEqual([r['current'] for r in training], ['2010vic'])

    def test_uncertainty_distinguishes_mean_spread_from_zero_error(self):
        rows = [dict(current='first', value=-1), dict(current='second', value=-3)]
        result = priors.moments(rows, 'value', sample=True)
        self.assertEqual(result['mean_pp'], -2)
        self.assertAlmostEqual(result['sd_pp'], math.sqrt(2))
        self.assertAlmostEqual(result['rmse_zero_pp'], math.sqrt(5))

    def test_local_moments_weight_elections_not_seat_counts(self):
        rows = [dict(current='first', value=10)] + [dict(current='second', value=0)] * 100
        result = priors.moments(rows, 'value')
        self.assertAlmostEqual(result['mean_pp'], 5)
        self.assertAlmostEqual(result['sd_pp'], 5)
        self.assertAlmostEqual(result['rmse_zero_pp'], math.sqrt(50))
        self.assertEqual(result['elections'], 2)

    def test_federal_seat_gap_and_local_change_use_state_not_national(self):
        old = election('2019fed', [('A', 1000, 900, 850), ('B', 1000, 900, 850), ('C', 1000, 900, 850)])
        new = election('2022fed', [('A', 1000, 800, 750), ('B', 1000, 880, 830), ('C', 1000, 900, 850)])
        old.seats['C'] = replace(old.seats['C'], subdivision='qld')
        new.seats['C'] = replace(new.seats['C'], subdivision='qld')
        rows = priors.prepare_rows([old, new])
        a = next(r for r in rows if r['level'] == 'seat' and r['geography'] == 'A')
        self.assertEqual(a['turnout_pct_local_change_pp'], -4)
        self.assertEqual(a['turnout_pct_previous_gap_pp'], 0)

    def test_pooled_pattern_slope_demeans_election_shocks(self):
        rows = []
        for code, offset, count in [('first', 100, 1), ('second', -100, 50)]:
            for _ in range(count):
                for gap in (-1, 1):
                    rows.append(dict(current=code, turnout_pct_previous_gap_pp=gap,
                                     turnout_pct_local_change_pp=offset - .25 * gap))
        self.assertEqual(priors.fit_local_pattern(rows, 'turnout_pct'), -.25)
        self.assertEqual(priors.fit_local_pattern([], 'turnout_pct'), 0)

    def test_target_votes_do_not_influence_held_prediction_parameters(self):
        history = self.history()
        before, _ = priors.backtest(priors.prepare_rows(history))
        history[2] = election('2010fed', [('A', 1000, 600, 500), ('B', 1000, 800, 750)])
        after, _ = priors.backtest(priors.prepare_rows(history))
        fields = ('predicted_turnout_pct', 'predicted_formality_pct', 'expected_formal_votes',
                  'turnout_pct_slope', 'formality_pct_slope')
        first = [r for r in before if r['current'] == '2010fed']
        second = [r for r in after if r['current'] == '2010fed']
        self.assertEqual(len(first), len(second))
        for a, b in zip(first, second):
            self.assertEqual([a[k] for k in fields], [b[k] for k in fields])

    def test_formality_drift_excludes_reform_training(self):
        row = dict(level='election', previous_turnout_pct=90, current_turnout_pct=89,
                   previous_formality_pct=95, current_formality_pct=94,
                   current_enrolment=1000, current_formal_votes=837)
        training = [dict(ballot_transition=False, turnout_pct_change_pp=-1, formality_pct_change_pp=.1),
                    dict(ballot_transition=True, turnout_pct_change_pp=-3, formality_pct_change_pp=-10)]
        result = priors.prediction(row, 'both_drift', training, {})
        self.assertAlmostEqual(result['predicted_turnout_pct'], 88)
        self.assertAlmostEqual(result['predicted_formality_pct'], 95.1)
        self.assertEqual(result['formality_pct_drift_training_elections'], 1)
        result = priors.prediction(row, 'turnout_drift', training, {})
        self.assertEqual(result['predicted_formality_pct'], 95)
        self.assertEqual(result['formality_pct_drift_training_elections'], 0)

    def test_no_training_retains_previous_rates_and_count_identity(self):
        row = dict(level='election', previous_turnout_pct=90, current_turnout_pct=85,
                   previous_formality_pct=95, current_formality_pct=90,
                   current_enrolment=1200, current_formal_votes=918)
        result = priors.prediction(row, 'turnout_drift', [], {})
        self.assertEqual(result['expected_formal_votes'], 1026)
        self.assertEqual(result['predicted_turnout_pct'], 90)
        self.assertEqual(result['predicted_formality_pct'], 95)

    def test_gap_bins_use_prior_not_current_level_and_exclude_formality_reforms(self):
        rows = [dict(level='seat', current='2022vic', ballot_transition=False,
                     turnout_pct_previous_gap_pp=-6, turnout_pct_local_change_pp=8,
                     formality_pct_previous_gap_pp=1, formality_pct_local_change_pp=2),
                dict(level='seat', current='2017qld', ballot_transition=True,
                     turnout_pct_previous_gap_pp=-5, turnout_pct_local_change_pp=0,
                     formality_pct_previous_gap_pp=1, formality_pct_local_change_pp=-20)]
        result = priors.gap_variability(rows)
        below = next(r for r in result if r['metric'] == 'turnout_pct' and r['previous_gap_bin_pp'] == 'below -5')
        self.assertEqual(below['observations'], 1)
        self.assertEqual(below['mean_pp'], 8)
        formal = next(r for r in result if r['metric'] == 'formality_pct')
        self.assertEqual(formal['observations'], 1)
        self.assertEqual(formal['mean_pp'], 2)

    def test_seat_histories_do_not_include_formality_reform_as_normal_change(self):
        rows = priors.prepare_rows(self.history())
        histories = priors.seat_histories(rows)
        a = next(r for r in histories if r['jurisdiction'] == 'fed' and r['seat'] == 'A'
                 and r['metric'] == 'formality_pct')
        self.assertEqual(a['transitions'], 3)
        self.assertNotIn('2016fed', a['comparison_targets'])
        # The latest descriptive level remains available even for a reform election.
        self.assertEqual(a['latest'], '2016fed')

    def test_score_weights_election_folds_not_seat_counts(self):
        rows = [dict(current='first', error_pp=10)] + [dict(current='second', error_pp=0)] * 100
        rows = [dict(r, scheme='leave_one_out', level='seat', model='prior_gap',
                     ballot_transition=False, metric='turnout_pct') for r in rows]
        score = next(r for r in priors.scores(rows, local=True) if r['subset'] == 'all')
        self.assertAlmostEqual(score['bias'], 5)
        self.assertAlmostEqual(score['mae'], 5)
        self.assertAlmostEqual(score['rmse'], math.sqrt(50))
        self.assertEqual(score['units'], 'pp')

    def test_rate_clipping_preserves_finite_vote_count(self):
        row = dict(level='election', previous_turnout_pct=99, current_turnout_pct=95,
                   previous_formality_pct=1, current_formality_pct=1,
                   current_enrolment=1000, current_formal_votes=9.5)
        training = [dict(ballot_transition=False, turnout_pct_change_pp=5, formality_pct_change_pp=-2)]
        result = priors.prediction(row, 'both_drift', training, {})
        self.assertEqual(result['predicted_turnout_pct'], 100)
        self.assertEqual(result['predicted_formality_pct'], 0)
        self.assertEqual(result['expected_formal_votes'], 0)
        self.assertTrue(result['turnout_pct_clipped'])
        self.assertTrue(result['formality_pct_clipped'])


if __name__ == '__main__':
    unittest.main()

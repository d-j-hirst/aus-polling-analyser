"""Protect election-fold independence, regime selection and expectation arithmetic."""

import unittest
from dataclasses import replace

from tests.test_turnout_changes import election
from scripts.turnout import turnout_expectations as expectations


class TurnoutExpectationTests(unittest.TestCase):
    def federal_history(self):
        return [
            election('2004fed', [('A', 1000, 950, 900)]),
            election('2007fed', [('A', 1000, 940, 890)]),
            election('2010fed', [('A', 1000, 920, 870)]),
            election('2013fed', [('A', 1000, 910, 860)]),
            election('2016fed', [('A', 1000, 900, 850)]),
            election('2019fed', [('A', 1000, 890, 840)]),
        ]

    def test_leave_one_out_removes_target_and_successor(self):
        rows = expectations.prepare_changes(self.federal_history())
        target = next(r for r in rows if r['level'] == 'election' and r['current'] == '2010fed')
        training = expectations.training_changes(rows, target, 'leave_one_out')
        self.assertEqual([r['current'] for r in training], ['2007fed', '2016fed', '2019fed'])
        self.assertTrue(all('2010fed' not in (r['previous'], r['current']) for r in training))

    def test_earlier_only_never_uses_future_elections(self):
        rows = expectations.prepare_changes(self.federal_history())
        target = next(r for r in rows if r['level'] == 'election' and r['current'] == '2010fed')
        self.assertEqual([r['current'] for r in expectations.training_changes(rows, target, 'earlier_only')], ['2007fed'])

    def test_comparable_formality_does_not_use_qld_opv_or_transition(self):
        history = [
            election('2009qld', [('A', 1000, 950, 930)], jurisdiction='qld'),
            election('2012qld', [('A', 1000, 940, 920)], jurisdiction='qld'),
            election('2015qld', [('A', 1000, 930, 910)], jurisdiction='qld'),
            election('2017qld', [('A', 1000, 920, 870)], jurisdiction='qld'),
            election('2020qld', [('A', 1000, 910, 880)], jurisdiction='qld'),
            election('2024qld', [('A', 1000, 900, 860)], jurisdiction='qld'),
        ]
        rows = expectations.prepare_changes(history)
        target = next(r for r in rows if r['level'] == 'election' and r['current'] == '2024qld')
        training = expectations.training_changes(rows, target, 'earlier_only')
        selected = expectations.comparable_changes(training, target)
        self.assertEqual([r['current'] for r in selected], ['2020qld'])

    def test_first_reform_falls_back_without_an_invented_effect(self):
        rows = expectations.prepare_changes(self.federal_history())
        target = next(r for r in rows if r['level'] == 'election' and r['current'] == '2016fed')
        training = expectations.training_changes(rows, target, 'earlier_only')
        predicted = expectations.predict(target, 'ballot_comparable', training, {})
        self.assertEqual(predicted['formality_pct_training_elections'], 0)
        self.assertEqual(predicted['predicted_formality_pct'], target['previous_formality_pct'])

    def test_formal_count_uses_known_current_enrolment_and_predicted_rates(self):
        old = election('2019fed', [('A', 1000, 900, 810)])
        new = election('2022fed', [('A', 1200, 1000, 950)])
        row = next(r for r in expectations.prepare_changes([old, new]) if r['level'] == 'election')
        result = expectations.predict(row, 'carry_forward', [], {})
        self.assertAlmostEqual(result['expected_ballots'], 1080)
        self.assertAlmostEqual(result['expected_formal_votes'], 972)

    def test_score_weights_election_folds_not_number_of_seats(self):
        rows = []
        for code, errors in [('2019fed', [10.0]), ('2022fed', [0.0] * 100)]:
            for error in errors:
                rows.append(dict(scheme='leave_one_out', model='carry_forward', jurisdiction='fed',
                                 level='seat', current=code, ballot_transition=False,
                                 turnout_pct_error_pp=error, formality_pct_error_pp=0,
                                 formal_votes_error_pct=error))
        score = next(r for r in expectations.summarize_scores(rows)
                     if r['metric'] == 'turnout' and r['subset'] == 'all')
        self.assertEqual(score['mae'], 5.0)
        self.assertAlmostEqual(score['rmse'], 50 ** .5)

    def test_prediction_parameters_ignore_held_votes(self):
        history = self.federal_history()
        before = expectations.backtest(history, expectations.prepare_changes(history))
        history[2] = election('2010fed', [('A', 1000, 700, 600)])
        after = expectations.backtest(history, expectations.prepare_changes(history))
        fields = ['predicted_turnout_pct', 'predicted_formality_pct', 'expected_formal_votes']
        for first, second in zip(
                [r for r in before if r['current'] == '2010fed'],
                [r for r in after if r['current'] == '2010fed']):
            self.assertEqual([first[k] for k in fields], [second[k] for k in fields])

    def test_federal_state_offset_is_relative_to_training_national_change(self):
        history = [
            election('2013fed', [('A', 1000, 900, 850), ('B', 1000, 900, 850)]),
            election('2016fed', [('A', 1000, 800, 750), ('B', 1000, 900, 850)]),
            election('2019fed', [('A', 1000, 850, 800), ('B', 1000, 850, 800)]),
        ]
        for e in history:
            e.seats['B'] = replace(e.seats['B'], subdivision='qld')
        predictions = expectations.backtest(history, expectations.prepare_changes(history))
        seats = {r['geography']: r for r in predictions if r['current'] == '2019fed'
                 and r['level'] == 'seat' and r['scheme'] == 'earlier_only' and r['model'] == 'ballot_state'}
        self.assertEqual(seats['A']['turnout_pct_drift_pp'], -5)
        self.assertEqual(seats['A']['turnout_pct_state_offset_pp'], -5)
        self.assertEqual(seats['B']['turnout_pct_state_offset_pp'], 5)
        self.assertEqual(seats['A']['predicted_turnout_pct'], 70)
        self.assertEqual(seats['B']['predicted_turnout_pct'], 90)


if __name__ == '__main__':
    unittest.main()

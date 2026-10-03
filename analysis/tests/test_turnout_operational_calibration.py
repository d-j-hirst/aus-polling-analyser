"""Check actual fold boundaries, pooling weights and rounding sensitivity."""

import unittest

from scripts.turnout import turnout_operational_calibration as calibration


def case(code, count, final, seat='A', **changes):
    row = dict(row_id=code + '/' + seat, election_code=code,
               election_date=code[:4] + '-05-01', jurisdiction=code[4:],
               kind='federal' if code.endswith('fed') else 'state',
               family='postal_applications_cumulative', source_id='source',
               seat_name=seat, observation_status='contemporaneous',
               observed_at=code[:4] + '-04-30', operational_count=count,
               final_formal=final, seat_formal=500, enrolment=600,
               conversion=final / count if count else None,
               rounding_half_width=0, eligible=True, members=[seat],
               target_categories=['postal'], target_source_category='')
    row.update(changes)
    return row


class OperationalCalibrationTests(unittest.TestCase):
    def test_prediction_control_with_missing_target_cannot_train_conversion(self):
        rows = [case('2022vic', 100, 90),
                dict(case('2022vic', 100, 90, seat='B'), final_formal=None, conversion=None)]
        summary = calibration.election_summaries(rows)[0]
        self.assertEqual(summary['cases'], 1)
        self.assertEqual(summary['operational_count'], 100)
        self.assertEqual(summary['conversion'], .9)

    def test_each_election_has_equal_weight_despite_district_count(self):
        rows = [case('2020qld', 100, 50)] + [case('2021wa', 100, 90, str(i)) for i in range(20)]
        parameters = calibration.fit(calibration.election_summaries(rows))
        self.assertAlmostEqual(parameters['factor'], .7)
        self.assertEqual(parameters['n_elections'], 2)

    def test_whole_election_is_held_out_and_earlier_only_is_separate(self):
        rows = [case('2020qld', 100, 50), case('2021wa', 100, 80),
                case('2022vic', 100, 90)]
        summaries = calibration.election_summaries(rows)
        target = rows[1]
        leave_out, _ = calibration.training_for(summaries, target, 'leave_one_out', 'pooled')
        earlier, _ = calibration.training_for(summaries, target, 'earlier_only', 'pooled')
        self.assertEqual({r['election_code'] for r in leave_out}, {'2020qld', '2022vic'})
        self.assertEqual([r['election_code'] for r in earlier], ['2020qld'])
        self.assertNotIn('2021wa', calibration.fit(leave_out)['training_elections'])

    def test_federal_state_pool_has_an_explicit_sparse_sample_fallback(self):
        rows = [case('2019fed', 100, 90), case('2020qld', 100, 50),
                case('2021wa', 100, 60), case('2022vic', 100, 70)]
        summaries = calibration.election_summaries(rows)
        selected, fallback = calibration.training_for(summaries, rows[-1], 'leave_one_out', 'kind_pooled')
        self.assertEqual({r['election_code'] for r in selected}, {'2020qld', '2021wa'})
        self.assertEqual(fallback, '')
        selected, fallback = calibration.training_for(summaries, rows[1], 'earlier_only', 'kind_pooled')
        self.assertEqual([r['election_code'] for r in selected], ['2019fed'])
        self.assertTrue(fallback)

    def test_rounding_limits_change_only_the_sensitivity_not_the_point_fit(self):
        rows = [case('2020qld', 100, 50, rounding_half_width=1), case('2021wa', 100, 70)]
        parameters = calibration.fit(calibration.election_summaries(rows))
        self.assertAlmostEqual(parameters['factor'], .6)
        self.assertLess(parameters['factor_low'], parameters['factor'])
        self.assertGreater(parameters['factor_high'], parameters['factor'])
        self.assertLess(parameters['factor_high'] - parameters['factor'], .01)

    def test_pairwise_scores_use_identical_cases_and_equal_election_weights(self):
        predictions = []
        for code, seat, model, error in [('2020qld', 'A', 'pooled', 10),
                                         ('2020qld', 'B', 'pooled', 100),
                                         ('2020qld', 'A', 'previous_category', 20),
                                         ('2021wa', 'A', 'pooled', 30),
                                         ('2021wa', 'A', 'previous_category', 40)]:
            predictions.append(dict(scheme='leave_one_out', family='postal_applications_cumulative',
                row_id=code + seat, election_code=code, model=model, final_formal=100,
                seat_formal=500, error_votes=error, interval_low=None))
        pair = next(r for r in calibration.paired_scores(predictions)
                    if r['scheme'] == 'leave_one_out' and r['model'] == 'previous_category')
        self.assertEqual(pair['pooled']['cases'], 2)
        self.assertEqual(pair['pooled']['aggregate_abs_pct'], 20)
        self.assertEqual(pair['alternative']['aggregate_abs_pct'], 30)

    def test_no_training_does_not_invent_a_point_or_interval(self):
        rows = [case('2020qld', 100, 50)]
        folds, predictions = calibration.backtest(rows, calibration.election_summaries(rows), {})
        self.assertTrue(all(f['parameters'] is None for f in folds))
        self.assertEqual(predictions, [])

    def test_shared_error_allowance_does_not_choose_the_favourable_scheme(self):
        comparisons = [case('2020qld', 100, 50), case('2021wa', 100, 50)]
        parameters = [dict(family='postal_applications_cumulative', kind='all',
                           n_elections=2, common_sd=.01)]
        predictions = [dict(row_id=rows['row_id'], election_code=rows['election_code'],
                            scheme=scheme, model='pooled', family=rows['family'],
                            operational_count=100, error_votes=error)
                       for rows in comparisons for scheme, error in
                       [('leave_one_out', 10), ('earlier_only', 20)]]
        calibration.attach_error_allowances(parameters, predictions, comparisons)
        self.assertAlmostEqual(parameters[0]['common_error_allowance'], .2)
        self.assertEqual(parameters[0]['validation_errors']['earlier_only']['elections'], 2)

    def test_district_and_parent_controls_are_not_pooled_twice(self):
        # Use the existing normalized schema so this checks selection against
        # an actual parent/child source, rather than a mock of the selector.
        from lib.shared.turnout_data import (ElectionDefinition, OperationalObservation,
            SeatTotal, SourceDefinition, TurnoutDataset, VoteTypeRecord)
        dataset = TurnoutDataset(
            elections=[ElectionDefinition('2020qld', '2020-05-01', 'qld')],
            sources=[SourceDefinition('final', '2020qld', 'Commission', 'retained', 'fixture', 'final', 'fixture'),
                     SourceDefinition('source', '2020qld', 'Commission', 'retained', 'fixture', 'operational', 'fixture')],
            seat_totals=[SeatTotal('2020qld', name, 'final', 1000, 800, 50, 850) for name in ['A', 'B']],
            vote_types=[VoteTypeRecord('2020qld', name, 'final', 'partition', 'Postal', 'postal', 50, 0, 50, 'partial')
                        for name in ['A', 'B']],
            operational_observations=[OperationalObservation('2020qld', 'source', 'postal_applications_cumulative',
                '2020-04-30', count, geography, 'contemporaneous', seat_name=seat)
                for count, geography, seat in [(100, 'elector_division', 'A'),
                                                (100, 'elector_division', 'B'), (201, 'state', '')]])
        rows, exclusions = calibration.comparison_rows({'2020qld': dataset})
        self.assertEqual(len(rows), 2)
        self.assertEqual(exclusions['alternative_source_or_parent'], 1)
        self.assertEqual(calibration.election_summaries(rows)[0]['operational_count'], 200)


if __name__ == '__main__':
    unittest.main()

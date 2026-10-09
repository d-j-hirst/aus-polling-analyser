"""Exercise continuity, retained tails and exact counted-account boundaries."""

import unittest

import numpy as np

from lib.turnout import late_counts, live
from tests.test_turnout_live_prototype import live_case


class LateCountTests(unittest.TestCase):
    def prepared_case(self):
        inputs, draws, units = live_case()
        units[0]['counted'], units[1]['counted'] = 450, 200
        units[2]['counted'], units[3]['counted'] = 90, 30
        prepared = live.update(draws, inputs, units)
        return inputs, units, prepared

    def test_neutral_evidence_reproduces_broad_prediction(self):
        inputs, units, prepared = self.prepared_case()
        result = late_counts.update(prepared,inputs,units,[dict(strength=0.) for u in units])
        np.testing.assert_array_equal(result['totals'],np.repeat(prepared['totals'],8,axis=0))
        np.testing.assert_array_equal(result['unit_means'],prepared['unit_counts'].mean(axis=0))
        np.testing.assert_array_equal(result['no_addition_probability'],[0.,0.])

    def test_external_comparison_quantiles_reject_invalid_shape_and_endpoints(self):
        inputs, units, prepared = self.prepared_case()
        evidence = [dict(strength=.8) for u in units]
        for grid in (np.full((1, 1), .5), np.zeros((1024, 2)), np.ones((1024, 2))):
            with self.assertRaisesRegex(ValueError, 'interior quantile'):
                late_counts.update(prepared, inputs, units, evidence, uniforms=grid)

    def test_strong_evidence_reduces_total_without_losing_counts_or_booth_estimates(self):
        inputs, units, prepared = self.prepared_case()
        result = late_counts.update(prepared,inputs,units,[dict(strength=.8) for u in units])
        self.assertLess(result['totals'][:,0].mean(),prepared['totals'][:,0].mean())
        self.assertTrue((result['totals'][:,0] >= 770).all())
        self.assertTrue((result['totals'] < inputs['enrolment']).all())
        np.testing.assert_allclose(result['counts'].sum(axis=2),result['totals'])
        np.testing.assert_array_equal(result['unit_means'][:2],prepared['unit_counts'][:,:2].mean(axis=0))
        self.assertGreaterEqual(result['diagnostics']['minimum_category_addition'],-1e-9)
        self.assertTrue(all(c['probabilities']['batch'] > 0 for c in result['components']))

    def test_small_evidence_change_does_not_switch_count_outcomes(self):
        inputs, units, prepared = self.prepared_case()
        first = late_counts.update(prepared,inputs,units,[dict(strength=.37) for u in units],count_draws=4)
        next_value = late_counts.update(prepared,inputs,units,[dict(strength=.37000001) for u in units],count_draws=4)
        self.assertLess(np.abs(first['totals']-next_value['totals']).max(),.01)

    def test_authoritative_finalisation_remains_exact(self):
        inputs, draws, units = live_case()
        units[0]['counted'] = 700
        units[2]['counted'] = 100
        prepared = live.update(draws,inputs,units,finalised_seats=['A'])
        result = late_counts.update(prepared,inputs,units,[dict(strength=.8) for u in units])
        np.testing.assert_array_equal(result['totals'][:,0],np.full(1024,800))
        self.assertEqual(result['no_addition_probability'][0],1.)
        np.testing.assert_array_equal(late_counts.prediction_quantiles(result,[.025,.975])[:,0],[800,800])

    def test_shared_zero_event_preserves_account_and_rare_batch_probability(self):
        inputs, units, prepared = self.prepared_case()
        result = late_counts.update(prepared,inputs,units,[dict(strength=.8) for u in units])
        p = result['no_addition_probability'][0]
        self.assertGreater(p,.025)
        self.assertLess(p,1.)
        bounds = late_counts.prediction_quantiles(result,[.025,.975])
        self.assertEqual(bounds[0,0],770)
        self.assertGreater(bounds[1,0],770)
        np.testing.assert_allclose(late_counts.prediction_mean(result,'counts').sum(axis=1),late_counts.prediction_mean(result))
        for c in result['components']:
            self.assertAlmostEqual(sum(c['probabilities'].values()),1.)
            self.assertAlmostEqual(c['probabilities']['batch'],c['weight']*result['config']['batch_probability'])
        means = late_counts.prediction_mean(result)
        self.assertAlmostEqual(means[0],770+(1-p)*(result['totals'][:,0].mean()-770))

    def test_postal_receipt_window_also_discounts_shared_zero_event(self):
        inputs, units, prepared = self.prepared_case()
        units[2]['vote_type'] = 'Postal'
        neutral = [dict(strength=.8,postal_receipt_support=1.) for u in units]
        before_deadline = [dict(e) for e in neutral]
        before_deadline[2]['postal_receipt_support'] = .1
        first = late_counts.update(prepared,inputs,units,neutral)
        second = late_counts.update(prepared,inputs,units,before_deadline)
        self.assertAlmostEqual(second['no_addition_probability'][0],.1*first['no_addition_probability'][0])

    def test_zero_probability_does_not_depend_on_extra_count_draws(self):
        inputs, units, prepared = self.prepared_case()
        evidence = [dict(strength=.8) for u in units]
        first = late_counts.update(prepared,inputs,units,evidence,count_draws=1)
        second = late_counts.update(prepared,inputs,units,evidence,count_draws=8)
        np.testing.assert_array_equal(first['no_addition_probability'],second['no_addition_probability'])
        for a,b in zip(first['components'],second['components']):
            self.assertEqual(a['probabilities'],b['probabilities'])

    def test_progress_accounts_for_revision_and_discounts_long_unobserved_gap(self):
        units = [dict(seat_name='A',name='Postal',kind='declaration',counted=1000)]
        def source(day, count):
            return dict(source_time=f'2025-05-{3+day:02d}T18:00:00',
                        seats={'A':dict(vote_types={'Postal':count})})
        steady = [source(day,1000) for day in range(8)]
        quiet = late_counts.progress_evidence(steady,units)[0]
        sparse = late_counts.progress_evidence([steady[0],steady[-1]],units)[0]
        changed = late_counts.progress_evidence([*steady[:-1],source(7,1200),source(8,1000)],units)[0]
        self.assertGreater(quiet['strength'],sparse['strength'])
        self.assertLess(changed['strength'],quiet['strength'])
        unknown = [dict(source_time=s['source_time'],seats={'A':dict(vote_types={})}) for s in steady]
        self.assertEqual(late_counts.progress_evidence(unknown,units)[0]['strength'],0)

    def test_components_are_retained_even_when_batch_probability_is_tiny(self):
        inputs, units, prepared = self.prepared_case()
        result = late_counts.update(prepared,inputs,units,[dict(strength=.8) for u in units],
                                   options={'batch_probability':1e-6},count_draws=2)
        self.assertTrue(all(c['probabilities']['batch'] > 0 for c in result['components']))
        self.assertTrue(all(c['batch_mean_if_other_counts_fixed'] > c['small_mean_if_other_counts_fixed']
                            for c in result['components']))

    def test_closed_services_do_not_supply_local_or_shared_progress_evidence(self):
        from copy import deepcopy
        units = [dict(seat_name=name, name='Absent', kind='declaration', counted=count)
                 for name, count in [('A', 1000), ('B', 0)]]
        units[1]['closed_reason'] = 'Service unavailable at this election.'
        history = [dict(source_time=f'2025-05-{day:02d}T18:00:00',
                        seats={name:dict(vote_types={'Absent':count})
                               for name, count in [('A', 1000), ('B', 0)]})
                   for day in range(3, 28)]
        without_closed = deepcopy(history)
        for snapshot in without_closed:
            snapshot['seats'].pop('B')
        # Check both national and subdivision measurements. Historical zeros
        # remain in the archive, but an unavailable service cannot delay A.
        for states in ({}, {'A':'X', 'B':'X'}, {'A':'X', 'B':'Y'}):
            expected = late_counts.progress_evidence(without_closed, units[:1], states)[0]
            actual = late_counts.progress_evidence(history, units, states)
            self.assertEqual(actual[0], expected)
            self.assertEqual(actual[1]['strength'], 0)
            # A genuinely open, unstarted service still weakens shared support.
            open_units = deepcopy(units)
            open_units[1].pop('closed_reason')
            self.assertLess(late_counts.progress_evidence(history, open_units, states)[0]['strength'],
                            expected['strength'])
        self.assertEqual(late_counts.progress_evidence(history, units[1:])[0]['strength'], 0)

        # A closed reporting batch does not suppress an available sibling in
        # the same category, and does not itself receive the sibling's evidence.
        sibling = dict(units[0], counted=0, closed_reason='This batch was cancelled.')
        actual = late_counts.progress_evidence(history, [units[0], sibling])
        self.assertEqual(actual[0], late_counts.progress_evidence(history, units[:1])[0])
        self.assertEqual(actual[1]['strength'], 0)

    def test_shared_extremes_do_not_define_typical_reporting_progress(self):
        rows = [dict(activity=.1, started=.9, volume=10., current=1000.) for _ in range(43)]
        rows += [dict(activity=v, started=s, volume=m, current=1000.)
                 for v,s,m in [(0.,0.,0.),(0.,0.,0.),(1.,1.,10000.),(1.,1.,10000.)]]
        shared = late_counts.shared_measurements(rows)
        self.assertAlmostEqual(shared['activity'],.1)
        self.assertAlmostEqual(shared['started'],.9)
        self.assertAlmostEqual(shared['volume'],10/1000.5)

    def test_postal_deadline_response_is_smooth_and_preserves_other_categories(self):
        from datetime import datetime, timedelta
        deadline = datetime(2025,5,16,18)
        units = [dict(seat_name='A',name=category,kind='declaration',counted=1000)
                 for category in ('Postal','Absent')]
        def history(now):
            return [dict(source_time=(now-timedelta(days=d)).isoformat(),
                         seats={'A':dict(vote_types={'Postal':1000,'Absent':1000})})
                    for d in range(7,-1,-1)]
        strengths = []
        for hours in (-120,0,120):
            source = history(deadline+timedelta(hours=hours))
            base = late_counts.progress_evidence(source,units)
            adjusted = late_counts.progress_evidence(source,units,postal_deadline=deadline.isoformat())
            self.assertEqual(base[1]['strength'],adjusted[1]['strength'])
            strengths.append(adjusted[0]['strength'])
            if hours == 0:
                self.assertAlmostEqual(adjusted[0]['strength'],base[0]['strength']/2)
        self.assertLess(strengths[0],strengths[1])
        self.assertLess(strengths[1],strengths[2])
        a = late_counts.progress_evidence(history(deadline),units,postal_deadline=deadline.isoformat())
        b = late_counts.progress_evidence(history(deadline+timedelta(seconds=1)),units,postal_deadline=deadline.isoformat())
        self.assertLess(abs(a[0]['strength']-b[0]['strength']),1e-5)

    def test_roundoff_zero_additions_do_not_break_the_transform(self):
        inputs, units, prepared = self.prepared_case()
        for j in (2,3):
            removed = prepared['remaining'][:,j].copy()
            prepared['remaining'][:,j] = 0.
            prepared['unit_counts'][:,j] = units[j]['counted']
            prepared['counts'][:,units[j]['seat_index'],units[j]['group_index']] -= removed
            prepared['totals'][:,units[j]['seat_index']] -= removed
        neutral = late_counts.update(prepared,inputs,units,[dict(strength=0.) for u in units])
        np.testing.assert_array_equal(neutral['totals'],np.repeat(prepared['totals'],8,axis=0))
        result = late_counts.update(prepared,inputs,units,[dict(strength=.8) for u in units])
        self.assertTrue(np.isfinite(result['totals']).all())
        self.assertGreaterEqual(result['diagnostics']['minimum_category_addition'],-1e-9)

    def test_exported_components_use_json_numbers(self):
        import json
        inputs, units, prepared = self.prepared_case()
        result = late_counts.update(prepared,inputs,units,[dict(strength=.8) for u in units])
        json.dumps(result['components'],allow_nan=False)

    def test_archived_narungga_mapping_agrees_with_corrected_total(self):
        from scripts.turnout.turnout_late_batch import correct_narungga_candidates
        records = {'Narungga':{str(i):dict(PrePoll=189 if i == 133004 else 0)
                              for i in range(133001,133011)}}
        correct_narungga_candidates(records)
        self.assertEqual(sum(r['PrePoll'] for r in records['Narungga'].values()),848)
        self.assertEqual(records['Narungga']['133004']['PrePoll'],13)

    def test_share_reweighting_is_joint_and_preserves_observed_votes(self):
        observed = np.array([[60.,40.],[20.,80.]])
        counts = np.array([[100.,100.],[100.,150.],[100.,150.000001]])
        shares = late_counts.reweight_shares(observed,counts)
        np.testing.assert_allclose(shares.sum(axis=1),1)
        np.testing.assert_allclose(shares[0],[.4,.6])
        self.assertLess(shares[1,0],shares[0,0])
        self.assertLess(np.abs(shares[2]-shares[1]).max(),1e-8)
        with self.assertRaises(ValueError):
            late_counts.reweight_shares([[0.,0.]],[[100.]])


if __name__ == '__main__':
    unittest.main()

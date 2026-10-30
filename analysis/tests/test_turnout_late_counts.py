"""Exercise continuity, retained tails and exact counted-account boundaries."""

import unittest

import numpy as np

from lib.turnout import completion, late_counts, live
from tests.test_turnout_live_prototype import live_case


class LateCountTests(unittest.TestCase):
    def test_early_shortfall_guard_needs_both_conditions(self):
        z = np.array([-3.,-2.,0.,2.,np.nan])
        np.testing.assert_array_equal(completion.early_completion_retention(z,None),1)
        young = completion.early_completion_retention(z,0)
        self.assertLess(young[0],.02)
        np.testing.assert_array_equal(young[2:],1)
        np.testing.assert_allclose(completion.early_completion_retention(z,7),(1+young)/2)
        self.assertGreater(completion.early_completion_retention(z,14)[0],.98)
        for age in (0,7,14):
            self.assertLess(np.max(abs(completion.early_completion_retention(z,age)
                -completion.early_completion_retention(z,age+1e-6))),1e-6)
        history = [dict(source_time='2024-10-30T18:00:00'),dict(source_time='2024-10-27T18:00:00')]
        self.assertEqual(completion.days_after_poll_close(history,'2024-10-26T18:00:00'),4)
        self.assertIsNone(completion.days_after_poll_close(history,None))

    def test_guard_returns_all_suppressed_local_finishing_mass_to_routine(self):
        previous,small,batch = np.array([.5]),np.array([.3]),np.array([.02])
        (p,s,b),f,r = completion.guard_probabilities((previous,small,batch),np.array([.4]),np.array([.2]),np.array([.1]))
        np.testing.assert_allclose(p*f,.02)
        np.testing.assert_allclose(p*r,.01)
        np.testing.assert_allclose(s,.03)
        np.testing.assert_array_equal(b,batch)
        np.testing.assert_allclose(p+s+b,.82) # District zero outcome stays .18.

    def test_postal_balance_preserves_support_endpoints(self):
        self.assertEqual(completion.postal_finishing_weight(0,.2,0),0)
        self.assertEqual(completion.postal_finishing_weight(1,.2,.9),1)
        self.assertAlmostEqual(completion.postal_finishing_weight(.5,.2,0),.75)
        self.assertGreater(completion.postal_finishing_weight(.5,.2,.9),.75)

    def test_allocated_postal_widening_preserves_actual_mean_and_bounds(self):
        from scipy.special import expit
        capacity = np.linspace(200.,12000.,400)
        original = capacity*expit(np.linspace(-8.,2.,400))
        for sd in [0.,.5,1.5,4.]:
            lower,upper,p = completion.allocated_postal_spread(original,capacity,sd,np.linspace(-4.,4.,400))
            np.testing.assert_allclose(p*upper+(1-p)*lower,original,rtol=1e-14,atol=1e-10)
            self.assertTrue((lower>=0).all() and (upper<=capacity).all())
            if sd: self.assertTrue(np.any(lower<original) and np.any(upper>original))
        exact = np.array([0.,40.,100.])
        lower,upper,_ = completion.allocated_postal_spread(exact,np.array([100.,100.,100.]),np.array([1.,0.,1.]),1.5)
        np.testing.assert_array_equal(exact,lower)
        np.testing.assert_array_equal(exact,upper)
        # These tiny shares also need a finite bounded calculation and neutral
        # mean; an input floor would silently invent new votes.
        tiny=np.array([1e-200,1e-12,1e-4])
        lower,upper,p=completion.allocated_postal_spread(tiny,1e5,2.,3.)
        np.testing.assert_allclose(p*upper+(1-p)*lower,tiny,rtol=1e-12,atol=0)

    def test_postal_widening_leaves_all_sibling_draws_and_final_means_fixed(self):
        inputs,units,prepared = self.prepared_case()
        units[2]['vote_type'] = 'Postal'
        evidence = [dict(strength=.25,postal_days_before_deadline=4) for _ in units]
        before = late_counts.update(prepared,inputs,units,evidence,count_draws=128,postal_widening=False)
        after = late_counts.update(prepared,inputs,units,evidence,count_draws=128)
        np.testing.assert_allclose(after['unit_means'],before['unit_means'],rtol=0,atol=1e-8)
        postal_group = units[2]['group_index']
        np.testing.assert_array_equal(np.delete(after['counts'],postal_group,axis=2),
                                      np.delete(before['counts'],postal_group,axis=2))
        np.testing.assert_array_equal(after['counts'][:,1],before['counts'][:,1])
        np.testing.assert_array_equal(after['no_addition_probability'],before['no_addition_probability'])
        self.assertGreater(np.max(abs(after['counts']-before['counts'])),1)
        self.assertGreater(after['counts'][:,0,postal_group].std(),before['counts'][:,0,postal_group].std())
        self.assertTrue((after['counts']>=after['counted_groups']-1e-9).all())
        self.assertTrue((after['totals']<=np.asarray(inputs['enrolment'])+1e-9).all())

    def test_two_postal_services_share_headroom_without_moving_other_counts(self):
        inputs,units,prepared = self.prepared_case()
        units[2]['vote_type'] = units[3]['vote_type'] = 'Postal'
        evidence = [dict(strength=.25) for _ in units]
        before = late_counts.update(prepared,inputs,units,evidence,count_draws=128,postal_widening=False)
        after = late_counts.update(prepared,inputs,units,evidence,count_draws=128)
        np.testing.assert_array_equal(after['unit_means'],before['unit_means'])
        np.testing.assert_array_equal(late_counts.prediction_mean(after),late_counts.prediction_mean(before))
        self.assertTrue((after['totals']<=np.asarray(inputs['enrolment'])+1e-9).all())

    def test_positive_remainder_preserves_other_paths_and_category_allocations(self):
        inputs, units, prepared = self.prepared_case()
        units[2]['vote_type'] = 'Absent'
        old_evidence = [dict(strength=.25) for _ in units]
        new_evidence = [dict(e) for e in old_evidence]
        new_evidence[2].update(positive_remainder_support=.99,volume=2.,pooled=dict(volume=.01))
        before = late_counts.update(prepared,inputs,units,old_evidence,count_draws=128)
        after = late_counts.update(prepared,inputs,units,new_evidence,count_draws=128)
        self.assertLess(after['unit_means'][2],before['unit_means'][2])
        np.testing.assert_array_equal(np.delete(after['unit_means'],2),np.delete(before['unit_means'],2))
        np.testing.assert_array_equal(after['no_addition_probability'],before['no_addition_probability'])
        self.assertTrue((after['totals'] <= before['totals']+1e-10).all())
        self.assertTrue((after['counts'] >= after['counted_groups']-1e-10).all())
        self.assertAlmostEqual(after['components'][0]['positive_remainder_median_votes'],2.+.01*90.5)
        self.assertGreater(after['components'][0]['probabilities']['positive_remainder'],0)
        for a,b in zip(after['components'],before['components']):
            self.assertEqual(a['allocation_probabilities_before_local_completion'],b['allocation_probabilities_before_local_completion'])
            self.assertEqual(a['probabilities']['batch'],b['probabilities']['batch'])
            self.assertEqual(a['probabilities']['small'],b['probabilities']['small'])
            self.assertAlmostEqual(sum(a['probabilities'].values()),1.)
        # A postal or unstarted service cannot acquire the new path, even if a
        # caller supplies supplementary quietness evidence for that service.
        for category,counted in [('Postal',90),('Provisional',90),('Absent',0)]:
            unit = dict(units[2],vote_type=category,counted=counted)
            fractions,_ = completion.positive_remainder_parameters([unit],[new_evidence[2]],np.array([.2]),np.array([.3]))
            self.assertEqual(fractions[0],0)

    def test_positive_remainder_width_shared_variation_and_branch_boundaries(self):
        n = 32768
        grid = np.random.default_rng(824).uniform(.000001,.999999,(n,2))
        shape = np.ones((n,2))
        shared = completion.postal_shared_normals(n,123 ^ 0x71651)
        # Force only the new positive path, with ample capacity to observe its
        # proposed distribution rather than the subsequent allocation limit.
        amounts = completion.positive_remainder_limits(8*shape,-12*shape,0*shape,1e9*shape,
            np.zeros(2),np.ones(2),np.array([100.,200.]),(1.,0.,0.),1.,grid,shared)
        logs = np.log(amounts/np.array([100.,200.]))
        np.testing.assert_allclose(np.median(logs,axis=0),0,atol=.04)
        np.testing.assert_allclose(logs.std(axis=0),1.75,atol=.03)
        self.assertAlmostEqual(np.corrcoef(logs.T)[0,1],.5**2/1.75**2,delta=.02)
        # A separate existing completion interval occupies the first 20% of
        # routine outcomes. Neither it nor either continuous mixture branch
        # may receive the new cap.
        grid = ((np.arange(1000)+.5)/1000)[:,None]
        shape = np.ones_like(grid)
        limits = completion.positive_remainder_limits(0*shape,0*shape,0*shape,1e9*shape,
            np.array([.2]),np.array([.3]),np.array([100.]),(.6,.2,.2),1.,grid,np.zeros(1000))
        np.testing.assert_array_equal(np.isfinite(limits[:,0]),(grid[:,0]>.32)&(grid[:,0]<.5))

    def test_gentler_local_activity_preserves_existing_shared_progress(self):
        from datetime import datetime, timedelta
        from lib.turnout import late_schedule
        units = [dict(seat_name=name,seat_index=k,name='Absent',kind='declaration',counted=1077.)
                 for k,name in enumerate(('A','B','C'))]
        start = datetime(2024,11,1)
        history = [dict(source_time=(start+timedelta(hours=6*k)).isoformat(),
            seats={name:dict(vote_types={'Absent':1000. if k<12 else 1077.}) for name in ('A','B','C')})
            for k in range(13)]
        deadline = '2024-11-12T00:00:00'
        measured = late_counts.progress_evidence(history,units,postal_deadline=deadline,
            counting_clock=late_schedule.counting_hours,event_scale=.02,shared_deadline=deadline)
        old = late_counts.progress_evidence(history,units,postal_deadline=deadline,
            counting_clock=late_schedule.counting_hours,event_scale=.001,shared_deadline=deadline)
        actual = late_schedule.progress_evidence(history,units,{},deadline,np.ones(3))
        quiet = lambda value,scale:1/(1+(value/scale)**2)
        for e,m,o in zip(actual,measured,old):
            self.assertEqual(e['pooled'],m['pooled'])
            self.assertLess(e['gentle_local_activity'],e['activity'])
            shared = quiet(m['pooled']['activity'],.3)
            strength = (m['coverage']*m['started']*quiet(1-m['pooled']['started'],.1)
                        *quiet(m['activity'],.3)*(.5+.5*shared)*m['postal_receipt_support'])
            prior_weight = .5*(-np.expm1(-(o['strength']/.3)**2)-np.expm1(-(strength/.3)**2)*shared)
            extra = .8*m['small_batch_support']*m['started']
            prior_weight += (1-prior_weight)*extra
            self.assertAlmostEqual(-np.expm1(-(e['strength']/.3)**2),prior_weight)
            self.assertGreater(e['positive_remainder_support'],prior_weight)

    def test_original_size_supports_completion_but_extreme_counts_retain_growth(self):
        from scipy.special import expit, logit
        parent = 10001.
        centre = logit(1000.5/parent)
        original = parent*expit(centre+np.array([-.5,.5]))-.5
        inputs = dict(enrolment=[10000.])
        units = [dict(seat_index=0,name='Absent',kind='declaration',counted=0)]
        prepared = dict(original_unit_counts=original[:,None],complete=np.array([False]))
        def at(z):
            units[0]['counted'] = parent*expit(centre+.5*z)-.5
            return completion.evidence(prepared,inputs,units,[dict(strength=0.)])[0][0]
        self.assertLess(at(-3),.01)
        self.assertGreater(at(0),.4)
        self.assertGreater(at(1),at(0))
        self.assertLess(at(3),at(1))
        self.assertLess(at(6),.02)
        self.assertLess(abs(at(1)-at(1.000001)),1e-5)
        units[0]['counted'] = 1000
        ordinary = completion.evidence(prepared,inputs,units,[dict(strength=0.)])[0][0]
        units[0]['name'] = 'Postal'
        postal = completion.evidence(prepared,inputs,units,[dict(strength=0.,postal_receipt_support=.01)])[0][0]
        self.assertAlmostEqual(postal,ordinary)
        for original in [np.zeros((2,1)),np.full((2,1),1000.)]:
            prepared['original_unit_counts'] = original
            weight, z = completion.evidence(prepared,inputs,units,[dict(strength=0.)])
            self.assertEqual(weight[0],0.)
            self.assertTrue(np.isnan(z[0]))

    def test_postal_remainder_calendar_centres_and_continuity(self):
        from scipy.special import expit
        for days, median in [(4,.06),(2,.04),(0,.02)]:
            self.assertAlmostEqual(expit(completion.postal_remainder_log_odds(days)-completion.POSTAL_LOG_MEDIAN_SHIFT),median)
        self.assertAlmostEqual(expit(completion.postal_remainder_log_odds()-completion.POSTAL_LOG_MEDIAN_SHIFT),.06)
        for days in (4,2,0,-2):
            before = completion.postal_remainder_log_odds(days+1e-7)
            after = completion.postal_remainder_log_odds(days-1e-7)
            self.assertGreater(before,after)
            self.assertLess(before-after,1e-6)
        self.assertAlmostEqual(np.exp(completion.postal_remainder_log_odds(-2)-completion.POSTAL_LOG_MEDIAN_SHIFT),.5*.02/.98)
        self.assertAlmostEqual(np.exp(completion.postal_remainder_log_odds(4)+1.25**2/2),.06/.94*np.exp(.32/2))

    def test_postal_remainder_keeps_shared_variation_and_scales_with_count(self):
        # Force the near-finished path, with ample allocation, to observe its
        # distribution independently of the other retained mixture branches.
        from scipy.special import logit
        n = 32768
        rng = np.random.default_rng(317)
        grid = rng.uniform(.000001,.999999,(n,3))
        reference = np.full((n,3),8.)
        small = np.full((n,3),-12.)
        batch = np.zeros((n,3))
        common = completion.postal_shared_normals(n,5)
        amount = completion.limits(reference,small,batch,1e8*np.ones((n,3)),
            np.ones(3),(1.,0.,0.),1.,grid,postal_counts=np.array([5000.,5000.,10000.]),
            postal_log_odds=np.full(3,logit(.06)),shared_normal=common)
        fraction = amount/(amount+np.array([5000.,5000.,10000.]))
        np.testing.assert_allclose(np.median(fraction,axis=0),.06,atol=.001)
        z = logit(fraction)-logit(.06)
        self.assertAlmostEqual(z[:,0].std(),1.25,delta=.02)
        self.assertAlmostEqual(np.corrcoef(z[:,:2].T)[0,1],.4**2/1.25**2,delta=.025)
        # A shared draw is invariant to the number/order of seats, not sampled
        # again per category. Reusing a local draw also gives exact volume scaling.
        grid[:,2] = grid[:,0]
        scaled = completion.limits(reference,small,batch,1e8*np.ones((n,3)),
            np.ones(3),(1.,0.,0.),1.,grid,postal_counts=np.array([5000.,5000.,10000.]),
            postal_log_odds=np.full(3,logit(.06)),shared_normal=common)
        np.testing.assert_allclose(scaled[:,2],2*scaled[:,0])

    def test_postal_remainder_releases_capacity_before_sibling_allocation(self):
        inputs, units, prepared = self.prepared_case()
        units[2]['vote_type'] = 'Postal'
        evidence = [dict(strength=.25,postal_days_before_deadline=4) for _ in units]
        reference = dict(prepared)
        reference.pop('original_unit_counts')
        local = dict(prepared)
        local['original_unit_counts'] = np.broadcast_to(prepared['original_unit_counts'].mean(0),
            prepared['original_unit_counts'].shape).copy()
        local['original_unit_counts'][:,2] = prepared['original_unit_counts'][:,2]
        before = late_counts.update(reference,inputs,units,evidence)
        after = late_counts.update(local,inputs,units,evidence)
        self.assertLess(after['unit_means'][2],before['unit_means'][2])
        # A smaller postal remainder can release some space to the other local
        # declaration, but cannot force all released votes into it. Physical
        # booths and other districts remain unchanged.
        np.testing.assert_array_equal(after['unit_means'][[0,1,4,5,6,7]],before['unit_means'][[0,1,4,5,6,7]])
        self.assertGreaterEqual(after['unit_means'][3],before['unit_means'][3])
        self.assertLess(after['unit_means'][:4].sum(),before['unit_means'][:4].sum())
        np.testing.assert_array_equal(after['no_addition_probability'],before['no_addition_probability'])
        for a,b in zip(after['components'],before['components']):
            self.assertEqual(a['probabilities']['batch'],b['probabilities']['batch'])
        from scipy.special import expit
        self.assertAlmostEqual(after['components'][0]['postal_remainder_median_fraction'],expit(completion.postal_remainder_log_odds(4)))
        later = [dict(e,postal_days_before_deadline=0) for e in evidence]
        final = late_counts.update(local,inputs,units,later)
        self.assertLess(final['unit_means'][2],after['unit_means'][2])
        self.assertGreater(final['unit_means'][2],units[2]['counted'])
        self.assertTrue((final['counts'] >= final['counted_groups']-1e-9).all())

    def test_local_completion_releases_votes_without_inflating_siblings(self):
        inputs, units, prepared = self.prepared_case()
        # Choose only one category's original uncertainty as new evidence. All
        # existing progress and exceptional-batch draws are identical in both
        # runs, including those for another category in the same district.
        reference = dict(prepared)
        reference.pop('original_unit_counts')
        local = dict(prepared)
        local['original_unit_counts'] = np.broadcast_to(prepared['original_unit_counts'].mean(0),
            prepared['original_unit_counts'].shape).copy()
        local['original_unit_counts'][:,2] = prepared['original_unit_counts'][:,2]
        progress = [dict(strength=.25) for _ in units]
        before = late_counts.update(reference,inputs,units,progress)
        after = late_counts.update(local,inputs,units,progress)
        self.assertLess(after['unit_means'][2],before['unit_means'][2])
        np.testing.assert_allclose(np.delete(after['unit_means'],2),np.delete(before['unit_means'],2),rtol=0,atol=1e-10)
        self.assertTrue((after['totals'] <= before['totals']+1e-10).all())
        self.assertTrue((after['counts'] >= after['counted_groups']-1e-10).all())
        np.testing.assert_array_equal(before['no_addition_probability'],after['no_addition_probability'])
        for old,new in zip(before['components'],after['components']):
            self.assertEqual(old['probabilities']['batch'],new['probabilities']['batch'])
        # Conditioned expectations are not allowed to become the next prior.
        altered = dict(local,unit_counts=prepared['unit_counts']*1.5,remaining=prepared['remaining']*1.5)
        np.testing.assert_array_equal(completion.evidence(local,inputs,units,progress)[0],
                                      completion.evidence(altered,inputs,units,progress)[0])

    def prepared_case(self):
        inputs, draws, units = live_case()
        units[0]['counted'], units[1]['counted'] = 450, 200
        units[2]['counted'], units[3]['counted'] = 90, 30
        prepared = live.update(draws, inputs, units)
        return inputs, units, prepared

    def test_no_progress_or_original_size_evidence_preserves_broad_prediction(self):
        inputs, units, prepared = self.prepared_case()
        # A missing original size gives no local completion information. With
        # it present, a just-arrived count can now inform completion even when
        # no quietness has yet been observed.
        prepared.pop('original_unit_counts')
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
        np.testing.assert_allclose(neutral['totals'],np.repeat(prepared['totals'],8,axis=0),rtol=0,atol=1e-12)
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

"""Check count evidence, retained backlogs, sibling isolation and physical limits."""
import copy
import unittest

import numpy as np

from lib.turnout import late_counts, live, routine_remainders as routine


class RoutineRemainderTests(unittest.TestCase):
    def account(self):
        inputs = dict(election_code='fictional',seat_names=['Example'],
            categories=['ordinary','postal','absent','early-absent','provisional'],enrolment=[24000.])
        base = np.array([11000.,3200.,1800.,2500.,100.])
        factors = np.array([.85,.95,1.05,1.15])
        counts = base[None,None,:]*factors[:,None,None]
        draws = dict(counts=counts,totals=counts.sum(axis=2))
        names = ['Ordinary','Postal','Absent','PrePoll','Provisional']
        current = [10400.,2800.,1350.,1800.,70.]
        units = [dict(seat_index=0,seat_name='Example',group_index=g,group=inputs['categories'][g],
            name=name,vote_type=name,kind='ordinary' if g==0 else 'declaration',weight=1.,
            counted=current[g],matched=True) for g,name in enumerate(names)]
        prepared = live.update(draws,inputs,units)
        evidence = [dict(strength=.15,coverage=.9,started=u['counted']/(u['counted']+10),
            volume=30.,pooled=dict(volume=.01),positive_remainder_support=.3) for u in units]
        return inputs,units,prepared,evidence

    def test_processing_is_an_expected_amount_with_broad_local_and_shared_variation(self):
        rng = np.random.default_rng(123)
        n = 100000
        local = rng.standard_normal((n,2))
        shared = rng.standard_normal((n,1))
        reduced,_ = routine.alternatives(np.full((n,2),1e6),np.full((n,2),1e10),
            np.full((n,2),1e6),100.,1.,local,shared)
        np.testing.assert_allclose(reduced.mean(axis=0),100.,rtol=.03)
        np.testing.assert_allclose(np.log(reduced).std(axis=0),1.25,atol=.02)
        self.assertAlmostEqual(np.corrcoef(np.log(reduced).T)[0,1],.5**2/1.25**2,delta=.02)

    def test_maturity_is_gradual_and_cannot_declare_completion(self):
        inputs,units,prepared,evidence = self.account()
        retention = np.ones(4)
        original = prepared['original_unit_counts'].copy()
        for fraction,expected_phase in [(.3,.15517241379310345),(.5,.5),(.75,.9),(1.5,1.)]:
            for j in [2,3]:units[j]['counted']=fraction*original[:,j].mean()
            support,amount,phase=routine.parameters(prepared,units,[1,2,3,4],evidence,retention)
            np.testing.assert_allclose(phase[[1,2]],expected_phase)
            self.assertGreater(amount[1],0)
            self.assertGreater(support[1],0)
            self.assertEqual(support[0],0)  # Postal receipt has its own path.
            self.assertEqual(support[3],0)  # Provisional is unchanged.

    def test_joint_account_preserves_siblings_counts_and_original_component_probabilities(self):
        inputs,units,prepared,evidence = self.account()
        prior_copy=copy.deepcopy(prepared)
        base=late_counts.update(prepared,inputs,units,evidence,32,123,days_after_poll_close=9,
            declaration_remainders=False)
        result=late_counts.update(prepared,inputs,units,evidence,32,123,days_after_poll_close=9)
        np.testing.assert_array_equal(base['counts'][:,:,0:2],result['counts'][:,:,0:2])
        np.testing.assert_array_equal(base['counts'][:,:,4],result['counts'][:,:,4])
        np.testing.assert_array_equal(base['no_addition_probability'],result['no_addition_probability'])
        self.assertGreater(np.max(abs(base['unit_means']-result['unit_means'])),1.)
        self.assertLess(result['diagnostics']['maximum_accounting_error'],1e-7)
        self.assertGreaterEqual(result['diagnostics']['minimum_category_addition'],-1e-7)
        self.assertGreaterEqual(result['diagnostics']['minimum_unused_enrolment'],-1e-7)
        for before,after in zip(base['components'],result['components']):
            p,q=before['probabilities'],after['probabilities']
            self.assertAlmostEqual(sum(q.values()),1.)
            self.assertEqual(p['batch'],q['batch'])
            self.assertEqual(p['no_additions'],q['no_additions'])
            if after['category'] in routine.CATEGORIES:
                self.assertGreater(q['reduced_routine'],0.)
                self.assertGreater(q['modest_remainder'],0.)
                self.assertAlmostEqual(p['previous'],q['previous']+q['reduced_routine'])
                self.assertAlmostEqual(p['small'],q['small']+q['modest_remainder'])
            else:self.assertEqual(before['probabilities'],after['probabilities'])
        for field in ('totals','counts','original_unit_counts'):
            np.testing.assert_array_equal(prepared[field],prior_copy[field])

    def test_no_history_and_unstarted_counts_do_not_invent_progress(self):
        inputs,units,prepared,_=self.account()
        evidence=[dict(strength=0.) for _ in units]
        off=late_counts.update(prepared,inputs,units,evidence,8,10,declaration_remainders=False)
        on=late_counts.update(prepared,inputs,units,evidence,8,10)
        for field in ('totals','counts','unit_means','no_addition_probability'):
            np.testing.assert_array_equal(off[field],on[field])
        for j in [2,3]:units[j]['counted']=0
        measured=[dict(coverage=1.,started=1.,volume=100.,pooled=dict(volume=.1)) for _ in units]
        support,_,_=routine.parameters(prepared,units,[1,2,3,4],measured,np.ones(4))
        np.testing.assert_array_equal(support,np.zeros(4))

    def test_retained_backlogs_and_exceptional_outcomes_cannot_be_reclassified(self):
        point=np.array([True,True,True,False,False])
        finishing=np.array([False,True,False,False,False])
        processing=np.array([False,False,True,False,False])
        batch_share=np.array([0.,0.,0.,0.,1.])
        lower,higher=routine.choice_weights(point,finishing,processing,batch_share,.8,.4)
        np.testing.assert_allclose(lower,[.8,0,0,0,0])
        np.testing.assert_allclose(higher,[0,.4,0,.4,0])
        # No physical room remains: a shape calculation must stay finite and
        # retain the zero amount instead of inventing a future vote.
        reduced,modest=routine.alternatives(np.array([0.]),np.array([0.]),np.array([0.]),10.,1.,0.,0.)
        np.testing.assert_array_equal(reduced,[0.])
        np.testing.assert_array_equal(modest,[0.])


if __name__=='__main__':unittest.main()

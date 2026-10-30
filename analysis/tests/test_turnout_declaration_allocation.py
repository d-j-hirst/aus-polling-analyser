"""Check that discarded postal allowances cannot displace other categories."""
import unittest
import numpy as np
from lib.turnout.declaration_allocation import allocate


class DeclarationAllocationTests(unittest.TestCase):
    def test_discarded_postal_amount_has_no_remaining_influence(self):
        for discarded in (200., 2000., 20000.):
            actual = allocate([10000.],np.log([[discarded/8500.,1000./8500.]]),[[100.,np.inf]])
            np.testing.assert_allclose(actual,[[100.,9900./9.5]],rtol=1e-13)

    def test_postal_increase_cannot_reduce_combined_additions(self):
        remaining = np.linspace(0.,3000.,1001)
        odds = np.tile(np.log([20000./8500.,1000./8500.]),(len(remaining),1))
        actual = allocate(np.full(len(remaining),10000.),odds,np.column_stack([remaining,np.full(len(remaining),np.inf)]))
        np.testing.assert_allclose(actual[:,0],remaining,atol=1e-10)
        self.assertTrue((np.diff(actual.sum(axis=1))>=-1e-9).all())
        self.assertTrue((np.diff(actual[:,1])<=1e-9).all())

    def test_multiple_limits_conserve_capacity_and_ignore_category_order(self):
        rng = np.random.default_rng(10129)
        parent = rng.uniform(1000.,20000.,2000)
        odds = rng.normal(0.,3.,(len(parent),5))
        limits = rng.uniform(0.,2000.,odds.shape)
        limits[rng.random(odds.shape)<.5] = np.inf
        actual = allocate(parent,odds,limits)
        self.assertTrue((actual>=0).all() and (actual<=limits+1e-8).all())
        self.assertTrue((actual.sum(axis=1)<=parent+1e-8).all())
        order = [4,2,0,3,1]
        np.testing.assert_allclose(actual[:,order],allocate(parent,odds[:,order],limits[:,order]),atol=1e-8)
        raised = limits.copy(); raised[:,0] += 100
        self.assertTrue((allocate(parent,odds,raised).sum(axis=1)>=actual.sum(axis=1)-1e-8).all())

    def test_unconstrained_allocation_and_extreme_discarded_weight(self):
        odds = np.array([[.3,-.8,1.2],[-2.,3.,-.5]])
        parent = np.array([10000.,20000.])
        expected = parent[:,None]*np.exp(odds)/(1+np.exp(odds).sum(axis=1))[:,None]
        np.testing.assert_allclose(allocate(parent,odds,np.inf),expected,rtol=1e-14)
        np.testing.assert_allclose(allocate([10000.],[[10000.,np.log(1000./8500.)]],[[100.,np.inf]]),
                                  [[100.,9900./9.5]],rtol=1e-13)
        np.testing.assert_array_equal(allocate([10000.],[[0.,0.]],[[0.,0.]]),[[0.,0.]])


if __name__ == '__main__':
    unittest.main()

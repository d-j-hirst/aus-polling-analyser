"""Check allocation experiments preserve counts, bounds and smooth transitions."""

import unittest

import numpy as np

from lib.turnout import allocation_experiment as allocation,live
from tests.test_turnout_live_prototype import live_case


class AllocationExperimentTests(unittest.TestCase):
    def case(self):
        inputs,draws,units = live_case()
        units[0]['counted'],units[1]['counted'] = 450,200
        units[2]['counted'],units[3]['counted'] = 90,30
        broad = live.update(draws,inputs,units)
        own = allocation.own_additions(draws,broad,units,units)
        evidence = {(u['seat_name'],'allocation:'+u['group']):.4 for u in units}
        return inputs,draws,units,broad,own,evidence

    def test_neutral_progress_retains_current_prediction(self):
        inputs,_,units,broad,own,_ = self.case()
        result,weights = allocation.prepare(broad,inputs,units,own,{},'gradual',.3)
        np.testing.assert_array_equal(weights,np.zeros(len(units)))
        np.testing.assert_array_equal(result['totals'],broad['totals'])

    def test_release_preserves_booths_counted_account_and_enrolment(self):
        inputs,_,units,broad,own,evidence = self.case()
        for mode in ('immediate','gradual','directional'):
            result,_ = allocation.prepare(broad,inputs,units,own,evidence,mode,.3)
            np.testing.assert_array_equal(result['counted'],broad['counted'])
            np.testing.assert_array_equal(result['unit_counts'][:,:2],broad['unit_counts'][:,:2])
            self.assertTrue((result['remaining']>=-1e-10).all())
            self.assertTrue((result['totals']<inputs['enrolment']).all())
            np.testing.assert_allclose(result['counts'].sum(axis=2),result['totals'])

    def test_small_progress_change_has_small_effect(self):
        inputs,_,units,broad,own,evidence = self.case()
        first,_ = allocation.prepare(broad,inputs,units,own,evidence,'gradual',.3)
        second,_ = allocation.prepare(broad,inputs,units,own,{k:v+1e-7 for k,v in evidence.items()},'gradual',.3)
        self.assertLess(np.abs(first['totals']-second['totals']).max(),.01)

    def test_extra_spread_only_applies_to_assumed_finer_splits(self):
        inputs,draws,units,broad,own,_ = self.case()
        # This fixture has one unit per supported category: its historical
        # category uncertainty already exists, so the extra assumption is idle.
        extra = allocation.own_additions(draws,broad,units,units,.5)
        np.testing.assert_array_equal(extra,own)

    def test_directional_release_is_smooth_across_equal_estimates(self):
        inputs,_,units,broad,_,evidence = self.case()
        # Crossing the aggregate estimate must not trigger a jump between a
        # permitted decrease and an excluded increase. Both remain possible.
        first,_ = allocation.prepare(broad,inputs,units,broad['remaining']*(1-1e-7),evidence,'directional',.5)
        second,_ = allocation.prepare(broad,inputs,units,broad['remaining']*(1+1e-7),evidence,'directional',.5)
        self.assertLess(np.abs(first['totals']-second['totals']).max(),.01)


if __name__ == '__main__':
    unittest.main()

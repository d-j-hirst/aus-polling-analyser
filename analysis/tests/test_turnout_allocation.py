"""Check the adopted allocation rule uses measured groups and preserves counts."""

import unittest

import numpy as np

from lib.turnout import allocation, allocation_experiment, late_counts, live
from tests.test_turnout_live_prototype import live_case


class AllocationTests(unittest.TestCase):
    def case(self):
        inputs,draws,units = live_case()
        for j,value in enumerate((450,200,90,30,450,200,90,30)):
            units[j]['counted'] = value
        prepared = live.update(draws,inputs,units)
        grouped = allocation.group_observation(units,inputs)
        seats = {name:dict(vote_types={**row['vote_types'],**{
            late_counts.category_key(u):u['counted'] for u in units
            if u['seat_name']==name and u['kind']=='declaration'}}) for name,row in grouped.items()}
        history = [dict(source_time=f'2025-05-{day:02d}T18:00:00',seats=seats) for day in range(3,11)]
        return inputs,draws,units,prepared,history

    def test_reuses_the_unscaled_estimate_from_the_existing_count_update(self):
        inputs,draws,units,prepared,_ = self.case()
        original = allocation_experiment.own_additions(draws,prepared,units,units)
        columns = [j for j,u in enumerate(units) if u['kind']=='declaration']
        np.testing.assert_allclose(prepared['own_remaining'][:,columns],original[:,columns],atol=1e-10)

    def test_quiet_history_releases_allowances_without_changing_counts_or_booths(self):
        inputs,draws,units,prepared,history = self.case()
        grouped = allocation.group_evidence(history,units,inputs)
        revised,weights = allocation.release(prepared,inputs,units,prepared['own_remaining'],grouped)
        self.assertGreater(weights.max(),0)
        np.testing.assert_array_equal(revised['counted'],prepared['counted'])
        booths = [j for j,u in enumerate(units) if u['kind']!='declaration']
        np.testing.assert_array_equal(revised['unit_counts'][:,booths],prepared['unit_counts'][:,booths])
        np.testing.assert_allclose(revised['counts'].sum(axis=2),revised['totals'])
        self.assertTrue((revised['totals']<inputs['enrolment']).all())
        self.assertTrue((revised['remaining']>=0).all())
        combined = live.update_with_progress(draws,inputs,units,history)
        np.testing.assert_array_equal(combined['allocation_prediction']['totals'],revised['totals'])

    def test_missing_group_observations_do_not_invent_slowing_evidence(self):
        inputs,_,units,prepared,history = self.case()
        for row in history:
            for seat in row['seats'].values():
                seat['vote_types'] = {key:value for key,value in seat['vote_types'].items()
                                     if not key.startswith('allocation:') and key!='Postal'}
        grouped = allocation.group_evidence(history,units,inputs)
        revised,weights = allocation.release(prepared,inputs,units,prepared['own_remaining'],grouped)
        np.testing.assert_array_equal(weights,0)
        np.testing.assert_array_equal(revised['totals'],prepared['totals'])


if __name__ == '__main__':
    unittest.main()

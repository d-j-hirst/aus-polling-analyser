"""Check source-only signals and the unit of comparison in the progress grid."""

import unittest

from scripts.turnout.turnout_declaration_progress import quiet_cases
from scripts.turnout.turnout_declaration_thresholds import observations, selection_summary, smooth_weight


def snapshot(day, values):
    return dict(source_time=f'2025-05-{3+day:02d}T18:00:00', day=day,
                seats={seat:dict(vote_types={'Postal':value} if value is not None else {})
                       for seat,value in values.items()})


class DeclarationThresholdTests(unittest.TestCase):
    def test_default_quiet_selection_matches_existing_audit(self):
        counts = {str(i):1000 for i in range(10)}
        history = [snapshot(day,dict(counts, moving=1000+day*100, unknown=None, unstarted=0))
                   for day in range(4)]
        final = {seat:{'Postal':1200} for seat in counts}
        rows = observations(history,['Postal'],final)
        original = quiet_cases(history,['Postal'],final)
        self.assertEqual(len(rows),len(original))
        self.assertTrue(all(r['local_movement_ratio'] == 0 for r in rows))
        self.assertEqual(rows[0]['active_fraction'],original[0]['district_activity_fraction'])
        self.assertEqual(rows[0]['pooling_districts'],12)
        self.assertEqual(rows[0]['started_fraction'],11/12)

    def test_final_scoring_cannot_change_continuous_evidence_weight(self):
        counts = {str(i):1000 for i in range(10)}
        history = [snapshot(day,counts) for day in range(4)]
        small = observations(history,['Postal'],{'0':{'Postal':1000}})[0]
        large = observations(history,['Postal'],{'0':{'Postal':50000}})[0]
        self.assertNotEqual(small['net_remaining'],large['net_remaining'])
        self.assertEqual(smooth_weight(small),smooth_weight(large))
        # Weakening any one piece of current evidence reduces the confidence
        # that counting is ending; a later final target cannot compensate it.
        self.assertLess(smooth_weight(dict(small,activity_evidence_mean=.2)),smooth_weight(small))
        self.assertLess(smooth_weight(dict(small,activity_percent=1.)),smooth_weight(small))

    def test_repeated_late_observations_do_not_hide_first_signal_failure(self):
        first = dict(seat='A',category='Postal',source_timestamp='2025-05-17',
                     day=14,current=1000,net_remaining=500)
        last = dict(first,source_timestamp='2025-05-24',day=21,current=1500,net_remaining=0)
        result = selection_summary([last,first,last,last])
        self.assertEqual(result['district_categories'],1)
        self.assertEqual(result['substantial'],1)
        self.assertEqual(result['mean_positive_addition'],500)


if __name__ == '__main__':
    unittest.main()

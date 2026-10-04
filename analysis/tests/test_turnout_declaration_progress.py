"""Check that the progress audit does not confuse unstarted or revised counts with completion."""

import unittest

from scripts.turnout.turnout_declaration_progress import quiet_cases


def snapshot(day, values):
    return dict(source_time=f'2025-05-{3+day:02d}T18:00:00', day=day,
                seats={seat:dict(vote_types={'Postal':value} if value is not None else {})
                       for seat,value in values.items()})


class DeclarationProgressTests(unittest.TestCase):
    def test_quiet_started_booth_does_not_hide_unstarted_or_unknown_districts(self):
        history = [snapshot(day,{'A':100,'B':0,'C':None}) for day in range(4)]
        rows = quiet_cases(history,['Postal'],{'A':{'Postal':1000},'B':{'Postal':1000}})
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0]['pooling_districts'],2)
        self.assertEqual(rows[0]['started_district_fraction'],.5)
        self.assertEqual(rows[0]['district_activity_fraction'],0)
        self.assertEqual(rows[0]['net_remaining'],900)

    def test_reversed_revision_is_activity_and_large_time_gap_is_not_a_quiet_window(self):
        final = {'A':{'Postal':100}}
        history = [snapshot(0,{'A':100}),snapshot(1,{'A':200}),snapshot(3,{'A':100})]
        self.assertEqual(quiet_cases(history,['Postal'],final),[])
        gap = [snapshot(0,{'A':100}),snapshot(6,{'A':100})]
        self.assertEqual(quiet_cases(gap,['Postal'],final),[])


if __name__ == '__main__':
    unittest.main()

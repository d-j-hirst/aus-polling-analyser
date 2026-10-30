"""Check independent processing clocks without feeds or election fitting."""
import copy
from datetime import datetime, timedelta
import unittest

import numpy as np
from lib.turnout import late_schedule, processing_timetables


class ProcessingTimetableTests(unittest.TestCase):
    close = '2025-03-08T18:00:00'
    deadline = '2025-03-13T09:00:00'

    def inputs(self, day=12, active=False):
        names = ['Absent', 'PrePoll', 'Provisional', 'Postal', 'SIR']
        units = [dict(seat_name='Example', seat_index=0, name=c, kind='declaration', counted=100.) for c in names]
        now = datetime.fromisoformat(self.close)+timedelta(days=day)
        history = [dict(source_time=(now-timedelta(hours=6*(8-k))).isoformat(),
            seats={'Example': dict(vote_types={c: (90. if active and k<8 else 100.) for c in names})})
            for k in range(9)]
        return units, history

    def test_defaults_and_independent_postal_clock(self):
        a = processing_timetables.resolve(self.close,self.deadline)
        b = processing_timetables.resolve(self.close,'2025-03-20T09:00:00')
        for category, days in [('Absent',14),('PrePoll',14),('Provisional',18)]:
            self.assertEqual(a[category],b[category])
            self.assertEqual((a[category]['reference']-datetime.fromisoformat(self.close)).days,days)
        self.assertNotEqual(a['Postal'],b['Postal'])
        self.assertNotIn('SIR',a)
        self.assertNotIn('Absent',processing_timetables.resolve(receipt_deadline=self.deadline))

    def test_progress_and_batch_amounts_do_not_inherit_postal_deadline(self):
        units, history = self.inputs(active=True)
        all_results = []
        for deadline in [self.deadline,'2025-03-25T09:00:00']:
            evidence = late_schedule.progress_evidence(history,units,{},deadline,np.ones(len(units)),poll_close=self.close)
            schedule = late_schedule.schedule(history,units,evidence,deadline,poll_close=self.close)
            all_results.append((evidence,schedule))
        for j in [0,1,2,4]:
            self.assertEqual(all_results[0][0][j],all_results[1][0][j])
            for key in ['usual_log_shifts','processing_phase','batch_survival']:
                self.assertEqual(all_results[0][1][key][j],all_results[1][1][key][j])
        self.assertNotEqual(all_results[0][0][3],all_results[1][0][3])

    def test_wa_override_is_gradual_and_does_not_move_siblings(self):
        units, history = self.inputs(day=14)
        evidence = [dict(volume=0.) for _ in units]
        old = late_schedule.schedule(history,units,evidence,self.deadline,poll_close=self.close)
        override = {'Absent': dict(days_after_poll_close=16,transition_days=2)}
        new = late_schedule.schedule(history,units,evidence,self.deadline,poll_close=self.close,processing_overrides=override)
        self.assertAlmostEqual(np.exp(old['usual_log_shifts'][0]),.5)
        self.assertAlmostEqual(np.exp(new['usual_log_shifts'][0]),1/(1+np.exp(-1)))
        for key in new:
            np.testing.assert_array_equal(old[key][1:],new[key][1:])
        later = copy.deepcopy(history)
        later[-1]['source_time']=(datetime.fromisoformat(later[-1]['source_time'])+timedelta(minutes=1)).isoformat()
        near = late_schedule.schedule(later,units,evidence,self.deadline,poll_close=self.close,processing_overrides=override)
        self.assertLess(abs(near['processing_phase'][0]-new['processing_phase'][0]),.001)
        self.assertGreater(new['batch_survival'][0],.9)

    def test_no_history_no_results_and_explicit_disable_are_neutral(self):
        units, history = self.inputs(day=30)
        evidence = [dict(volume=0.) for _ in units]
        for observed in [[],history]:
            zero = [dict(u,counted=0.) for u in units]
            schedule = late_schedule.schedule(observed,zero,evidence,self.deadline,poll_close=self.close)
            np.testing.assert_array_equal(schedule['usual_log_shifts'],np.zeros(5))
        schedule = late_schedule.schedule(history,units,evidence,self.deadline,poll_close=self.close,
            processing_overrides={'Absent':None})
        self.assertEqual(schedule['usual_log_shifts'][0],0.)
        self.assertEqual(schedule['processing_phase'][0],0.)
        self.assertLess(schedule['usual_log_shifts'][1],0.)

    def test_bad_settings_rejected(self):
        for value in [[],{'Postal':{}},{'Absent':False},{'Absent':{'days_after_poll_close':True}},
                      {'Absent':{'days_after_poll_close':-1}},
                      {'Absent':{'days_after_poll_close':14,'transition_days':0}},
                      {'Absent':{'days_after_poll_close':float('nan')}}]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                processing_timetables.resolve(overrides=value)

    def test_separate_postal_processing_preserves_receipt_and_other_categories(self):
        units, history = self.inputs(day=12)
        options = {'Postal': dict(days_after_poll_close=13,transition_days=2)}
        old = late_schedule.progress_evidence(history,units,{},self.deadline,np.ones(1),poll_close=self.close)
        new = late_schedule.progress_evidence(history,units,{},self.deadline,np.ones(1),poll_close=self.close,
                                              processing_overrides=options)
        for j in [0,1,2,4]:
            self.assertEqual(old[j],new[j])
        for key in ['postal_receipt_support','postal_days_before_deadline']:
            self.assertEqual(old[3][key],new[3][key])
        self.assertLess(new[3]['strength'],old[3]['strength'])
        self.assertLess(new[3]['postal_finishing_support'],old[3]['postal_finishing_support'])
        first = processing_timetables.resolve(self.close,self.deadline,options)
        later_receipt = processing_timetables.resolve(self.close,'2025-03-20T09:00:00',options)
        self.assertEqual(first,later_receipt)
        self.assertTrue(first['Postal']['separate_postal_processing'])
        self.assertNotIn('Postal',processing_timetables.resolve(self.close,self.deadline,{'Postal':None}))
        self.assertNotIn('Postal',processing_timetables.resolve(receipt_deadline=self.deadline,overrides=options))


if __name__ == '__main__':
    unittest.main()

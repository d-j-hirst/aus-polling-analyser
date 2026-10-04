"""Check the count boundaries of the explicitly reviewed retrospective repairs."""

import copy
import unittest

from lib.turnout import sa_hindcast


def snapshot(name, groups):
    categories = {c:sum(r.get(c,0) for r in groups.values())
                  for c in ('Absent','Provisional','PrePoll','Early Provisional')}
    return dict(source_time='2026-03-29T18:00:00',seats={name:dict(
        counted=sum(categories.values()),vote_types=categories,
        booths={sa_hindcast.LABELS[c]:v for c,v in categories.items()},candidate_groups=groups)})


class SaHindcastTests(unittest.TestCase):
    def test_relabelling_preserves_every_candidate_and_partial_provisional_batch(self):
        groups = {'108001':{'Absent':0,'Provisional':110,'PrePoll':0,'Early Provisional':50},
                  '108002':{'Absent':0,'Provisional':25,'PrePoll':0,'Early Provisional':30}}
        source = snapshot('Croydon',groups)
        untouched = copy.deepcopy(source)
        reviewed = {'Croydon':{'1':{'Absent':100,'Provisional':15,'PrePoll':50,'Early Provisional':0},
                              '2':{'Absent':20,'Provisional':10,'PrePoll':30,'Early Provisional':0}}}
        result = sa_hindcast.apply_reviewed_overrides(source,reviewed)
        seat = result['seats']['Croydon']
        self.assertEqual(seat['vote_types'],{'Absent':120,'Provisional':15,'PrePoll':80,'Early Provisional':0})
        for i,r in groups.items():
            self.assertEqual(sum(r.values()),sum(seat['candidate_groups'][i].values()))
        self.assertEqual(source,untouched)
        self.assertEqual(seat['counted'],source['seats']['Croydon']['counted'])
        self.assertEqual(sa_hindcast.apply_reviewed_overrides(result,reviewed)['seats'],result['seats'])

    def test_incomplete_unidentified_batch_is_not_filled_using_final_results(self):
        source = snapshot('Croydon',{'108001':{'Provisional':10,'Early Provisional':5}})
        reviewed = {'Croydon':{'1':{'Absent':100,'Provisional':15,'PrePoll':50,'Early Provisional':0}}}
        result = sa_hindcast.apply_reviewed_overrides(source,reviewed)
        self.assertEqual(result['seats'],source['seats'])
        self.assertEqual(result['hindcast_overrides'],[])

    def test_missing_record_is_unknown_and_positive_revision_is_retained(self):
        source = snapshot('Flinders',{'111001':{'Absent':10,'PrePoll':0}})
        result = sa_hindcast.apply_reviewed_overrides(source,{}, {'Flinders':['PrePoll']})
        self.assertIsNone(result['seats']['Flinders']['vote_types']['PrePoll'])
        self.assertEqual(result['seats']['Flinders']['counted'],10)
        self.assertEqual(source['seats']['Flinders']['vote_types']['PrePoll'],0)
        revised = snapshot('Flinders',{'111001':{'Absent':10,'PrePoll':20}})
        retained = sa_hindcast.apply_reviewed_overrides(revised,{}, {'Flinders':['PrePoll']})
        self.assertEqual(retained['seats'],revised['seats'])


if __name__ == '__main__':
    unittest.main()

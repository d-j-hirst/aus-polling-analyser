"""Fictional ECQ accounts verify roster, history and turnout preparation."""
import copy
import unittest
import xml.etree.ElementTree as ET

from scripts.elections.prepare_qld_live import historical_results, data_fingerprint
from scripts.turnout.turnout_prepare_qld import configuration, group_for
from lib.turnout import maintained_parameters


class QueenslandPreparationTests(unittest.TestCase):
    def test_official_history_and_missing_tcp(self):
        election = ET.fromstring('''<election electionName="Fictional State General Election">
          <districts><district districtName="Coast"><candidates>
            <candidate ballotName="ONE, A" party="Party One"/>
            <candidate ballotName="TWO, B" party="Party Two"/>
          </candidates><countRound countName="Official First Preference Count">
            <totalFormalVotes><count>100</count></totalFormalVotes><booths>
              <booth name="School"><formalVotes>100</formalVotes><primaryVoteResults>
                <candidate ballotName="ONE, A"><count>40</count></candidate>
                <candidate ballotName="TWO, B"><count>60</count></candidate>
              </primaryVoteResults></booth>
            </booths></countRound></district></districts></election>''')
        account = historical_results(election)['Coast']['booths']['School']
        self.assertEqual(account, dict(fp={'0': 40, '1': 60}, tcp={}))
        election.find('.//formalVotes').text = '101'
        with self.assertRaisesRegex(ValueError, 'reconcile'):
            historical_results(election)

    def test_metadata_filter_retains_revisions(self):
        original = ET.fromstring('<election id="42"><generationDateTime>old</generationDateTime><district lastUpdated="old" percentRollCounted="5"><count>10</count><percentage>20</percentage></district></election>')
        metadata = copy.deepcopy(original)
        metadata.find('district').set('lastUpdated', 'new')
        metadata.find('generationDateTime').text = 'new'
        metadata.find('.//percentage').text = '21'
        self.assertEqual(data_fingerprint(original), data_fingerprint(metadata))
        metadata.find('.//count').text = '11'
        self.assertNotEqual(data_fingerprint(original), data_fingerprint(metadata))
        metadata.find('.//count').text = '10'
        self.assertEqual(data_fingerprint(original), data_fingerprint(metadata))

    def test_category_accounts_and_mobile_resumption(self):
        self.assertEqual(group_for('Absent Early Voting'), 'early_declaration')
        self.assertEqual(group_for('Mobile Polling'), 'ordinary_other')
        names = ['School', 'Coast Early Voting Centre', 'Mobile Polling',
            'Absent Early Voting', 'Absent Election Day', 'Postal Declaration Votes',
            'In Person Declaration Votes', 'Telephone Voting', 'Telephone Voting - Early Voting']
        sizes = [500, 300, 0, 100, 50, 200, 20, 10, 5]
        election = ET.Element('election');districts = ET.SubElement(election,'districts')
        district = ET.SubElement(districts,'district',districtName='Coast',enrolment='1500')
        booths = ET.SubElement(ET.SubElement(district,'countRound'),'booths')
        for name in names: ET.SubElement(booths,'booth',name=name)
        previous = {'Coast':dict(booths={n:dict(fp={'1':v},tcp={}) for n,v in zip(names,sizes)})}
        historical = dict(seat_totals=[dict(seat_name='Coast',enrolment=1400,
            total_ballots=1250,formal_votes=sum(sizes))])
        mobile = dict(vote_types=[dict(seat_name='Coast',canonical_category='mobile_or_institution',formal_votes=35)])
        settings = dict(election='2024qld',parameter_preset='2024qld/earlier_only',
            operational_counts=dict(early=500,postal=250),preparation_samples=256,count_draws=8,
            schedule=dict(ppvc_reporting_decay=False,poll_close='2024-10-26T18:00:00'))
        parameters = maintained_parameters.prior_parameters(settings['parameter_preset'])
        config = configuration(settings,election,previous,historical,mobile,parameters)
        units = {u['name']:u for u in config['units']}
        self.assertEqual(len(units),8)  # The two telephone services share one category.
        self.assertEqual(units['PrePoll']['kind'],'declaration')
        self.assertEqual(units['Mobile Polling']['kind'],'ordinary')
        self.assertFalse(units['Mobile Polling']['matched'])
        self.assertGreater(units['Mobile Polling']['weight'],0)
        for group in range(5):
            self.assertAlmostEqual(sum(u['weight'] for u in units.values() if u['group']==group),1)
        self.assertTrue(all(u['counted']==0 for u in units.values()))


if __name__ == '__main__': unittest.main()

"""Check population/provenance rules for additional historical notional TPP."""
import csv
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from scripts.diagnostics.collect_historical_preference_counts import collect_election, matched_record, raw_primaries, serialize


class HistoricalPreferenceCountsTests(unittest.TestCase):
    def setUp(self):
        self.parties = {11:'ALP', 12:'LP', 13:'IND'}
        self.fp = {11:300, 12:400, 13:300}

    def test_matched_population_and_missing_counts(self):
        row, reason = matched_record('2019fed','Hill','booth',91,self.fp,(500,500),self.parties)
        self.assertIsNone(reason)
        self.assertEqual(row[4], '11:300;12:400;13:300')
        for target in [None, (500,499), (250,750), (700,300), (-1,1001)]:
            row, reason = matched_record('2019fed','Hill','booth',91,self.fp,target,self.parties)
            self.assertIsNone(row)
            self.assertIsNotNone(reason)

    def test_stable_unquoted_runtime_format(self):
        row, _ = matched_record('2019fed','Hill','declaration',5,self.fp,(500,500),self.parties)
        contents = serialize([row])
        self.assertNotIn('\r', contents)
        rows = list(csv.DictReader(io.StringIO(contents)))
        self.assertEqual(rows[0]['id'], '5')
        self.assertEqual(rows[0]['labor'], '500')
        self.assertEqual(rows[0]['fp'], '11:300;12:400;13:300')
        with self.assertRaises(ValueError):
            matched_record('2019fed','Bad, name','booth',91,self.fp,(500,500),self.parties)

    def test_historical_lnq_primaries_bound_coalition_tpp(self):
        parties = dict(self.parties)
        parties[12] = 'LNQ'
        # The alias must participate in feasibility checks as well as district
        # selection: 400 Coalition primaries cannot become only 350 TPP votes.
        row, reason = matched_record('2010fed','Hill','booth',91,self.fp,(650,350),parties)
        self.assertIsNone(row)
        self.assertEqual(reason, 'TPP smaller than finalist primary')

    def test_lnq_nonclassic_history_is_retained_but_classic_is_not_duplicated(self):
        def primary(seat, candidate, party, votes):
            row = dict(DivisionNm=seat, CandidateID=str(candidate), PartyNm=party,
                       PartyAb=party, TotalVotes=str(votes))
            row.update({label+'Votes':'0' for label in ['Absent','Provisional','PrePoll','Postal']})
            return row

        tables = {
            'HouseFirstPrefsByCandidateByVoteType':[
                primary('Hill',11,'ALP',300), primary('Hill',12,'LNQ',400),
                primary('Hill',13,'IND',300), primary('Plain',21,'ALP',300),
                primary('Plain',22,'LNQ',700)],
            'HouseTcpByCandidateByVoteType':[
                dict(DivisionNm='Hill',PartyAb='IND'), dict(DivisionNm='Hill',PartyAb='LNQ'),
                dict(DivisionNm='Plain',PartyAb='ALP'), dict(DivisionNm='Plain',PartyAb='LNQ')],
            'HouseTppByPollingPlace':[
                {'DivisionNm':'Hill','PollingPlaceID':'91','Australian Labor Party Votes':'500',
                 'Liberal/National Coalition Votes':'500'}],
        }
        module = 'scripts.diagnostics.collect_historical_preference_counts'
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'feed.zip'
            source.write_bytes(b'fictional commission source')
            with patch(module+'.csv_table',side_effect=lambda year,stem,download,sources:tables[stem]), \
                    patch(module+'.last_feed',return_value=source), \
                    patch(module+'.raw_primaries',return_value={'Hill':{91:self.fp}}) as read_fp:
                rows, manifest = collect_election(2010,Path(folder))
            self.assertEqual(manifest['eligible_seats'], ['Hill'])
            self.assertEqual(manifest['omitted_seats']['Plain'],
                             'classic TCP already supplies the normal comparison')
            self.assertEqual(set(read_fp.call_args.args[1]), {'Hill'})
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0], ['2010fed','Hill','booth',91,
                                      '11:300;12:400;13:300',500,500])

    def test_feed_totals_cannot_silently_change_the_population(self):
        xml = '''<MediaFeed><Contest><ContestIdentifier><ContestName>Hill</ContestName></ContestIdentifier>
          <FirstPreferences><Candidate><CandidateIdentifier Id="11"/><Votes>300</Votes></Candidate>
          <Candidate><CandidateIdentifier Id="12"/><Votes>400</Votes></Candidate>
          <Candidate><CandidateIdentifier Id="13"/><Votes>300</Votes></Candidate></FirstPreferences>
          <PollingPlaces><PollingPlace><PollingPlaceIdentifier Id="91"/><FirstPreferences>
          <Candidate><CandidateIdentifier Id="11"/><Votes>300</Votes></Candidate>
          <Candidate><CandidateIdentifier Id="12"/><Votes>400</Votes></Candidate>
          <Candidate><CandidateIdentifier Id="13"/><Votes>300</Votes></Candidate>
          </FirstPreferences></PollingPlace></PollingPlaces></Contest></MediaFeed>'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'feed.zip'
            with zipfile.ZipFile(path,'w') as archive:
                archive.writestr('xml/aec-mediafeed.xml',xml)
            eligible = {'Hill':dict(fp=self.fp, declarations={})}
            self.assertEqual(raw_primaries(path,eligible)['Hill'][91],self.fp)
            changed = dict(self.fp); changed[13] += 1
            with self.assertRaisesRegex(ValueError,'XML/CSV FP totals disagree'):
                raw_primaries(path,{'Hill':dict(fp=changed,declarations={})})
            with self.assertRaisesRegex(ValueError,'category totals disagree'):
                raw_primaries(path,{'Hill':dict(fp=self.fp,declarations={5:{11:10}})})


if __name__ == '__main__':
    unittest.main()

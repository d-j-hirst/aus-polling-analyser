"""Check population/provenance rules for additional historical notional TPP."""
import csv
import io
from pathlib import Path
import tempfile
import unittest
import zipfile

from scripts.diagnostics.collect_historical_preference_counts import matched_record, raw_primaries, serialize


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

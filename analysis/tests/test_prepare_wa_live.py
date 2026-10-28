"""Check WA archive clocks and ballot identities using small fictional exports."""
import copy
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from scripts.elections.prepare_wa_live import (
    candidate_roster, capture_time, data_fingerprint, prepare_captures, update_running_parties)


class WaPreparationTests(unittest.TestCase):
    def test_collector_clock_observes_sydney_dst(self):
        self.assertEqual(capture_time('250308224521', 'Australia/Sydney'), '20250308194521')
        self.assertEqual(capture_time('250406145852', 'Australia/Sydney'), '20250406125852')
        self.assertEqual(capture_time('250309010000', 'Australia/Sydney'), '20250308220000')

    def test_only_update_clocks_are_ignored(self):
        root = ET.fromstring('<result LastUpdated="old"><count Votes="10" Status="preliminary"/></result>')
        other = copy.deepcopy(root); other.set('LastUpdated', 'new')
        self.assertEqual(data_fingerprint(root), data_fingerprint(other))
        for attribute, value in [('Votes', '11'), ('Status', 'official')]:
            changed = copy.deepcopy(other); changed[0].set(attribute, value)
            self.assertNotEqual(data_fingerprint(root), data_fingerprint(changed))

    def test_assembly_roster_excludes_council_and_distinguishes_unendorsed(self):
        source = '''<ElectionEvent xmlns="http://tempuri.org/MediaExportSchema.xsd" ElectionDate="2025-03-08">
          <ElectionRegion><ElectionDistrict Name="North"><LA>
            <Candidate BallotPaperOrder="1" BallotPaperName="ONE, A" RegisteredPartyAbbreviation="IND" Independent="true"/>
            <Candidate BallotPaperOrder="2" BallotPaperName="TWO, B" RegisteredPartyAbbreviation="" Independent="false"/>
          </LA></ElectionDistrict><LC><Candidate BallotPaperOrder="1" BallotPaperName="COUNCIL, C" RegisteredPartyAbbreviation="IND"/></LC>
          </ElectionRegion></ElectionEvent>'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'candidates.xml'; path.write_text(source)
            date, rows = candidate_roster(path)
        self.assertEqual(date, '2025-03-08')
        self.assertEqual([r['party'] for r in rows], ['IND', 'OTH'])

    def test_running_counts_preserve_authored_seat_assumptions(self):
        rows = [dict(district='North', party=p) for p in ['ALP', 'LIB', 'GRN', 'OTH', 'OTH', 'IND', 'IND']]
        original = '#North\nfTppMargin=7.2\nbConfirmedProminentIndependent=1\nsCandidateNames=Existing,IND\nsRunningParties=ALP,LIB\n'
        updated = update_running_parties(original, rows)
        self.assertIn('sRunningParties=ALP,LIB,GRN,OTH*,IND*', updated)
        self.assertEqual(original.split('sRunningParties=')[0], updated.split('sRunningParties=')[0])

    def test_repeated_counts_move_aside_but_reversions_remain(self):
        def xml(votes, clock):
            return f'''<ElectionEvent xmlns="http://tempuri.org/MediaExportSchema.xsd" ElectionDate="2025-03-08">
              <ElectionRegion><ElectionDistrict Name="North"><LA><Count LastUpdated="{clock}" Votes="{votes}"/>
              </LA></ElectionDistrict></ElectionRegion></ElectionEvent>'''
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder)/'source', Path(folder)/'feeds'; source.mkdir()
            for code, votes, clock in [('250308210000', 10, 'a'), ('250308211000', 10, 'b'),
                                       ('250308212000', 11, 'c'), ('250308213000', 10, 'd')]:
                (source/f'Election - {code}.xml').write_text(xml(votes, clock))
            rows = prepare_captures(source, target, 'Australia/Sydney', '2025-03-08')
            self.assertEqual([r['metadata_only_repeat'] for r in rows], [False, True, False, False])
            self.assertEqual(len(list(target.glob('*.xml'))), 3)
            self.assertEqual(len(list((target/'no-data-change').glob('*.xml'))), 1)
            self.assertEqual(rows, prepare_captures(source, target, 'Australia/Sydney', '2025-03-08'))


if __name__ == '__main__':
    unittest.main()

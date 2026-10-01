from io import BytesIO
import unittest
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import turnout_data
import turnout_qld


def legacy_xml(unknown_type=False, bad_total=False):
    booth_type = 'XX' if unknown_type else 'PB'
    total = 101 if bad_total else 100
    return (
        '<election name="2006 State General Election" date="2006-09-09">'
        '<districts><district number="1" name="Example" enrolment="150">'
        '<formalVotes><count>96</count></formalVotes>'
        '<informalVotes><count>4</count></informalVotes>'
        '<totalVotes>100</totalVotes><totalBallots>100</totalBallots>'
        '<booths>'
        '<booth id="1" name="Example" typeCode="{booth_type}" '
        'typeDescription="Polling Booth"><ballots>{total}</ballots>'
        '<formalVotes>96</formalVotes><informalVotes>4</informalVotes>'
        '<totalVotes>{total}</totalVotes></booth>'
        '</booths></district></districts></election>'
    ).format(booth_type=booth_type, total=total).encode('utf-8')


def current_xml():
    return b'''
        <ecq>
          <election electionName="2020 State General Election"
                    electionDay="2020-10-31" eventType="State General">
            <districts>
              <district districtName="Example" number="1" enrolment="200">
                <countRound id="3" countName="Official First Preference Count"
                            unofficial="NO" preferences="NO">
                  <totalFormalVotes><count>144</count></totalFormalVotes>
                  <totalInformalVotes><count>6</count></totalInformalVotes>
                  <totalVotes>150</totalVotes>
                  <booths>
                    <booth id="1" name="Example" typeCode="PB"
                           typeDescription="Polling Booth">
                      <ballots>80</ballots><formalVotes>76</formalVotes>
                      <informalVotes>4</informalVotes>
                    </booth>
                    <booth id="2" name="Example Early Voting Centre"
                           typeCode="EV"
                           typeDescription="Early Voting Centre Qld">
                      <ballots>50</ballots><formalVotes>49</formalVotes>
                      <informalVotes>1</informalVotes>
                    </booth>
                    <booth id="99991" name="Postal Declaration Votes"
                           typeCode="DV1"
                           typeDescription="Postal Declaration Votes">
                      <ballots>20</ballots><formalVotes>19</formalVotes>
                      <informalVotes>1</informalVotes>
                    </booth>
                  </booths>
                </countRound>
              </district>
            </districts>
          </election>
          <election electionName="Other Event" electionDay="2020-10-31" />
        </ecq>
    '''


class QldTurnoutAdapterTests(unittest.TestCase):
    legacy_election = turnout_qld.QldElection(
        '2006qld',
        '2006-09-09',
        'https://example.test/public.zip',
        '2006 State General Election',
        1,
        'legacy',
    )
    current_election = turnout_qld.QldElection(
        '2020qld',
        '2020-10-31',
        'https://example.test/final.zip',
        '2020 State General Election',
        1,
        'current',
    )

    def test_reads_public_results_from_zip(self):
        output = BytesIO()
        with ZipFile(output, 'w') as archive:
            archive.writestr('folder/publicResults.xml', b'<election />')

        self.assertEqual(
            turnout_qld.read_archive(output.getvalue(), 'fixture'),
            b'<election />',
        )

    def test_rejects_archive_without_exactly_one_public_result(self):
        output = BytesIO()
        with ZipFile(output, 'w') as archive:
            archive.writestr('other.xml', b'<election />')

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'contains 0 publicResults.xml files'
        ):
            turnout_qld.read_archive(output.getvalue(), 'fixture')

    def test_builds_reconciled_legacy_partition(self):
        dataset = turnout_qld.build_dataset(
            self.legacy_election, legacy_xml()
        )

        self.assertEqual(len(dataset.seat_totals), 1)
        self.assertEqual(dataset.seat_totals[0].formal_votes, 96)
        self.assertEqual(len(dataset.vote_types), 1)
        self.assertEqual(
            dataset.vote_types[0].canonical_category,
            'election_day_ordinary',
        )

    def test_classifies_legacy_labelled_modes(self):
        cases = (
            ('PB', 'Polling Booth', 'Returning Officer Example Pre-Poll',
             'early_in_person'),
            ('PB', 'Polling Booth', 'BLV - Telephone', 'telephone'),
            ('PB', 'Polling Booth', 'Mobile Team 1', 'mobile_or_institution'),
            ('PP', 'Pre-Poll (In Person) Absent Votes', 'Declared',
             'declaration_early'),
            ('PR', 'Polling Day Declaration Votes', 'Declared', 'enrolment'),
            ('UI', 'Uncertain Identity', 'Declared', 'provisional'),
        )
        for code, description, name, expected in cases:
            with self.subTest(name=name):
                booth = ET.fromstring(
                    '<booth typeCode="{}" typeDescription="{}" name="{}" />'
                    .format(code, description, name)
                )
                _source, canonical = turnout_qld.classify_booth(
                    booth, 'legacy'
                )
                self.assertEqual(canonical, expected)

    def test_builds_current_partition_and_ignores_other_event(self):
        dataset = turnout_qld.build_dataset(
            self.current_election, current_xml()
        )

        self.assertEqual(dataset.seat_totals[0].total_ballots, 150)
        categories = {
            record.canonical_category: record.total_ballots
            for record in dataset.vote_types
        }
        self.assertEqual(
            categories,
            {
                'early_in_person': 50,
                'election_day_ordinary': 80,
                'postal': 20,
            },
        )

    def test_rejects_unknown_booth_type(self):
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'unsupported ECQ booth type'
        ):
            turnout_qld.build_dataset(
                self.legacy_election, legacy_xml(unknown_type=True)
            )

    def test_rejects_booth_arithmetic_mismatch(self):
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'formal and informal counts do not equal ballots',
        ):
            turnout_qld.build_dataset(
                self.legacy_election, legacy_xml(bad_total=True)
            )


if __name__ == '__main__':
    unittest.main()

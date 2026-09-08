import unittest

import turnout_data
import turnout_wa


def election(layout='contiguous', combined=False, district_names=()):
    return turnout_wa.WaElection(
        election_code='2005wa',
        election_date='2005-02-26',
        report_url='https://example.test/report.pdf',
        table_pages=(0,),
        expected_districts=2,
        layout=layout,
        expected_totals=(300, 150, 30, 15, 45, 6, 246, 9, 255),
        district_names=district_names,
        ordinary_combines_early=combined,
    )


CONTIGUOUS_TABLE = '''
Legislative Assembly Types of Votes by District
District Electors Ordinary Absent Early Votes (by post) Early Votes (in person)
Alpha 100 50 10 5 15 2 82 3 85 85.00%
Beta 200 100 20 10 30 4 164 6 170 85.00%
Total 300 150 30 15 45 6 246 9 255
'''


COMBINED_TABLE = '''
2025 WA State Election Legislative Assembly Types of Votes by District
Alpha 100 65 10 5 2 82 3 85 85.00%
Beta 200 130 20 10 4 164 6 170 85.00%
'''


VERBOSE_RESULTS = b'''<?xml version="1.0" encoding="utf-8"?>
<ElectionEvent ElectionDate="2025-03-08"
    Name="8 March 2025 State General Election"
    xmlns="http://tempuri.org/MediaExportSchema.xsd">
  <ElectionRegion Code="00">
    <ElectionDistrict Code="ALP" Name="Alpha">
      <LA ElectionStatus="Results Declared">
        <DistrictVotes CountDefinitionCode="LAPC" DistrictName="Alpha"
            FormalVotes="82" InformalVotes="3">
          <OrdinaryPollingPlaceVotes OrdinaryPollingPlaceName="Alpha Early Learning Centre"
              FormalVotes="60" InformalVotes="2" />
          <CategoryVotes CategoryCode="SIR" CategoryName="Mobile Polling"
              FormalVotes="5" InformalVotes="0" />
          <CategoryVotes CategoryCode="AV" CategoryName="Absent Votes"
              FormalVotes="10" InformalVotes="0" />
          <CategoryVotes CategoryCode="POV" CategoryName="Postal Votes"
              FormalVotes="5" InformalVotes="1" />
          <CategoryVotes CategoryCode="PRV" CategoryName="Provisional Votes"
              FormalVotes="2" InformalVotes="0" />
        </DistrictVotes>
      </LA>
    </ElectionDistrict>
    <ElectionDistrict Code="BET" Name="Beta">
      <LA ElectionStatus="Results Declared">
        <DistrictVotes CountDefinitionCode="LAPC" DistrictName="Beta"
            FormalVotes="164" InformalVotes="6">
          <OrdinaryPollingPlaceVotes OrdinaryPollingPlaceName="Beta Hall"
              FormalVotes="90" InformalVotes="3" />
          <OrdinaryPollingPlaceVotes OrdinaryPollingPlaceName="Beta Early Polling Place"
              FormalVotes="40" InformalVotes="2" />
          <CategoryVotes CategoryCode="AV" CategoryName="Absent Votes"
              FormalVotes="20" InformalVotes="0" />
          <CategoryVotes CategoryCode="POV" CategoryName="Postal Votes"
              FormalVotes="10" InformalVotes="1" />
          <CategoryVotes CategoryCode="PRV" CategoryName="Provisional Votes"
              FormalVotes="4" InformalVotes="0" />
        </DistrictVotes>
      </LA>
    </ElectionDistrict>
  </ElectionRegion>
</ElectionEvent>
'''


def combined_election():
    return turnout_wa.WaElection(
        '2025wa',
        '2025-03-08',
        'https://example.test/report.pdf',
        (0,),
        2,
        'contiguous-combined',
        (300, 195, 30, 15, 0, 6, 246, 9, 255),
        ordinary_combines_early=True,
        verbose_results_url='http://example.test/final.xml',
    )


class WaTurnoutAdapterTests(unittest.TestCase):
    def test_parses_contiguous_report_rows(self):
        rows = turnout_wa.parse_report_table(CONTIGUOUS_TABLE, election())

        self.assertEqual([row.name for row in rows], ['Alpha', 'Beta'])
        self.assertEqual(rows[0].postal, 5)
        self.assertEqual(rows[1].early, 30)

    def test_parses_split_ordinary_layout_by_official_row_order(self):
        table = '''
        Legislative Assembly Types of Votes by District
        100 10 5 15 2 82 3 85 85.00%
        200 20 10 30 4 164 6 170 85.00%
        District Ordinary
        Alpha 50
        Beta 100
        '''
        rows = turnout_wa.parse_report_table(
            table,
            election('split-ordinary', district_names=('Alpha', 'Beta')),
        )

        self.assertEqual(rows[0].ordinary, 50)
        self.assertEqual(rows[1].ordinary, 100)

        dataset = turnout_wa.build_dataset(
            election('split-ordinary', district_names=('Alpha', 'Beta')),
            table,
        )
        ordinary = [
            record for record in dataset.vote_types
            if record.source_category == 'Ordinary'
        ]
        self.assertEqual(
            {record.derivation for record in ordinary},
            {'difference_official_total'},
        )

    def test_parses_combined_ordinary_and_early_category(self):
        dataset = turnout_wa.build_dataset(
            combined_election(), COMBINED_TABLE, VERBOSE_RESULTS
        )

        self.assertEqual(
            {record.canonical_category for record in dataset.vote_types},
            {
                'election_day_ordinary',
                'early_in_person',
                'mobile_or_institution',
                'absent',
                'postal',
                'provisional',
            },
        )
        by_seat_and_category = {
            (record.seat_name, record.canonical_category): record.formal_votes
            for record in dataset.vote_types
        }
        self.assertEqual(
            by_seat_and_category[('Alpha', 'election_day_ordinary')], 60
        )
        self.assertEqual(by_seat_and_category[('Alpha', 'early_in_person')], 0)
        self.assertEqual(
            by_seat_and_category[('Alpha', 'mobile_or_institution')], 5
        )
        self.assertEqual(by_seat_and_category[('Beta', 'early_in_person')], 40)

    def test_combined_report_requires_verbose_results(self):
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'requires final verbose results XML'
        ):
            turnout_wa.build_dataset(combined_election(), COMBINED_TABLE)

    def test_rejects_verbose_results_that_do_not_reconcile(self):
        mismatched = VERBOSE_RESULTS.replace(
            b'FormalVotes="40" InformalVotes="2"',
            b'FormalVotes="39" InformalVotes="2"',
        ).replace(
            b'FormalVotes="164" InformalVotes="6"',
            b'FormalVotes="163" InformalVotes="6"',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'polling-place split totals'
        ):
            turnout_wa.build_dataset(
                combined_election(), COMBINED_TABLE, mismatched
            )

    def test_rejects_internally_inconsistent_verbose_results(self):
        mismatched = VERBOSE_RESULTS.replace(
            b'FormalVotes="40" InformalVotes="2"',
            b'FormalVotes="39" InformalVotes="2"',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'XML categories total'
        ):
            turnout_wa.build_dataset(
                combined_election(), COMBINED_TABLE, mismatched
            )

    def test_rejects_malformed_verbose_results(self):
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'verbose results XML is invalid'
        ):
            turnout_wa.build_dataset(
                combined_election(), COMBINED_TABLE, b'<ElectionEvent>'
            )

    def test_builds_complete_formal_vote_partition(self):
        dataset = turnout_wa.build_dataset(election(), CONTIGUOUS_TABLE)

        self.assertEqual(len(dataset.seat_totals), 2)
        self.assertEqual(len(dataset.vote_types), 10)
        self.assertEqual(dataset.seat_totals[0].formal_votes, 82)

    def test_rejects_vote_type_arithmetic_mismatch(self):
        invalid = CONTIGUOUS_TABLE.replace(
            'Alpha 100 50 10 5 15 2 82',
            'Alpha 100 51 10 5 15 2 82',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'vote types total'
        ):
            turnout_wa.parse_report_table(invalid, election())

    def test_rejects_aggregate_mismatch(self):
        wrong_totals = turnout_wa.WaElection(
            '2005wa', '2005-02-26', 'https://example.test/report.pdf',
            (0,), 2, 'contiguous', (300, 149, 30, 15, 45, 6, 246, 9, 255),
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'differ from published'
        ):
            turnout_wa.build_dataset(wrong_totals, CONTIGUOUS_TABLE)

    def test_rejects_missing_split_layout_district_label(self):
        table = '''
        100 10 5 15 2 82 3 85 85.00%
        200 20 10 30 4 164 6 170 85.00%
        Alpha 50
        '''

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'missing district labels: Beta'
        ):
            turnout_wa.parse_report_table(
                table,
                election('split-ordinary', district_names=('Alpha', 'Beta')),
            )


if __name__ == '__main__':
    unittest.main()

import unittest

import turnout_data
import turnout_sa


def election(expected_districts=2, expected_totals=None, source_format='pdf'):
    if expected_totals is None:
        expected_totals = (300, 130, 7, 137, 93, 5, 98, 223, 12, 235)
    return turnout_sa.SaElection(
        '2018sa',
        '2018-03-17',
        'https://example.test/report.pdf',
        expected_totals,
        source_format,
        enrolment_url='https://example.test/enrolment.csv',
        enrolment_page=0,
        ballot_page=1,
        expected_districts=expected_districts,
    )


ENROLMENT_TABLE = '''
Table 3.2 HOUSE OF ASSEMBLY ELECTOR, VOTER, INFORMAL AND DECLARATION ENVELOPE FIGURES
Alpha  100  80  80.0  4  5.0  35  43.8
M  ount Exam ple  200  155  77.5  8  5.2  63  40.6
Total  300  235  78.3  12  5.1  98  41.7
'''

BALLOT_TABLE = '''
Table 3.3 HOUSE OF ASSEMBLY ORDINARY, DECLARATION AND TOTAL BALLOT PAPERS - FORMAL AND INFORMAL
Alpha  45  2  47  33  2  35  78  4  82
Mount Example  85  5  90  60  3  63  145  8  153
Total  130  7  137  93  5  98  223  12  235
'''


RESULTS_CSV = '''"Electoral Commission SA, 2022 State Election, House of Assembly Ordinary, Declaration and Total ballot papers - formal and informal",,,,,,,,,,,,,,,
District,Ordinary Ballot Papers,,,,,Declaration Ballot Papers,,,,,Total Ballot Papers,,,,
,Formal Votes,,Informal Votes,,Total Votes,Formal Votes,,Informal Votes,,Total Votes,Formal Votes,,Informal Votes,,Total Votes
,Votes,%,Votes,%,,Votes,%,Votes,%,,Votes,%,Votes,%,
Alpha,45,95.7%,2,4.3%,47,33,94.3%,2,5.7%,35,78,95.1%,4,4.9%,82
Mount Example,85,94.4%,5,5.6%,90,60,95.2%,3,4.8%,63,145,94.8%,8,5.2%,153
Total,130,94.9%,7,5.1%,137,93,94.9%,5,5.1%,98,223,94.9%,12,5.1%,235
'''

ENROLMENT_CSV = '''"Electoral Commission SA, 2022 State Election, House of Assembly district enrolment breakdown",,,,,,,,,,,,
District,Male no.,Male %,Female no.,Female %,Unspecified gender no.,Unspecified gender %,Total enrolments,"% deviation from quota (26,951)",,,,
Alpha,50,50%,50,50%,0,0%,100,0%,,,,
Mount Example,100,50%,100,50%,0,0%,200,0%,,,,
Total,150,50%,150,50%,0,0%,300,,,,,
'''


class SaTurnoutAdapterTests(unittest.TestCase):
    def test_reconciles_differently_spaced_pdf_district_names(self):
        self.assertEqual(
            turnout_sa._reconciled_name('Ham m ond', 'Hamm ond'),
            'Hammond',
        )

    def test_parses_pdf_tables_and_category_informality(self):
        records = turnout_sa.parse_pdf_tables(
            ENROLMENT_TABLE, BALLOT_TABLE, election()
        )

        self.assertEqual([record.name for record in records], ['Alpha', 'Mount Example'])
        self.assertEqual(records[0].ordinary_informal, 2)
        self.assertEqual(records[1].declaration_total, 63)

        dataset = turnout_sa.build_dataset(election(), records)
        self.assertEqual(len(dataset.seat_totals), 2)
        self.assertEqual(len(dataset.vote_types), 4)
        declaration = next(
            record for record in dataset.vote_types
            if record.seat_name == 'Alpha'
            and record.canonical_category == 'declaration_combined'
        )
        self.assertEqual(declaration.informal_votes, 2)
        self.assertEqual(declaration.total_ballots, 35)

    def test_parses_equivalent_2022_csvs(self):
        records = turnout_sa.parse_2022_csvs(
            RESULTS_CSV.encode(), ENROLMENT_CSV.encode(), election(source_format='csv')
        )

        self.assertEqual(records[0].formal, 78)
        self.assertEqual(records[1].enrolment, 200)

    def test_rejects_category_arithmetic_error(self):
        invalid = BALLOT_TABLE.replace(
            'Alpha  45  2  47  33  2  35  78',
            'Alpha  45  2  47  32  2  34  78',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'category formal votes'
        ):
            turnout_sa.parse_pdf_tables(ENROLMENT_TABLE, invalid, election())

    def test_rejects_district_identity_mismatch(self):
        invalid = BALLOT_TABLE.replace('Mount Example', 'Different District')

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'district identity mismatch'
        ):
            turnout_sa.parse_pdf_tables(ENROLMENT_TABLE, invalid, election())

    def test_rejects_missing_published_total(self):
        invalid = BALLOT_TABLE.replace(
            'Total  130  7  137  93  5  98  223  12  235', ''
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'missing its published total row'
        ):
            turnout_sa.parse_pdf_tables(ENROLMENT_TABLE, invalid, election())

    def test_rejects_changed_statewide_control(self):
        invalid = BALLOT_TABLE.replace(
            'Total  130  7  137  93  5  98  223  12  235',
            'Total  129  7  136  93  5  98  222  12  234',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'published totals'
        ):
            turnout_sa.parse_pdf_tables(ENROLMENT_TABLE, invalid, election())

    def test_rejects_unexpected_2022_headers(self):
        invalid = RESULTS_CSV.replace('Ordinary Ballot Papers', 'Ordinary Votes', 1)

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'unexpected identity or headers'
        ):
            turnout_sa.parse_2022_csvs(
                invalid.encode(), ENROLMENT_CSV.encode(), election(source_format='csv')
            )

    def test_coverage_summary_includes_ballot_categories(self):
        records = turnout_sa.parse_pdf_tables(
            ENROLMENT_TABLE, BALLOT_TABLE, election()
        )
        summary = turnout_sa.coverage_summary(
            turnout_sa.build_dataset(election(), records)
        )

        self.assertEqual(summary['total_ballots'], 235)
        self.assertEqual(
            summary['categories']['declaration_combined']['informal_votes'], 5
        )


if __name__ == '__main__':
    unittest.main()

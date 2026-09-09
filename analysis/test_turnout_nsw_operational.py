import csv
import io
from dataclasses import replace
import unittest

import turnout_data
import turnout_nsw_operational as operational


class NswOperationalTurnoutTests(unittest.TestCase):
    def setUp(self):
        self.election = replace(
            operational.ELECTIONS['2015nsw'], expected_transactions=3
        )

    def dataset(self):
        source_id = 'nswec-final'
        return turnout_data.TurnoutDataset(
            elections=[turnout_data.ElectionDefinition(
                '2015nsw', '2015-03-28', 'nsw'
            )],
            sources=[turnout_data.SourceDefinition(
                source_id=source_id,
                election_code='2015nsw',
                authority='NSW Electoral Commission',
                locator='https://example.test/final',
                adapter='nswec-final-v1',
                status='final',
                category_regime='nswec-final-v1',
            )],
            seat_totals=[
                turnout_data.SeatTotal(
                    election_code='2015nsw',
                    seat_name=name,
                    source_id=source_id,
                    enrolment=100,
                    formal_votes=None,
                    informal_votes=None,
                    total_ballots=None,
                )
                for name in ('Alpha', 'Beta')
            ],
        )

    @staticmethod
    def rows(text):
        return csv.DictReader(io.StringIO(text))

    def valid_rows(self):
        return self.rows(
            'ENROLLED District,Mark-off Date,Ignored\n'
            'Alpha,20150316,x\n'
            'Alpha,20150327,x\n'
            'Beta,20150327,x\n'
        )

    def test_aggregates_exact_final_pre_election_counts(self):
        counts = operational.read_transactions(
            self.valid_rows(), self.election, self.dataset()
        )
        dataset = operational.merge_dataset(
            self.dataset(), self.election, counts
        )

        records = dataset.operational_observations
        self.assertEqual(len(records), 3)
        self.assertEqual(
            {(record.geography_basis, record.seat_name): record.count
             for record in records},
            {
                ('elector_division', 'Alpha'): 2,
                ('elector_division', 'Beta'): 1,
                ('state', ''): 3,
            },
        )
        self.assertTrue(all(
            record.count_precision == 'exact'
            and record.observation_status == 'contemporaneous'
            for record in records
        ))

    def test_rejects_unknown_district(self):
        rows = self.rows(
            'ENROLLED District,Mark-off Date\n'
            'Alpha,20150327\n'
            'Gamma,20150327\n'
            'Beta,20150327\n'
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'unknown district'
        ):
            operational.read_transactions(rows, self.election, self.dataset())

    def test_rejects_missing_district(self):
        rows = self.rows(
            'ENROLLED District,Mark-off Date\n'
            'Alpha,20150327\n'
            'Alpha,20150327\n'
            'Alpha,20150327\n'
        )
        with self.assertRaisesRegex(turnout_data.TurnoutDataError, 'omit'):
            operational.read_transactions(rows, self.election, self.dataset())

    def test_rejects_out_of_period_markoff(self):
        rows = self.rows(
            'ENROLLED District,Mark-off Date\n'
            'Alpha,20150315\n'
            'Alpha,20150327\n'
            'Beta,20150327\n'
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'out-of-period'
        ):
            operational.read_transactions(rows, self.election, self.dataset())

    def test_repeated_merge_replaces_adapter_records(self):
        counts = operational.read_transactions(
            self.valid_rows(), self.election, self.dataset()
        )
        dataset = operational.merge_dataset(
            self.dataset(), self.election, counts
        )
        dataset = operational.merge_dataset(dataset, self.election, counts)

        self.assertEqual(len(dataset.sources), 2)
        self.assertEqual(len(dataset.operational_observations), 3)


if __name__ == '__main__':
    unittest.main()

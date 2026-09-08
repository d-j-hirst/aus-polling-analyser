import unittest

import turnout_aec_operational
import turnout_data


def csv_bytes(headers, rows):
    lines = [','.join(headers)]
    lines.extend(','.join(str(value) for value in row) for row in rows)
    return ('\n'.join(lines) + '\n').encode('utf-8')


class AecOperationalTurnoutAdapterTests(unittest.TestCase):
    election = turnout_aec_operational.AecOperationalElection(
        '2010fed',
        '2010-08-21',
        'https://example.test/prepoll.csv',
        'wide',
        'https://example.test/postal.csv',
        'wide-legacy',
    )

    def dataset(self):
        return turnout_data.TurnoutDataset(
            elections=[
                turnout_data.ElectionDefinition(
                    '2010fed', '2010-08-21', 'fed'
                )
            ],
            sources=[turnout_data.SourceDefinition(
                source_id='aec-final',
                election_code='2010fed',
                authority='Australian Electoral Commission',
                locator='https://example.test/final',
                adapter='aec-final-vote-types-v1',
                status='final',
                category_regime='aec-ordinary-prepoll-v1',
            )],
            seat_totals=[
                turnout_data.SeatTotal(
                    election_code='2010fed',
                    seat_name=name,
                    source_id='aec-final',
                    enrolment=100,
                    formal_votes=None,
                    informal_votes=None,
                    total_ballots=None,
                    subdivision='sa',
                )
                for name in ('Alpha', 'Beta')
            ],
        )

    def files(self):
        prepoll = csv_bytes(
            ['State', 'Division', '02 Aug 10', '03 Aug 10'],
            [
                ['SA', 'ALPHA', 10, 15],
                ['SA', 'BETA', 2, 3],
            ],
        )
        postal = csv_bytes(
            [
                'State', 'Enrolment', 'Sum of AEC and Parties', 'AEC',
                'TOTAL to date (Inc GPV)', '28 Jul 10', '29 Jul 10',
            ],
            [
                ['SA', 'ALPHA', 7, 3, 10, 4, 6],
                ['SA', 'BETA', 3, 2, 5, 1, 4],
            ],
        )
        return {'prepoll': prepoll, 'postal': postal}

    def test_builds_cumulative_series_with_distinct_geographies(self):
        dataset = turnout_aec_operational.merge_dataset(
            self.dataset(), self.election, self.files()
        )

        alpha = [
            record for record in dataset.operational_observations
            if record.seat_name == 'Alpha'
        ]
        prepoll = [
            record for record in alpha
            if record.measure == turnout_aec_operational.PREPOLL_MEASURE
        ]
        postal = [
            record for record in alpha
            if record.measure == turnout_aec_operational.POSTAL_APPLICATION_MEASURE
        ]
        self.assertEqual([record.count for record in prepoll], [10, 25])
        self.assertEqual([record.count for record in postal], [4, 10])
        self.assertEqual(prepoll[0].geography_basis, 'administering_division')
        self.assertEqual(postal[0].geography_basis, 'elector_division')
        self.assertEqual(prepoll[0].observation_status, 'final_reconciled')

    def test_rejects_postal_daily_total_disagreement(self):
        files = self.files()
        files['postal'] = files['postal'].replace(
            b'SA,ALPHA,7,3,10,4,6', b'SA,ALPHA,7,3,11,4,6'
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'dated and out-of-range applications total 10, expected 11',
        ):
            turnout_aec_operational.merge_dataset(
                self.dataset(), self.election, files
            )

    def test_rejects_unknown_division(self):
        files = self.files()
        files['prepoll'] = files['prepoll'].replace(b'ALPHA', b'GAMMA')

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            "unknown SA division 'GAMMA'",
        ):
            turnout_aec_operational.merge_dataset(
                self.dataset(), self.election, files
            )

    def test_snapshot_records_applications_and_returns(self):
        election = turnout_aec_operational.AecOperationalElection(
            '2010fed',
            '2010-08-21',
            'https://example.test/prepoll.csv',
            'wide',
            'https://example.test/postal-{date}.csv',
            'snapshot-series',
        )
        files = {'prepoll': self.files()['prepoll']}
        files['postal:2010-08-20'] = csv_bytes(
            [
                'State', 'Division', 'Valid Apps Received',
                'Postal Votes Returned',
            ],
            [
                ['SA', 'Alpha', 20, 8],
                ['SA', 'Beta', 10, 4],
            ],
        )

        dataset = turnout_aec_operational.merge_dataset(
            self.dataset(), election, files
        )

        returns = [
            record for record in dataset.operational_observations
            if record.measure == turnout_aec_operational.POSTAL_RETURN_MEASURE
        ]
        self.assertEqual(len(returns), 2)
        self.assertTrue(all(
            record.observation_status == 'contemporaneous'
            for record in returns
        ))

    def test_out_of_range_applications_are_validated_but_not_dated(self):
        election = turnout_aec_operational.AecOperationalElection(
            '2010fed',
            '2010-08-21',
            'https://example.test/prepoll.csv',
            'wide',
            'https://example.test/postal.csv',
            'wide-modern',
        )
        files = {'prepoll': self.files()['prepoll']}
        files['postal'] = csv_bytes(
            [
                'State_Cd', 'PVA_Web_1_Party_Div', 'AEC - OPVA',
                'PVA_Web_2_Date_V2_Div', '<>', '20100801',
                'Date out of range',
            ],
            [
                ['SA', 'ALPHA', 11, 'ALPHA', 1, 10, ''],
                ['SA', 'BETA', 5, 'BETA', '', 4, 1],
            ],
        )

        dataset = turnout_aec_operational.merge_dataset(
            self.dataset(), election, files
        )

        postal = [
            record for record in dataset.operational_observations
            if record.measure == turnout_aec_operational.POSTAL_APPLICATION_MEASURE
        ]
        self.assertEqual({record.seat_name: record.count for record in postal}, {
            'Alpha': 10,
            'Beta': 4,
        })

    def test_repeated_merge_replaces_adapter_records(self):
        dataset = turnout_aec_operational.merge_dataset(
            self.dataset(), self.election, self.files()
        )
        first_count = len(dataset.operational_observations)

        dataset = turnout_aec_operational.merge_dataset(
            dataset, self.election, self.files()
        )

        self.assertEqual(len(dataset.operational_observations), first_count)
        self.assertEqual(len(dataset.sources), 3)


if __name__ == '__main__':
    unittest.main()

import json
import unittest
from dataclasses import replace

import turnout_data
import turnout_published_operational as published


class PublishedOperationalTurnoutTests(unittest.TestCase):
    def make_dataset(self, election_code='2023nsw', election_date='2023-03-25'):
        source_id = 'commission-final'
        return turnout_data.TurnoutDataset(
            elections=[turnout_data.ElectionDefinition(
                election_code, election_date, election_code[4:]
            )],
            sources=[turnout_data.SourceDefinition(
                source_id=source_id,
                election_code=election_code,
                authority='Electoral commission',
                locator='https://example.test/final',
                adapter='commission-final-v1',
                status='final',
                category_regime='commission-final-v1',
            )],
            seat_totals=[
                turnout_data.SeatTotal(
                    election_code=election_code,
                    seat_name='Alpha',
                    source_id=source_id,
                    enrolment=1001,
                    formal_votes=900,
                    informal_votes=20,
                    total_ballots=920,
                ),
                turnout_data.SeatTotal(
                    election_code=election_code,
                    seat_name='Beta',
                    source_id=source_id,
                    enrolment=2000,
                    formal_votes=1700,
                    informal_votes=50,
                    total_ballots=1750,
                ),
            ],
        )

    def test_rate_table_becomes_explicitly_approximate_district_counts(self):
        election = published.PublishedElection(
            election_code='2023nsw',
            election_date='2023-03-25',
            article_url='https://example.test/election-eve',
            state_counts=(published.PublishedCount(
                published.PREPOLL_MEASURE,
                '2023-03-24',
                1000,
                'Final pre-poll total',
            ),),
            district_url='https://example.test/district.csv',
            district_layout='nsw-rates',
            district_observed_at='2023-03-24',
        )
        data = (
            b'District,Total Early,Pre-Poll Voted,Applied for Postal\n'
            b'Alpha,40.0,25.0,15.0\n'
            b'Beta,50.0,30.0,20.0\n'
            b'State Total,46.7,28.4,18.3\n'
        )

        _source, observations = published.build_observations(
            election, {'district': data}, self.make_dataset()
        )
        district = [record for record in observations if record.seat_name]

        self.assertEqual(len(district), 4)
        self.assertEqual(district[0].count, 250)
        self.assertEqual(district[0].count_precision, 'approximate')
        self.assertEqual(
            district[0].derivation, 'rounded_rate_times_enrolment'
        )
        self.assertEqual(district[-1].count, 400)

    def test_rate_table_requires_every_final_dataset_district(self):
        election = published.PublishedElection(
            election_code='2023nsw',
            election_date='2023-03-25',
            article_url='https://example.test/election-eve',
            state_counts=(),
            district_url='https://example.test/district.csv',
            district_layout='nsw-rates',
            district_observed_at='2023-03-24',
        )
        data = (
            b'District,Total Early,Pre-Poll Voted,Applied for Postal\n'
            b'Alpha,40.0,25.0,15.0\n'
        )

        with self.assertRaisesRegex(turnout_data.TurnoutDataError, 'omits: Beta'):
            published.build_observations(
                election, {'district': data}, self.make_dataset()
            )

    def test_old_datawrapper_chart_supplies_exact_district_counts(self):
        election = published.PublishedElection(
            election_code='2020qld',
            election_date='2020-10-31',
            article_url='https://example.test/election-eve',
            state_counts=(),
            district_url='https://example.test/chart',
            district_layout='qld2020-postal-chart',
            district_observed_at='2020-10-31T10:30:00+10:00',
        )
        chart_csv = (
            'District,Sent,Pct1,Returned,Pct2\r\n'
            'Alpha,300,30.0,200,20.0\r\n'
            'Beta,600,30.0,400,20.0\r\n'
            'Total,900,30.0,600,20.0\r\n'
        )
        html = (
            '<script>{"chartData":' + json.dumps(chart_csv)
            + ',"isPreview":false}</script>'
        ).encode('utf-8')

        _source, observations = published.build_observations(
            election,
            {'district': html},
            self.make_dataset('2020qld', '2020-10-31'),
        )

        self.assertEqual(len(observations), 4)
        self.assertTrue(all(
            record.count_precision == 'exact' for record in observations
        ))
        self.assertEqual(
            sum(
                record.count for record in observations
                if record.measure == published.POSTAL_RETURN_MEASURE
            ),
            600,
        )

    @staticmethod
    def vic2014_article(rows):
        def cell(value):
            return {
                'type': 'tagname',
                'key': 'td',
                'children': [{'type': 'text', 'content': value}],
            }

        table_rows = [[
            'Pct', 'Electorate', 'Pct', 'Electorate'
        ]] + rows
        document = {
            'props': {
                'pageProps': {
                    'article': {
                        'children': [{
                            'type': 'tagname',
                            'key': 'table',
                            'children': [
                                {
                                    'type': 'tagname',
                                    'key': 'thead',
                                    'children': [{
                                        'type': 'text',
                                        'content': (
                                            'Alphabetic List '
                                            'Descending Turnout %'
                                        ),
                                    }],
                                },
                                {
                                    'type': 'tagname',
                                    'key': 'tbody',
                                    'children': [{
                                        'type': 'tagname',
                                        'key': 'tr',
                                        'children': [cell(value) for value in row],
                                    } for row in table_rows],
                                },
                            ],
                        }],
                    },
                },
            },
        }
        return (
            '<script id="__NEXT_DATA__" type="application/json">{}</script>'
            .format(json.dumps(document))
            .encode('utf-8')
        )

    def test_vic2014_article_uses_alphabetic_half_of_duplicated_table(self):
        election = published.PublishedElection(
            election_code='2014vic',
            election_date='2014-11-29',
            article_url='https://example.test/election-eve',
            state_counts=(),
            district_url='https://example.test/election-eve',
            district_layout='vic2014-combined-rates-article',
            district_observed_at='2014-11-28T18:00:00+11:00',
        )
        article = self.vic2014_article([
            ['25.0', 'Alpha', '40.0', 'Beta'],
            ['40.0', 'Beta', '25.0', 'Alpha'],
            ['33.0', 'State Total', '33.0', 'State Total'],
        ])

        dataset = self.make_dataset('2014vic', '2014-11-29')
        dataset.seat_totals = [
            replace(seat, seat_name=seat.seat_name + ' District')
            for seat in dataset.seat_totals
        ]
        _source, observations = published.build_observations(
            election,
            {'district': article},
            dataset,
        )

        self.assertEqual(len(observations), 2)
        self.assertEqual(
            [(record.seat_name, record.count) for record in observations],
            [('Alpha District', 250), ('Beta District', 800)],
        )
        self.assertTrue(all(
            record.measure == published.EARLY_VOTES_RECORDED_MEASURE
            and record.count_precision == 'approximate'
            and record.derivation == 'rounded_rate_times_enrolment'
            for record in observations
        ))

    def test_vic2014_article_requires_every_final_dataset_district(self):
        election = published.ELECTIONS['2014vic']
        article = self.vic2014_article([
            ['25.0', 'Alpha', '25.0', 'Alpha'],
        ])
        dataset = self.make_dataset('2014vic', '2014-11-29')
        dataset.seat_totals = [
            replace(seat, seat_name=seat.seat_name + ' District')
            for seat in dataset.seat_totals
        ]

        with self.assertRaisesRegex(turnout_data.TurnoutDataError, 'omits'):
            published.build_observations(
                election,
                {'district': article},
                dataset,
            )

    def test_merge_replaces_only_this_adapters_previous_records(self):
        dataset = self.make_dataset()
        election = published.PublishedElection(
            election_code='2023nsw',
            election_date='2023-03-25',
            article_url='https://example.test/election-eve',
            state_counts=(published.PublishedCount(
                published.PREPOLL_MEASURE,
                '2023-03-24',
                1000,
                'Final pre-poll total',
            ),),
        )

        first = published.merge_dataset(dataset, election, {})
        second = published.merge_dataset(first, election, {})

        self.assertEqual(len(second.sources), 2)
        self.assertEqual(len(second.operational_observations), 1)
        self.assertEqual(second.operational_observations[0].count, 1000)

    def test_retrospective_count_retains_reconciliation_status(self):
        dataset = self.make_dataset()
        election = published.PublishedElection(
            election_code='2023nsw',
            election_date='2023-03-25',
            article_url='https://example.test/retrospective',
            state_counts=(published.PublishedCount(
                published.PREPOLL_MEASURE,
                '2023-03-24',
                1000,
                'Retrospective final pre-poll total',
                observation_status='final_reconciled',
            ),),
        )

        _source, observations = published.build_observations(
            election, {}, dataset
        )

        self.assertEqual(
            observations[0].observation_status, 'final_reconciled'
        )

    def test_published_lower_bound_retains_its_relation(self):
        dataset = self.make_dataset()
        election = published.PublishedElection(
            election_code='2023nsw',
            election_date='2023-03-25',
            article_url='https://example.test/election-eve',
            state_counts=(published.PublishedCount(
                published.POSTAL_READY_MEASURE,
                '2023-03-24',
                1000,
                'At least 1,000 returned postals',
                count_precision='approximate',
                count_relation='lower_bound',
            ),),
        )

        _source, observations = published.build_observations(
            election, {}, dataset
        )

        self.assertEqual(observations[0].count_relation, 'lower_bound')

    def test_additional_publication_source_is_attributed_separately(self):
        election = published.ELECTIONS['2019nsw']

        sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2019nsw', '2019-03-23'),
        )

        self.assertEqual(
            [source.source_id for source in sources],
            ['abc-2019nsw-election-eve'],
        )
        self.assertTrue(all(
            record.source_id == 'abc-2019nsw-election-eve'
            for record in observations
        ))
        combined = next(
            record for record in observations
            if record.measure == published.PRE_ELECTION_VOTES_CAST_MEASURE
        )
        self.assertEqual(combined.count_relation, 'lower_bound')
        self.assertEqual(combined.count_precision, 'approximate')

    def test_published_forecast_retains_its_basis(self):
        election = published.ELECTIONS['2017wa']

        sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2017wa', '2017-03-11'),
        )

        prepoll = next(
            record for record in observations
            if record.measure == published.PREPOLL_MEASURE
        )
        self.assertEqual(prepoll.count_basis, 'forecast')
        self.assertEqual(prepoll.count_relation, 'lower_bound')

    def test_wa2013_records_only_votes_ready_for_election_night(self):
        election = published.ELECTIONS['2013wa']

        _sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2013wa', '2013-03-09'),
        )

        self.assertEqual(
            {record.measure: record.count for record in observations},
            {
                published.PREPOLL_READY_MEASURE: 78000,
                published.POSTAL_READY_MEASURE: 45000,
            },
        )

    def test_wa2008_distinguishes_postal_issuance_from_ready_votes(self):
        election = published.ELECTIONS['2008wa']

        sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2008wa', '2008-09-06'),
        )

        self.assertEqual(
            {record.measure: record.count for record in observations},
            {
                published.PREPOLL_MEASURE: 60000,
                published.POSTAL_APPLICATION_MEASURE: 65000,
                published.POSTAL_ISSUED_MEASURE: 81219,
                published.POSTAL_READY_MEASURE: 35467,
            },
        )
        self.assertTrue(all(
            record.observation_status == 'final_reconciled'
            for record in observations
        ))
        self.assertEqual(
            [source.authority for source in sources],
            [
                'Western Australian Electoral Commission',
                'Western Australian Electoral Commission',
            ],
        )
        lower_bounds = [
            record for record in observations
            if record.source_id == 'waec-2008-annual-report'
        ]
        self.assertEqual(len(lower_bounds), 2)
        self.assertTrue(all(
            record.count_precision == 'approximate'
            and record.count_relation == 'lower_bound'
            for record in lower_bounds
        ))

    def test_early_federal_postal_controls_are_not_returned_votes(self):
        expected = {
            '2004fed': {
                published.POSTAL_ISSUED_MEASURE: 760000,
            },
            '2007fed': {
                published.POSTAL_APPLICATION_MEASURE: 833178,
                published.POSTAL_ISSUED_MEASURE: 812826,
            },
        }

        for election_code, expected_counts in expected.items():
            election = published.ELECTIONS[election_code]
            _sources, observations = published.build_observations(
                election,
                {},
                self.make_dataset(election_code, election.election_date),
            )

            self.assertEqual(
                {record.measure: record.count for record in observations},
                expected_counts,
            )
            self.assertTrue(all(
                record.geography_basis == 'national'
                and record.observation_status == 'final_reconciled'
                for record in observations
            ))
            self.assertNotIn(
                published.POSTAL_RETURN_MEASURE,
                {record.measure for record in observations},
            )

    def test_older_commission_reconciliations_preserve_measure_semantics(self):
        expected = {
            '2005wa': {
                published.PREPOLL_MEASURE: 35220,
                published.POSTAL_APPLICATION_MEASURE: 50419,
                published.POSTAL_READY_MEASURE: 34821,
            },
            '2006sa': {
                published.PREPOLL_MEASURE: 23419,
                published.POSTAL_APPLICATION_MEASURE: 66066,
                published.POSTAL_ISSUED_MEASURE: 61364,
                published.POSTAL_RETURN_MEASURE: 54543,
                published.POSTAL_ACCEPTED_MEASURE: 51584,
            },
            '2006qld': {
                published.POSTAL_APPLICATION_MEASURE: 141000,
            },
            '2006vic': {
                published.PREPOLL_MEASURE: 255161,
                published.POSTAL_APPLICATION_MEASURE: 226170,
            },
            '2015qld': {
                published.POSTAL_ISSUED_MEASURE: 306064,
            },
        }

        for election_code, expected_counts in expected.items():
            election = published.ELECTIONS[election_code]
            _sources, observations = published.build_observations(
                election,
                {},
                self.make_dataset(election_code, election.election_date),
            )

            self.assertEqual(
                {record.measure: record.count for record in observations},
                expected_counts,
            )
            self.assertTrue(all(
                record.observation_status == 'final_reconciled'
                for record in observations
            ))

    def test_vic2010_keeps_election_eve_estimate_and_combined_final_control(self):
        election = published.ELECTIONS['2010vic']

        sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2010vic', '2010-11-27'),
        )

        self.assertEqual(len(observations), 2)
        self.assertEqual(
            [source.source_id for source in sources],
            [
                'abc-2010vic-election-eve',
                'abc-2014-vic-election-day-retrospective',
            ],
        )
        prepoll = next(
            record for record in observations
            if record.measure == published.PREPOLL_MEASURE
        )
        self.assertEqual(prepoll.count, 500000)
        self.assertEqual(prepoll.count_precision, 'approximate')
        self.assertEqual(prepoll.observation_status, 'contemporaneous')
        combined = next(
            record for record in observations
            if record.measure == published.PRE_ELECTION_VOTES_CAST_MEASURE
        )
        self.assertEqual(
            combined.observation_status,
            'final_reconciled',
        )
        self.assertEqual(combined.count, 768483)

    def test_sa2014_preserves_election_eve_forecasts_and_final_control(self):
        election = published.ELECTIONS['2014sa']

        sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2014sa', '2014-03-15'),
        )

        self.assertEqual(
            [source.source_id for source in sources],
            [
                'abc-2014sa-election-eve',
                'antony-green-2014sa-retrospective',
            ],
        )
        forecasts = [
            record for record in observations
            if record.count_basis == 'forecast'
        ]
        self.assertEqual(
            {(record.measure, record.count) for record in forecasts},
            {
                (published.PREPOLL_MEASURE, 70000),
                (published.PRE_ELECTION_VOTES_CAST_MEASURE, 160000),
            },
        )
        self.assertEqual(
            next(
                record for record in observations
                if record.measure == published.PREPOLL_MEASURE
                and record.observation_status == 'final_reconciled'
            ).count,
            80087,
        )
        reported_prepoll = next(
            record for record in observations
            if record.measure == published.PREPOLL_MEASURE
            and record.observation_status == 'contemporaneous'
            and record.count_basis == 'reported'
        )
        self.assertEqual(reported_prepoll.count, 50000)
        self.assertEqual(reported_prepoll.count_relation, 'lower_bound')
        self.assertEqual(
            next(
                record for record in observations
                if record.measure == published.POSTAL_APPLICATION_MEASURE
            ).count,
            86000,
        )

    def test_sa2018_keeps_election_eve_estimates_and_final_controls(self):
        election = published.ELECTIONS['2018sa']

        sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2018sa', '2018-03-17'),
        )

        self.assertEqual(
            [source.source_id for source in sources],
            [
                'abc-2018sa-election-morning',
                'antony-green-2018sa-retrospective',
            ],
        )
        contemporaneous = [
            record for record in observations
            if record.observation_status == 'contemporaneous'
        ]
        self.assertEqual(
            {(record.measure, record.count) for record in contemporaneous},
            {
                (published.PREPOLL_MEASURE, 120000),
                (published.POSTAL_ISSUED_MEASURE, 95000),
                (published.PRE_ELECTION_VOTES_CAST_MEASURE, 215000),
            },
        )
        self.assertTrue(all(
            record.count_precision == 'approximate'
            for record in contemporaneous
        ))
        combined = next(
            record for record in contemporaneous
            if record.measure == published.PRE_ELECTION_VOTES_CAST_MEASURE
        )
        self.assertEqual(combined.count_relation, 'lower_bound')
        final_controls = [
            record for record in observations
            if record.observation_status == 'final_reconciled'
        ]
        self.assertEqual(
            {(record.measure, record.count) for record in final_controls},
            {
                (published.PREPOLL_MEASURE, 120468),
                (published.POSTAL_APPLICATION_MEASURE, 82213),
                (published.POSTAL_ISSUED_MEASURE, 94831),
            },
        )

    def test_qld2009_report_remains_an_approximate_application_count(self):
        election = published.ELECTIONS['2009qld']

        _sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2009qld', '2009-03-21'),
        )

        self.assertEqual(len(observations), 1)
        self.assertEqual(
            observations[0].measure,
            published.POSTAL_APPLICATION_MEASURE,
        )
        self.assertEqual(observations[0].count, 213000)
        self.assertEqual(observations[0].count_precision, 'approximate')

    def test_sa2010_report_remains_a_postal_application_lower_bound(self):
        election = published.ELECTIONS['2010sa']

        _sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2010sa', '2010-03-20'),
        )

        self.assertEqual(len(observations), 1)
        self.assertEqual(
            observations[0].measure,
            published.POSTAL_APPLICATION_MEASURE,
        )
        self.assertEqual(observations[0].count, 80000)
        self.assertEqual(observations[0].count_precision, 'approximate')
        self.assertEqual(observations[0].count_relation, 'lower_bound')

    def test_qld2017_postal_report_is_not_treated_as_returned_ballots(self):
        election = published.ELECTIONS['2017qld']

        _sources, observations = published.build_observations(
            election,
            {},
            self.make_dataset('2017qld', '2017-11-25'),
        )

        postal = next(
            record for record in observations
            if record.measure == published.POSTAL_ISSUED_MEASURE
        )
        self.assertEqual(postal.count, 369000)
        self.assertEqual(postal.count_precision, 'approximate')
        self.assertNotIn(
            published.POSTAL_RETURN_MEASURE,
            {record.measure for record in observations},
        )


if __name__ == '__main__':
    unittest.main()

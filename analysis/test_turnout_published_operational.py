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


if __name__ == '__main__':
    unittest.main()

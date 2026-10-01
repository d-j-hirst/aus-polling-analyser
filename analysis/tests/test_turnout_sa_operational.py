from dataclasses import replace
import unittest

import turnout_data
import turnout_sa_operational as operational


class SaOperationalTurnoutTests(unittest.TestCase):
    def setUp(self):
        self.election = replace(
            operational.ELECTIONS['2026sa'],
            expected_districts=2,
            expected_early_total=60,
            expected_postal_total=25,
            expected_early_district_residual=0,
            expected_postal_district_residual=0,
        )

    @staticmethod
    def page(
        alpha_early='10',
        early_total='60',
        alpha_postal='4',
        postal_total='25',
        postal_second_district='Beta',
    ):
        return (
            '<h4>Daily Tally | Early Voting Mark Off Numbers</h4>'
            '<table>'
            '<tr><td></td><td>March 19</td><td>March 20</td><td>TOTAL</td></tr>'
            '<tr><td>DISTRICT</td><td></td><td></td><td></td></tr>'
            '<tr><td>Alpha</td><td>{}</td><td>20</td><td>30</td></tr>'
            '<tr><td>Beta</td><td>12</td><td>18</td><td>30</td></tr>'
            '<tr><td></td><td>22</td><td>38</td><td>{}</td></tr>'
            '</table>'
            '<h4>Daily Tally | Postal Vote Applications by District</h4>'
            '<table>'
            '<tr><td></td><td>15/3/2026</td><td>16/3/2026</td><td>TOTAL</td></tr>'
            '<tr><td>Alpha</td><td>{}</td><td>6</td><td>10</td></tr>'
            '<tr><td>{}</td><td>8</td><td>7</td><td>15</td></tr>'
            '<tr><td></td><td>12</td><td>13</td><td>{}</td></tr>'
            '</table>'
        ).format(
            alpha_early,
            early_total,
            alpha_postal,
            postal_second_district,
            postal_total,
        ).encode('utf-8')

    def test_extracts_reconciled_state_and_district_totals(self):
        source, observations = operational.build_observations(
            self.election, self.page()
        )

        self.assertEqual(source.status, 'operational')
        self.assertEqual(len(observations), 6)
        self.assertEqual(
            {(row.measure, row.seat_name): row.count for row in observations},
            {
                (operational.PREPOLL_MEASURE, 'Alpha'): 30,
                (operational.PREPOLL_MEASURE, 'Beta'): 30,
                (operational.PREPOLL_MEASURE, ''): 60,
                (operational.POSTAL_APPLICATION_MEASURE, 'Alpha'): 10,
                (operational.POSTAL_APPLICATION_MEASURE, 'Beta'): 15,
                (operational.POSTAL_APPLICATION_MEASURE, ''): 25,
            },
        )
        self.assertTrue(all(
            row.count_precision == 'exact'
            and row.observation_status == 'contemporaneous'
            for row in observations
        ))

    def test_accepts_corrected_total_that_differs_from_daily_cells(self):
        early, total, _postal, _postal_total = operational.parse_tally_page(
            self.election, self.page(alpha_early='9')
        )

        self.assertEqual(early['Alpha'], 30)
        self.assertEqual(total, 60)

    def test_rejects_unexpected_published_state_total(self):
        page = self.page().replace(
            b'<td></td><td>22</td><td>38</td><td>60</td>',
            b'<td></td><td>21</td><td>38</td><td>59</td>',
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'early-vote total'
        ):
            operational.parse_tally_page(self.election, page)

    def test_rejects_unexpected_district_residual(self):
        page = self.page().replace(
            b'<tr><td>Alpha</td><td>10</td><td>20</td><td>30</td></tr>',
            b'<tr><td>Alpha</td><td>10</td><td>20</td><td>29</td></tr>',
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'district residual'
        ):
            operational.parse_tally_page(self.election, page)

    def test_accepts_period_as_a_thousands_separator(self):
        self.assertEqual(operational._count('3.833', 'test count'), 3833)

    def test_rejects_different_district_sets(self):
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'cover different districts'
        ):
            operational.parse_tally_page(
                self.election,
                self.page(postal_second_district='Gamma'),
            )

    def test_repeated_merge_replaces_adapter_records(self):
        dataset = operational._new_dataset(self.election)

        dataset = operational.merge_dataset(
            dataset, self.election, self.page()
        )
        dataset = operational.merge_dataset(
            dataset, self.election, self.page()
        )

        self.assertEqual(len(dataset.sources), 1)
        self.assertEqual(len(dataset.operational_observations), 6)


if __name__ == '__main__':
    unittest.main()

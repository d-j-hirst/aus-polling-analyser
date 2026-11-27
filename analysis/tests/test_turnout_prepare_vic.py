"""Check Victorian input decisions using fictional evidence, without feeds."""

import unittest
from unittest.mock import patch

from scripts.turnout import turnout_prepare_vic as preparation
from scripts.turnout import turnout_vic_early as early
from scripts.turnout import turnout_vic_voting_statistics as statistics


class VotingStatisticsTests(unittest.TestCase):
    settings = dict(period_start='2026-11-04', early_close='2026-11-27T18:00:00')

    def setUp(self):
        def row(label, counts):
            return dict(A=label, **dict(zip(statistics.COLUMNS, map(str, counts))))
        self.rows = {
            2: {'A': 'State Election 2026'}, 3: {'A': 'Date range: 04/11/2026 – 27/11/2026'},
            4: {'A': 'As at 28/11/2026 08:30 am'},
            10: dict(B='Postal Vote Applications', F='Postal Votes', H='Early Votes'),
            11: dict(zip('ABCDEFGHI', statistics.HEADINGS)),
            12: row('27/11/2026', [0, 160, 3, 10, 6, 35, 70, 1300]),
            13: row('East', [0, 100, 2, 9, 4, 24, 50, 800]),
            14: row('West', [0, 60, 1, 1, 2, 11, 20, 500])}

    def observations(self):
        with patch.object(statistics, 'read_xlsx_rows', return_value=self.rows):
            return statistics.completed_observations(b'', ['East', 'West'], self.settings,
                '2026-11-28T18:00:00')

    def test_measured_early_and_net_postals_use_friday_even_in_a_saturday_edition(self):
        self.rows[3]['A'] = 'Date range: 04/11/2026 – 28/11/2026'
        self.rows[15] = dict(self.rows[12], A='28/11/2026', I='9999')
        observations, summary = self.observations()
        self.assertEqual(summary['through'], '2026-11-27')
        self.assertEqual(summary['postal_returned'], 35)
        selected = preparation.operational_controls(observations, ['East', 'West'], '2026-11-28T18:00:00')
        self.assertEqual([r['count'] for r in selected['early']], [800, 500])
        self.assertEqual([r['count'] for r in selected['postal']], [91, 59])

    def test_incomplete_day_and_post_election_publication_are_rejected(self):
        for issued in ('27/11/2026 02:30 pm', '28/11/2026 06:02 pm'):
            with self.subTest(issued=issued), self.assertRaises(ValueError):
                self.rows[4]['A'] = 'As at ' + issued
                self.observations()
        self.rows[3]['A'] = 'Date range: 04/11/2026 – 26/11/2026'
        self.rows[12]['A'] = '26/11/2026'
        with self.assertRaisesRegex(ValueError, 'Waiting for completed district figures'):
            self.observations()

    def test_all_districts_and_columns_must_reconcile(self):
        for column, value in [('A', 'East'), ('H', ''), ('I', '499'), ('E', '61')]:
            with self.subTest(column=column), patch.dict(self.rows[14], {column: value}), self.assertRaises(ValueError):
                self.observations()
        del self.rows[14]
        with self.assertRaisesRegex(ValueError, 'Missing VEC voting statistics for West'):
            self.observations()


class PrepareVicTests(unittest.TestCase):
    def test_split_early_conserves_each_candidate_and_records_measured_centres(self):
        weights = early.district_weights(.75, 3000, 120)
        self.assertEqual(weights, {'EVC early': .75, 'Mobile early': .03, 'Other early': .22})
        pooled = {'fp': {'0': 53, '1': 34, '2': 13}, 'tcp': {'0': 65, '1': 37}}
        measured = {'0': 48, '1': 22, '2': 5}
        for centre in (measured, None):
            split = early.split_previous(pooled, centre, weights)
            home = split['EVC Early Votes']
            self.assertEqual(home.get('measured_fp', False), centre is not None)
            if centre is not None:
                self.assertEqual(home['fp'], measured)
            for stage, counts in pooled.items():
                for candidate, total in counts.items():
                    self.assertEqual(sum(row[stage][candidate] for row in split.values()), total)
            self.assertTrue(all(row['synthetic'] for row in split.values()))
        self.assertEqual(early.district_weights(.75, 3000, 0)['Mobile early'], 0)
        with self.assertRaises(ValueError):
            early.district_weights(.95, 1000, 1000)

    def test_booth_matches_do_not_choose_between_conflicting_old_records(self):
        districts = {
            'New': dict(booths=[dict(id=1, name='Unique'), dict(id=2, name='Repeated'), dict(id=3, name='Conflicting')]),
            'Other': dict(booths=[dict(id=4, name='Repeated')])}
        booth = dict(fp={'0': 100, '1': 200})
        previous = {'Old': dict(booths={'Unique': booth, 'Repeated': booth, 'Conflicting': booth}),
            'New': dict(booths={'Repeated': booth, 'Conflicting': booth, 'Early Votes': booth})}
        matches, ambiguous = preparation.match_booths(districts, previous)
        self.assertEqual(set(matches), {1, 2})
        self.assertEqual(matches[1]['seat'], 'Old')
        self.assertEqual(matches[1]['count'], 300)
        self.assertEqual(ambiguous, {3})

    def test_later_or_reconciled_counts_cannot_enter_initial_controls(self):
        observations = []
        for measure in ('prepoll_votes_cast_cumulative', 'postal_applications_cumulative'):
            base = dict(seat_name='Example District', geography_basis='elector_division', measure=measure,
                observation_status='contemporaneous', observed_at='2030-01-08', count=100)
            observations.extend([base, dict(base, observed_at='2030-01-11', count=999),
                dict(base, observation_status='final_reconciled', count=888),
                dict(base, observed_at='2030-01-09', count_basis='forecast', count=777),
                dict(base, observed_at='2030-01-09', count_relation='lower_bound', count=666)])
        controls = preparation.operational_controls(observations, ['Example'], '2030-01-10T18:00:00')
        self.assertEqual([rows[0]['count'] for rows in controls.values()], [100, 100])
        with self.assertRaises(ValueError):
            preparation.operational_controls(observations, ['Unknown'], '2030-01-10T18:00:00')

    def test_missing_partition_preserves_total_and_uses_complete_pool(self):
        totals = [dict(seat_name=name + ' District', enrolment=1000, total_ballots=900, formal_votes=850)
            for name in ('Published', 'Missing')]
        rows = [dict(seat_name='Published District', canonical_category=category,
            formal_votes=count, coverage='complete') for category, count in zip(
            preparation.SOURCE_GROUPS, [50, 350, 200, 20, 0, 230])]
        previous, partitions, pooled, rates = preparation.historical_baselines(dict(seat_totals=totals, vote_types=rows))
        self.assertIn('Missing', previous)
        self.assertNotIn('Missing', partitions)
        self.assertEqual(pooled['marked_as_voted'], 0)
        self.assertEqual(rates['turnout'], 90)


if __name__ == '__main__':
    unittest.main()

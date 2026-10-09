"""Check Victorian input decisions using fictional evidence, without feeds."""

import unittest

from scripts.turnout import turnout_prepare_vic as preparation


class PrepareVicTests(unittest.TestCase):
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

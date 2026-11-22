"""Check the recorded comparisons and the vote-share constraints used in Victoria."""

import tempfile
import unittest
from pathlib import Path

import numpy as np

from baseline import historical_difference, state_signal, south_australian_test
from evidence import FIRST_RETURNS
from study import (HERE, anchored, district_data, held_out_rules, read_first_returns, read_rows, replay_held_out,
                   run, shares_from_returns)


class BoothStudyTests(unittest.TestCase):
    def test_published_district_predictions_reproduce_the_ordinary_replay_scores(self):
        rows = read_rows(HERE / 'replay-comparisons.csv')
        self.assertEqual(set(rows[0]), {'seat', 'area', 'mode', 'stage', 'rule', 'weights', 'method',
                                        'actual_remaining_pp', 'predicted_remaining_pp', 'error_pp'})
        scores = read_rows(HERE / 'election-night.csv')
        for stage in ('1', '3'):
            sample = [r for r in rows if (r['mode'], r['stage'], r['rule']) == ('ordinary', stage, 'three')]
            self.assertEqual(len({r['seat'] for r in sample}), 47)
            errors = [abs(float(r['predicted_remaining_pp']) - float(r['actual_remaining_pp'])) for r in sample]
            summary, = (r for r in scores if (r['mode'], r['stage'], r['rule'], r['weights'])
                        == ('ordinary', stage, 'three', 'prior'))
            self.assertAlmostEqual(np.mean(errors), float(summary['mae_pp']), delta=.0002)

    def test_absent_state_candidates_supply_no_geography(self):
        names = ['Main School', 'Town Hall', 'New booth']
        self.assertTrue(np.isnan(state_signal({}, names)).all())
        state = state_signal({'mainschool': (0, 350), 'townhall': (30, 430)}, names)
        self.assertTrue(np.isfinite(state[:2]).all())
        self.assertTrue(np.isnan(state[2]))
        federal = np.array([-1.5, -.8, -1.1])
        sizes = np.array([350, 430, 200])
        difference = historical_difference(federal, state, sizes)
        self.assertEqual(difference[2], 0)
        self.assertAlmostEqual(np.average(difference, weights=sizes), 0.)
        # Raising the historical level everywhere by the same amount must
        # leave the proposed local pattern exactly where it started.
        np.testing.assert_allclose(historical_difference(federal, state + 2, sizes), difference)

    def test_expected_local_return_produces_no_district_movement(self):
        # Reproduce a hot booth in a strong seat and an ordinary booth without
        # a federal site nearby. The same local baseline feeds both projection
        # and comparison. A return at its own expectation supplies no surprise.
        offsets = np.array([-2.4, -1.2, -.9, np.nan])
        sizes = np.array([800., 500., 900., 200.])
        for target in (.09, .22, .36):
            baseline, _ = anchored(offsets, sizes, target)
            for reporting in ([0., 0., 620., 0.], [0., 175., 60., 26.], [0., 0., 0., 100.]):
                counts = np.array(reporting)
                observed = np.average(baseline, weights=counts)
                np.testing.assert_allclose(shares_from_returns(offsets, counts, sizes, observed), baseline)

    @unittest.skipUnless((HERE / FIRST_RETURNS).is_file(), 'requires local SA first returns')
    def test_earlier_state_comparisons_reproduce(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            south_australian_test(HERE, output)
            for name in ('baseline-comparisons.csv', 'baseline-strengths.csv'):
                self.assertEqual((output / name).read_bytes(), (HERE / name).read_bytes())
            rows = read_rows(output / 'baseline-comparisons.csv')
            federal = {r['previous_state_result']: int(r['districts']) for r in rows
                       if r['test'] == 'booth' and r['method'] == 'federal'}
            self.assertEqual(federal, {'absent': 28, 'stood': 19, 'stood_at_most_6.2pct': 8})

    def test_geography_preserves_target_and_relative_odds(self):
        offsets = np.array([0., np.log(2.), np.nan])
        sizes = np.array([100., 300., 600.])
        for target in (0., .01, .2, .85, 1.):
            shares, _ = anchored(offsets, sizes, target)
            self.assertAlmostEqual(np.average(shares, weights=sizes), target)
            self.assertEqual(shares[2], target)
            if 0 < target < 1:
                odds = shares[:2] / (1 - shares[:2])
                self.assertAlmostEqual(odds[1] / odds[0], 2.)

    def test_remote_returns_represent_the_group_mean(self):
        offsets = np.array([0., np.log(2.), np.nan])
        prior = np.array([900., 100., 1000.])
        for reported in (np.array([0, 0, 170]), np.array([50, 250, 200]), np.array([65, 10, 0])):
            shares = shares_from_returns(offsets, reported, prior, .27)
            self.assertAlmostEqual(np.average(shares, weights=reported), .27)
            self.assertAlmostEqual(np.average(shares, weights=prior), shares[2])
        np.testing.assert_allclose(shares_from_returns(np.full(3, np.nan), reported, prior, .27), .27)

    @unittest.skipUnless((HERE / FIRST_RETURNS).is_file(), 'requires local SA first returns')
    def test_held_out_votes_cannot_choose_their_geography_or_strength(self):
        rows = read_rows(HERE / 'sa-booths.csv')
        returns = read_first_returns(HERE)
        # Give every formal ballot in the held-out area to One Nation, both in
        # the official final and at first report. Its evaluation should change,
        # but the rules used to predict the area must be exactly the same.
        seats = {r['seat'] for r in rows if r['federal_area'] == 'Mayo'}
        altered = [dict(r, on=r['formal']) if r['seat'] in seats else r for r in rows]
        altered_live = [dict(r, reported_on=r['reported_formal']) if r['seat'] in seats else r for r in returns]

        def fitted(data, live):
            districts = district_data(data)
            fits = replay_held_out(districts, live, held_out_rules(districts, 'federal_area'))
            return {k: rules for k, rules in fits.items() if k[0] in seats}

        self.assertTrue(seats)
        self.assertEqual(fitted(rows, returns), fitted(altered, altered_live))

    @unittest.skipUnless((HERE / FIRST_RETURNS).is_file(), 'requires local SA first returns')
    def test_all_recorded_comparisons_reproduce(self):
        operator = (HERE / 'operator/vic-booths.csv').is_file()
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            run(HERE, output, historical_only=not operator)
            for path in output.rglob('*.csv'):
                self.assertEqual(path.read_bytes(), (HERE / path.relative_to(output)).read_bytes(), path.name)
            scores = read_rows(output / 'scores.csv')
            ordinary, = (r for r in scores if (r['split'], r['mode'], r['rule'])
                         == ('federal_area', 'ordinary', 'three-10km'))
            self.assertEqual((ordinary['districts'], ordinary['booths'], ordinary['formal']), ('47', '682', '481926'))
            early, = (r for r in scores if (r['split'], r['mode'], r['rule'])
                      == ('federal_area', 'early', 'uniform'))
            self.assertEqual((early['districts'], early['booths']), ('11', '22'))
            if operator:
                groups = read_rows(output / 'vic-districts.csv')
                self.assertEqual(len(groups), 176)
                self.assertTrue(all(float(r['weighted_pct']) == 20. for r in groups))
                self.assertEqual(sum(int(r['geographic_venues']) for r in groups), 1815)


if __name__ == '__main__':
    unittest.main()

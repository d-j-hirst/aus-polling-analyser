"""Exercise the booth FP prototype with contradictory, confirming and revised counts."""
import unittest

import numpy as np
import pandas as pd

from scripts.exploratory.booth_fp_update import adjusted_shares, district_gap_sd, district_projection, fit_shift
from scripts.exploratory.booth_revision_trial import local_ballots, score_series, trial_centres
from scripts.exploratory.nonmajor_booth_response import projections


class BoothUpdateTests(unittest.TestCase):
    def setUp(self):
        self.shapes = np.array([[.35,.45,.20], [.25,.60,.15], [.4,.39,.21]])
        self.counts = np.array([[77.,54.,50.], [26.,60.,39.], [0.,0.,0.]])
        self.ordinary = np.array([True,True,False])
        self.prior = np.array([.35,.45,.20])

    def test_split_uses_ordinary_fit_only_for_outstanding_ordinary_ballots(self):
        counts = self.counts.copy()
        counts[1] = 0
        for remaining,expected in [([0,1000,0],'strong'),([0,0,1000],'cautious')]:
            estimates = projections(self.shapes,counts,self.ordinary,np.array(remaining),self.prior)
            np.testing.assert_allclose(estimates['by_type'],estimates[expected])
        remaining = np.array([0,800,200])
        fit = trial_centres(self.shapes,self.shapes,counts,self.ordinary,remaining,self.prior)
        actual = projections(self.shapes,counts,self.ordinary,remaining,self.prior)
        np.testing.assert_allclose(fit['split/0/none'],actual['by_type'])

    def test_slope_selects_only_on_the_previous_move(self):
        def seat(name,wins,counts):
            return [dict(step=i,seat=name,pair='0/1',model='replay',fp=1.,win=win,
                         won=0,local=True,local_ballots=n) for i,(win,n) in enumerate(zip(wins,counts))]
        rows = seat('A',[35.,45.,45.,44.9,55.],[0,0,1,2,3])
        # The move at the 5 pp boundary is followed by 1 pp. The 10 pp
        # move in A is followed by zero. Both next updates must count.
        rows += seat('B',[40.,45.,46.],[0,1,1])
        frame = pd.DataFrame(rows)
        summary = score_series(frame).loc['replay']
        self.assertEqual(summary.triples,2)
        self.assertAlmostEqual(summary.lag,5/(100+25))
        local = score_series(frame,local_only=True).loc['replay']
        self.assertEqual(local.triples,1)
        self.assertAlmostEqual(local.lag,.2)
        self.assertTrue(np.isnan(score_series(frame,min_move_pp=30).loc['replay','lag']))
        # On a common set of triggers the alternate forecast still uses its
        # own movements on both sides of the slope.
        alternate = frame.assign(model='alternate',win=frame.win/2)
        common = score_series(pd.concat([frame,alternate],ignore_index=True),trigger_model='replay')
        self.assertEqual(common.loc['alternate','triples'],2)
        self.assertAlmostEqual(common.loc['alternate','lag'],summary.lag)
        # Missing snapshots or a different contest break the series.
        frame.loc[(frame.seat=='B') & (frame.step==2),'step'] = 3
        frame.loc[(frame.seat=='A') & (frame.step==2),'pair'] = '0/2'
        self.assertEqual(score_series(frame).loc['replay','triples'],0)

    def test_count_changes_include_reallocations_at_the_same_turnout(self):
        def booth(a,b):
            return {'one':{'fp_votes_current':{'ALP':a,'LIB':b}}}
        self.assertNotEqual(local_ballots(booth(45,55)),local_ballots(booth(55,45)))
        self.assertEqual(local_ballots(booth(0,0)),local_ballots({}))

    def estimate(self, counts, remaining):
        return district_projection(self.shapes, counts, remaining, self.ordinary, self.prior, 3000, 500)

    def test_new_booth_can_confirm_contradict_or_match_forecast(self):
        earlier = self.counts.copy()
        earlier[1] = 0
        before = self.estimate(earlier, [0,125,10000])
        shift = fit_shift(self.shapes[:1], earlier[:1], self.prior, 3000, 500)
        expected = adjusted_shares(self.shapes[1:2], shift)[0]
        for gap, direction in [(0,0), (.08,1), (-.08,-1)]:
            votes = earlier.copy()
            votes[1] = 125*(expected + np.array([gap,-gap,0]))
            after = self.estimate(votes, [0,0,10000])
            if direction:
                self.assertGreater(direction*((after[0]-after[1])-(before[0]-before[1])), 0)
            else:
                np.testing.assert_allclose(after, before, atol=1e-6, rtol=0)
                def spread(counts, remaining):
                    return district_gap_sd(self.shapes, counts, np.array(remaining), self.ordinary,
                                           self.prior, 3000, 500)
                self.assertLess(spread(votes, [0,0,10000]), spread(earlier, [0,125,10000]))

    def test_single_capture_order_revisions_and_counted_floor(self):
        remaining = np.array([0,0,200.])
        counts = self.counts.copy()
        original = self.estimate(counts, remaining)
        for order in ([2,1,0], [1,0,2]):
            current = district_projection(self.shapes[order], counts[order], remaining[order],
                                          self.ordinary[order], self.prior, 3000, 500)
            np.testing.assert_allclose(current, original, atol=1e-10, rtol=0)
        counts[0,0] -= 10
        counts[0,1] += 10
        self.assertLess(self.estimate(counts, remaining)[0], original[0])
        counts[0] = 0
        np.testing.assert_allclose(self.estimate(counts, remaining),
            district_projection(self.shapes[1:], counts[1:], remaining[1:], self.ordinary[1:],
                                self.prior, 3000, 500))
        np.testing.assert_allclose(self.estimate(counts, np.zeros(3)), 100*counts.sum(axis=0)/counts.sum())

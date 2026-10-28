"""Check historical matching and conservative WA reporting assumptions."""
import unittest
import xml.etree.ElementTree as ET
import numpy as np

from scripts.elections.prepare_wa_baseline import apportion, split_account, match_venues, NS
from lib.turnout import maintained_parameters, prior
from scripts.turnout.turnout_published_operational import ELECTIONS, PREPOLL_MEASURE


def district(booths):
    root = ET.Element('District')
    for number, name, venue in booths:
        ET.SubElement(root, '{'+NS['w']+'}OrdinaryPollingPlace', Number=number, Name=name, VenueId=venue)
    return root


class WaBaselineTests(unittest.TestCase):
    def test_partition_preserves_each_candidate(self):
        source = dict(fp={'a': 503, 'b': 207, 'c': 1}, tcp={'a': 601, 'b': 109})
        pieces = split_account(source, [.6, .35, .05])
        for field in source:
            for candidate, count in source[field].items():
                self.assertEqual(sum(p[field][candidate] for p in pieces), count)
        self.assertEqual(apportion(5, [0, 1]), [0, 5])

    def test_small_synthetic_parts_never_have_more_tcp_than_fp(self):
        source = dict(fp={'a': 1, 'b': 1, 'c': 1, 'd': 1}, tcp={'a': 2, 'b': 2})
        pieces = split_account(source, [.45, .45, .1])
        for piece in pieces:
            self.assertLessEqual(sum(piece['tcp'].values()), sum(piece['fp'].values()))
        for field, counts in source.items():
            for candidate, count in counts.items():
                self.assertEqual(sum(p[field][candidate] for p in pieces), count)

    def test_numbers_are_not_historical_identity(self):
        old = {'North': district([('1', 'Hall', 'a'), ('2', 'School', 'b')])}
        new = {'North': district([('1', 'Other', 'c'), ('2', 'Hall', 'a'), ('3', 'School', 'b')])}
        self.assertEqual(match_venues(new, old, {'North':['North']}),
            {('North','2'):('North','Hall'), ('North','3'):('North','School')})

    def test_shared_venues_need_unique_resident_account(self):
        old = {'North': district([('1','Hall','a')]), 'South': district([('1','Hall','a')])}
        new = {'New': district([('8','Hall','a')])}
        self.assertEqual(match_venues(new, old, {'New':['North','South']}), {})
        self.assertEqual(match_venues(new, old, {'New':['North']}), {('New','8'):('North','Hall')})

    def test_two_current_claims_do_not_duplicate_history(self):
        old = {'North': district([('1','Hall','a')])}
        new = {'North': district([('8','Hall','a'), ('9','Hall Annex','a')])}
        self.assertEqual(match_venues(new, old, {'North':['North']}), {})

    def test_reporting_uncertainty_conserves_parent_and_absence(self):
        params = maintained_parameters.prior_parameters('2025wa/synthetic_reporting')
        covariance = np.array(params['compositions']['remainder']['common_covariance'])
        prior.covariance_root(covariance)
        self.assertLess(covariance[1,2], 0) # shared early/absent transfer, not extra voters
        weights = np.array([.7,0,.25,.01,.04])
        draws = prior.transformed_partition(np.asarray(1000), weights, np.array([.4,2,-.3,.7,-.8]))
        self.assertAlmostEqual(draws.sum(), 1000)
        self.assertEqual(draws[1], 0) # no local centre cannot acquire one by sampling

    def test_friday_is_already_in_cumulative_early_total(self):
        observations = [r for r in ELECTIONS['2025wa'].state_counts if r.measure == PREPOLL_MEASURE]
        self.assertEqual(observations[0].count, 557693)
        self.assertEqual(observations[0].source_id, 'waec-2025-early-election-eve')


if __name__ == '__main__':
    unittest.main()

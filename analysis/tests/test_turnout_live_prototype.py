"""Check counted-vote preservation and snapshot independence in the prototype."""

import copy
import hashlib
from pathlib import Path
import unittest
from unittest.mock import patch

import numpy as np

from lib.turnout import live, prior, maintained_parameters
from scripts.turnout import turnout_live_prototype as prototype
from tests.test_turnout_prior_prototype import small_case


def live_case():
    inputs, parameters = small_case()
    draws = prior.draw(inputs, parameters, 128)
    units = []
    for seat in range(2):
        for group, kind in enumerate(('ordinary', 'ppvc', 'declaration', 'declaration')):
            units.append(dict(seat_index=seat, seat_name=inputs['seat_names'][seat],
                              group_index=group, group=inputs['categories'][group],
                              name=f'{kind}-{group}', weight=1., counted=0,
                              kind=kind, matched=True, closed_reason=None))
    return inputs, draws, units


class LivePrototypeTests(unittest.TestCase):
    def test_experimental_fixture_cannot_replace_installed_parameters(self):
        # A private experiment may supply election inputs, but its fitted
        # coefficients cannot silently replace deliberately installed tuning.
        payload = dict(schema_version=prior.SCHEMA_VERSION, model_version=prior.MODEL_VERSION,
                       fingerprint=dict(inputs={}, code={'prior.py': hashlib.sha256(
                           Path(prior.__file__).read_text(encoding='utf-8').encode()).hexdigest()}),
                       fixtures=[dict(inputs={'identity': '2026sa/earlier_only'},
                                      parameters={'uninstalled_experimental_value': 999})])
        with patch.object(prototype, 'read_json', return_value=payload):
            fixture = prototype.load_fixture('private-experiment.json')
        self.assertEqual(fixture['parameters'], maintained_parameters.prior_parameters('2026sa/earlier_only'))
        self.assertNotIn('uninstalled_experimental_value', fixture['parameters'])

    def test_no_counts_reproduces_prior_and_accounting(self):
        inputs, draws, units = live_case()
        result = live.update(draws, inputs, units)
        np.testing.assert_allclose(result['totals'], draws['totals'])
        np.testing.assert_allclose(result['counts'], draws['counts'])
        np.testing.assert_allclose(result['counts'].sum(axis=2), result['totals'])

    def test_complete_booths_preserved_partial_additions_positive(self):
        inputs, draws, units = live_case()
        units[0]['counted'] = 450
        units[1]['counted'] = 220
        units[2]['counted'] = 90
        result = live.update(draws, inputs, units)
        np.testing.assert_array_equal(result['unit_counts'][:, :2], np.broadcast_to([450, 220], (128, 2)))
        self.assertTrue((result['remaining'][:, 2:4] > 0).all())
        np.testing.assert_allclose(result['unit_counts'][:, :4].sum(axis=1), result['totals'][:, 0])
        self.assertTrue((result['totals'] < inputs['enrolment']).all())

    def test_repeated_snapshot_and_downward_revision_start_from_prior(self):
        inputs, draws, units = live_case()
        units[0]['counted'] = 450
        first = live.update(draws, inputs, units)
        revised = copy.deepcopy(units)
        revised[0]['counted'] = 400
        live.update(draws, inputs, revised)
        repeated = live.update(draws, inputs, units)
        np.testing.assert_array_equal(first['unit_counts'], repeated['unit_counts'])
        np.testing.assert_array_equal(draws['totals'], prior.draw(*small_case(), 128)['totals'])

    def test_observed_near_roll_does_not_create_a_zero_remaining_mass(self):
        inputs, draws, units = live_case()
        units[0]['counted'] = 970
        result = live.update(draws, inputs, units)
        self.assertTrue((result['totals'][:, 0] > 970).all())
        self.assertTrue((result['totals'][:, 0] < 1000).all())
        self.assertTrue((result['remaining'][:, 1:4] > 0).all())

    def test_known_closure_preserves_positive_revision_and_flags_it(self):
        inputs, draws, units = live_case()
        units[0].update(closed_reason='Known closure', counted=12)
        result = live.update(draws, inputs, units)
        np.testing.assert_array_equal(result['unit_counts'][:, 0], np.full(128, 12))
        self.assertEqual(result['diagnostics']['closure_count_conflicts'], ['ordinary-0'])
        units[0]['weight'] = 0
        adapted = live.update(draws, inputs, units, changes={'ordinary': {'multiplier': .9}})
        np.testing.assert_array_equal(adapted['unit_counts'][:, 0], np.full(128, 12))

    def test_authoritative_finalisation_fixes_total_and_all_units(self):
        inputs, draws, units = live_case()
        units[0]['counted'] = 700
        units[2]['counted'] = 100
        result = live.update(draws, inputs, units, ['A'])
        np.testing.assert_array_equal(result['totals'][:, 0], np.full(128, 800))
        np.testing.assert_array_equal(result['remaining'][:, :4], np.zeros((128, 4)))

    def test_unmatched_and_closed_booths_do_not_train_pooled_change(self):
        inputs, draws, units = live_case()
        units[0].update(counted=100, matched=False)
        units[4].update(counted=100, closed_reason='Known closure')
        central = draws['counts'].mean(axis=0)
        changes = live.pooled_booth_changes(units, central)
        self.assertEqual(changes['ordinary']['eligible_booths'], 0)
        self.assertEqual(changes['ordinary']['multiplier'], 1)
        units[1]['counted'] = 200
        changes = live.pooled_booth_changes(units, central)
        self.assertEqual(changes['ppvc']['eligible_booths'], 1)
        units[1]['calibration_exclusion_reason'] = 'Known reporting consolidation'
        changes = live.pooled_booth_changes(units, central)
        self.assertEqual(changes['ppvc']['eligible_booths'], 0)
        self.assertEqual(changes['ppvc']['multiplier'], 1)
        preserved = live.update(draws, inputs, units, changes=changes)
        np.testing.assert_array_equal(preserved['unit_counts'][:, 1], np.full(128, 200))

    def test_assumed_finer_booth_sizes_do_not_override_mixed_group_evidence(self):
        inputs, draws, units = live_case()
        units[0]['weight'] = .5
        units.append(dict(units[0], name='other unfinished unit', kind='declaration', weight=.5))
        plain = live.update(draws, inputs, units)
        adapted = live.update(draws, inputs, units, changes={'ordinary': dict(multiplier=.7)})
        np.testing.assert_allclose(plain['totals'], adapted['totals'])
        np.testing.assert_allclose(plain['counts'][:, 0], adapted['counts'][:, 0])
        self.assertTrue((adapted['unit_counts'][:, 0] < plain['unit_counts'][:, 0]).all())
        self.assertTrue((adapted['unit_counts'][:, -1] > plain['unit_counts'][:, -1]).all())

    def test_tiny_eav_does_not_inherit_completed_ppvc_shortfall(self):
        inputs, draws, units = live_case()
        inputs['enrolment'] = (np.asarray(inputs['enrolment'])*100).tolist()
        draws['totals'] *= 100
        draws['counts'] *= 100
        units[0]['counted'] = 40000
        units[1].update(weight=.999, counted=15000)
        units.append(dict(units[1], name='EAV A PPVC', weight=.001, counted=0, is_eav=True))
        result = live.update(draws, inputs, units)
        self.assertTrue((result['unit_counts'][:, -1] < 100).all())
        self.assertTrue((result['remaining'][:, 2:4] > 0).all())
        np.testing.assert_allclose(result['counts'].sum(axis=2), result['totals'])

    def test_partial_compensation_requires_large_reliable_non_eav_centre(self):
        units = [dict(seat_index=0, group_index=0, kind='ppvc', matched=True, name='One', counted=0),
                 dict(seat_index=0, group_index=0, kind='ppvc', matched=True, name='Two', counted=9000),
                 dict(seat_index=0, group_index=0, kind='ppvc', matched=True, name='EAV A PPVC', counted=0)]
        expected = np.array([[5000., 10000., 30.]])
        complete = np.array([False, True, False])
        shifts = live.ppvc_compensation_shifts(units, expected, np.array([100000]), complete)
        self.assertGreater(shifts[0], 0)
        self.assertEqual(shifts[2], 0)
        units[0]['matched'] = False
        self.assertEqual(live.ppvc_compensation_shifts(units, expected, np.array([100000]), complete)[0], 0)
        units[0]['matched'] = True
        expected[0, 0] = 1000
        self.assertEqual(live.ppvc_compensation_shifts(units, expected, np.array([100000]), complete)[0], 0)

    def test_unmatched_and_eav_units_do_not_receive_national_trend(self):
        inputs, draws, units = live_case()
        units[1].update(matched=False, name='Unmatched PPVC')
        units[5].update(name='EAV B PPVC')
        plain = live.update(draws, inputs, units)
        adapted = live.update(draws, inputs, units, changes={'ppvc': dict(multiplier=.7)})
        np.testing.assert_allclose(plain['unit_counts'], adapted['unit_counts'])

    def test_unmatched_eav_overrides_generic_size_using_historical_services(self):
        units = [dict(name='EAV A PPVC', matched=True), dict(name='EAV B PPVC', matched=True),
                 dict(name='EAV New PPVC', matched=False), dict(name='Public PPVC', matched=False)]
        original = np.array([[20., 40., 2000., 5000.], [22., 44., 2000., 5000.]])
        result = live.eav_starting_counts(original, units)
        np.testing.assert_array_equal(result[:, 2], [30, 33])
        np.testing.assert_array_equal(result[:, 3], [5000, 5000])
        np.testing.assert_array_equal(original[:, 2], [2000, 2000])

    def test_pooled_ppvc_strength_depends_on_votes_not_number_of_booths(self):
        small = [dict(name='Small', kind='ppvc', matched=True, counted=100, weight=1,
                      seat_index=0, group_index=0) for _ in range(10)]
        large = [dict(small[0], name='Large', counted=1000)]
        a = live.pooled_booth_changes(small, np.array([[200.]]))['ppvc']
        b = live.pooled_booth_changes(large, np.array([[2000.]]))['ppvc']
        self.assertAlmostEqual(a['multiplier'], b['multiplier'])
        self.assertEqual(a['observed_votes'], 1000)
        self.assertLess(a['observation_strength'], .01)
        self.assertGreater(a['multiplier'], .99)

    def test_compensation_order_is_identical_without_a_pooled_change(self):
        inputs, draws, units = live_case()
        before = live.update(draws, inputs, units, compensation_order='before_pool')
        after = live.update(draws, inputs, units, compensation_order='after_pool')
        np.testing.assert_array_equal(before['unit_counts'], after['unit_counts'])

    def test_delayed_ppvc_estimate_preserves_counted_votes_and_eav(self):
        inputs, draws, units = live_case()
        units[0]['counted'] = 100
        units[5]['name'] = 'EAV B PPVC'
        plain = live.update(draws, inputs, units)
        delayed = live.update(draws, inputs, units, unreported_ppvc_factor=.1)
        np.testing.assert_array_equal(plain['totals'], delayed['totals'])
        np.testing.assert_array_equal(delayed['unit_counts'][:, 0], np.full(128, 100))
        self.assertTrue((delayed['unit_counts'][:, 1] < plain['unit_counts'][:, 1]).all())
        self.assertTrue((delayed['unit_counts'][:, 1] > 0).all())
        # EAV receives no direct clock adjustment. It can still change slightly
        # through the common district accounting constraint, so check its input
        # role and completion state rather than asserting an independent total.
        self.assertFalse(delayed['complete'][5])
        self.assertTrue((delayed['unit_counts'][:, 5] > 0).all())
        units[1]['counted'] = 200
        reported = live.update(draws, inputs, units, unreported_ppvc_factor=.1)
        np.testing.assert_array_equal(reported['unit_counts'][:, 1], np.full(128, 200))
        # With the public centre counted, only EAV remains eligible by type.
        # A clock factor must then have no effect anywhere in the calculation.
        plain_reported = live.update(draws, inputs, units)
        np.testing.assert_array_equal(reported['unit_counts'], plain_reported['unit_counts'])

    def test_exceptional_complete_booth_difference_does_not_change_input(self):
        unit = dict(seat_name='A', name='one', kind='ordinary', counted=500)
        differences = prototype.substantial_complete_booth_differences([unit], {('A', 'one'): 700})
        self.assertEqual(differences[0]['difference'], 200)
        self.assertEqual(unit['counted'], 500)

    def test_snapshot_copy_retains_original_party_counts(self):
        booth = dict(seat_name='A', name='one', vote_type='Ordinary', booth_type='Normal', same_seat=True,
                     node=dict(fp_votes_current=[dict(party_index=12, value=7)], fp_votes_previous=[dict(value=10)]))
        base = [dict(seat_name='A', name='one', group='remaining', kind='ordinary', matched=True, previous=10)]
        analysis = dict(booths=[booth], seats=[dict(name='A', node=booth['node'])])
        units, totals = prototype.snapshot_units(base, analysis, dict(seat_names=['A']))
        self.assertEqual(units[0]['counted_party_votes'], booth['node']['fp_votes_current'])
        self.assertEqual(totals, {'A': 7})
        self.assertNotIn('counted', base[0])


if __name__ == '__main__':
    unittest.main()

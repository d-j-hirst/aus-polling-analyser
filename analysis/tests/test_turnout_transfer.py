"""Test competing turnout explanations with fictional, fully controlled counts."""
import copy
import unittest
import numpy as np
from lib.turnout import live, transfer


def example(shortfall, remaining=(1000.,), *, counted_late=0., samples=256):
    # Large enrolment keeps this experiment away from the physical cap; the
    # category discrepancies alone determine the preferred explanation.
    sizes = np.array([2000., *remaining])
    counts = np.tile(sizes, (samples, 1, 1))
    inputs = dict(election_code='example', seat_names=['Example'], enrolment=[10000.],
                  categories=['ordinary']+[f'service{i}' for i in range(len(remaining))])
    draws = dict(counts=counts, totals=counts.sum(axis=2))
    units = [dict(seat_index=0, seat_name='Example', group_index=j,
                  group=inputs['categories'][j], name=f'Unit {j}', weight=1., matched=True,
                  counted=2000.-shortfall if j == 0 else counted_late,
                  kind='ordinary' if j == 0 else 'declaration') for j in range(len(sizes))]
    return inputs, draws, units


class TransferTests(unittest.TestCase):
    def update(self, shortfall, **kwargs):
        inputs, draws, units = example(shortfall, **kwargs)
        return live.update(draws, inputs, units)

    def test_transfer_recedes_for_extreme_required_changes(self):
        cases = [self.update(d) for d in (100., 250., 600., 1200.)]
        probability = [r['transfer_probability'][0, 0] for r in cases]
        self.assertGreater(probability[0], .65)
        self.assertGreater(probability[1], .5)
        self.assertLess(probability[2], .5)
        self.assertLess(probability[3], .02)
        self.assertTrue(all(a > b for a, b in zip(probability, probability[1:])))
        moved = [r['unit_counts'][:, 1].mean()-1000 for r in cases]
        self.assertGreater(moved[1], moved[0])
        self.assertLess(moved[-1], moved[1])

    def test_mixture_keeps_both_accounts_and_joint_conservation(self):
        result = self.update(250., remaining=(600., 400.))
        selected = result['transfer_selected'][:, 0].astype(bool)
        self.assertTrue(selected.any() and (~selected).any())
        # Every transfer outcome adds 250 once across the two categories;
        # turnout outcomes preserve each own size. None average the accounts.
        np.testing.assert_allclose(result['totals'][selected, 0], 3000.)
        np.testing.assert_allclose(result['totals'][~selected, 0], 2750.)
        np.testing.assert_allclose(result['unit_counts'][~selected, 1:],
                                   np.tile([600., 400.], ((~selected).sum(), 1)))
        np.testing.assert_array_equal(result['unit_counts'][:, 0], 1750.)
        self.assertGreater(np.std(result['totals'][:, 0]), 80.)

    def test_large_excess_accepts_higher_turnout_instead_of_erasing_category(self):
        result = self.update(-900.)
        self.assertLess(result['transfer_probability'][0, 0], .001)
        np.testing.assert_allclose(result['totals'][:, 0], 3900.)
        np.testing.assert_allclose(result['unit_counts'][:, 1], 1000.)
        modest = self.update(-100.)
        self.assertGreater(modest['transfer_probability'][0, 0], .6)

    def test_completion_reduces_capacity_and_small_services_stay_small(self):
        early = self.update(250.)
        late = self.update(250., counted_late=950.)
        self.assertLess(late['transfer_probability'][0, 0], early['transfer_probability'][0, 0]/100)
        np.testing.assert_array_equal(late['unit_counts'][:, 1], 1000.)
        tiny = self.update(500., remaining=(20., 10.))
        np.testing.assert_allclose(tiny['unit_counts'][:, 1:], np.tile([20., 10.], (256, 1)))

    def test_exclusions_are_optional_and_do_not_remove_votes(self):
        inputs, draws, units = example(250.)
        units[1]['transfer_exclusion_reason'] = 'New category without a comparable baseline.'
        result = live.update(draws, inputs, units)
        np.testing.assert_array_equal(result['transfer_probability'], 0.)
        np.testing.assert_array_equal(result['unit_counts'][:, 1], 1000.)
        units[1].pop('transfer_exclusion_reason')
        units[1]['matched'] = False
        self.assertGreater(live.update(draws, inputs, units)['transfer_probability'][0, 0], .5)

    def test_wide_small_category_prior_is_not_unlimited_transfer_capacity(self):
        inputs, draws, units = example(250., remaining=(1000., 20.))
        # This service is genuinely poorly known. Retain that wide distribution
        # but do not let its variance make it absorb most of a booth shortfall.
        draws['counts'][:, 0, 2] *= np.exp(np.linspace(-1.4, 1.4, 256))
        draws['totals'] = draws['counts'].sum(axis=2)
        result = live.update(draws, inputs, units)
        own = draws['counts'][:, 0, 2]
        np.testing.assert_allclose(result['own_remaining'][:, 2], own)
        self.assertLess(result['unit_counts'][:, 2].mean()/own.mean(), 1.35)
        self.assertLess(result['unit_counts'][:, 2].mean()-own.mean(), 15.)

    def test_order_replay_and_revision_do_not_change_sampler(self):
        inputs, draws, units = example(250., remaining=(600., 400.))
        first = live.update(draws, inputs, units)
        changed = copy.deepcopy(units)
        changed[0]['counted'] = 800.
        live.update(draws, inputs, changed)
        again = live.update(draws, inputs, units)
        reversed_result = live.update(draws, inputs, list(reversed(units)))
        np.testing.assert_array_equal(first['unit_counts'], again['unit_counts'])
        np.testing.assert_allclose(first['unit_counts'], reversed_result['unit_counts'][:, ::-1])
        np.testing.assert_array_equal(np.sort(transfer.choices('example', 256)), (np.arange(256)+.5)/256)

    def test_splitting_a_category_into_booths_does_not_change_weight(self):
        inputs, draws, units = example(250.)
        units[1]['kind'] = 'ordinary'
        original = live.update(draws, inputs, units)
        split = copy.deepcopy(units[1])
        units[1]['weight'] = .3
        split.update(weight=.7, name='second booth')
        result = live.update(draws, inputs, units+[split])
        np.testing.assert_allclose(original['transfer_probability'], result['transfer_probability'])
        np.testing.assert_allclose(original['unit_counts'][:, 1], result['unit_counts'][:, 1:].sum(axis=1))

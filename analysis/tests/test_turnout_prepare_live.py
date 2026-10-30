"""Check the operational input boundary without private feeds or fitting jobs."""

import copy
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from scripts.turnout import turnout_prepare_live as preparation


EXAMPLE = Path(__file__).resolve().parents[2] / 'tests/fixtures/turnout/live-inputs-example.json'


class PrepareLiveTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads(EXAMPLE.read_text(encoding='utf-8'))

    def test_standard_prior_has_conserved_counts_and_no_snapshot_history(self):
        result = preparation.prepare(self.config, provenance={'example': True})
        self.assertNotIn('history', result)
        totals = np.array(result['prior']['totals'])
        counts = np.array(result['prior']['counts']).reshape(8, 2, 3)
        np.testing.assert_allclose(counts.sum(axis=2), totals)
        self.assertTrue((counts > 0).all())
        self.assertTrue((totals < self.config['inputs']['enrolment']).all())
        self.assertEqual(result['options']['receipt_deadline'], None)
        self.assertEqual(result['election'], 'example')

    def test_election_schedule_is_supplied_by_configuration(self):
        config = copy.deepcopy(self.config)
        config['schedule'].update(receipt_deadline='2030-01-20T18:00:00',
            postal_deadline='2030-01-20T18:00:00', poll_close='2030-01-10T18:00:00', ppvc_reporting_decay=True)
        result = preparation.prepare(config, provenance={})
        self.assertEqual(result['options']['receipt_deadline'], config['schedule']['receipt_deadline'])
        self.assertTrue(result['options']['ppvc_reporting_decay'])

    def test_optional_transfer_exclusion_preserves_legacy_configuration(self):
        old = preparation.prepare(self.config, provenance={})
        self.config['units'][0]['transfer_exclusion_reason'] = 'New category with no comparable history.'
        new = preparation.prepare(self.config, provenance={})
        self.assertEqual(new['units'][0]['transfer_exclusion_reason'], self.config['units'][0]['transfer_exclusion_reason'])
        np.testing.assert_array_equal(old['prior']['counts'], new['prior']['counts'])
        for reason in (True, None, '  '):
            self.config['units'][0]['transfer_exclusion_reason'] = reason
            with self.assertRaises(ValueError):
                preparation.prepare(self.config, provenance={})

    def test_processing_timetables_survive_preparation_and_validate(self):
        timings = {'Absent': {'days_after_poll_close':16, 'transition_days':2}, 'Provisional':None}
        self.config['schedule']['processing_timetables'] = timings
        result = preparation.prepare(self.config, provenance={})
        self.assertEqual(result['options']['processing_timetables'],timings)
        self.config['schedule']['processing_timetables']['Absent']['transition_days'] = -1
        with self.assertRaises(ValueError):
            preparation.prepare(self.config, provenance={})

    def test_postponed_contest_is_explicit_and_outside_count_population(self):
        contest = dict(seat='Example Postponed', status='postponed', reason='Polling was postponed.')
        self.config['inactive_contests'] = [contest]
        result = preparation.prepare(self.config, provenance={})
        self.assertEqual(result['inactive_contests'], [contest])
        self.assertNotIn(contest['seat'], result['prior']['seats'])
        for invalid in (dict(contest, seat='Example East'), dict(contest, status='closed'), dict(contest, reason='')):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                preparation.prepare(dict(self.config, inactive_contests=[invalid]), provenance={})
        with self.assertRaises(ValueError):
            preparation.prepare(dict(self.config, inactive_contests=[contest, contest]), provenance={})

    def test_mapping_changes_and_embedded_current_counts_are_rejected(self):
        for change in ('identity', 'count', 'closure', 'partition', 'preset'):
            config = copy.deepcopy(self.config)
            if change == 'identity': config['units'].append(config['units'][0])
            if change == 'count': config['units'][0]['counted'] = 100
            if change == 'closure': config['units'][0]['closed'] = True
            if change == 'partition': config['inputs']['remainder']['indices'] = [1]
            if change == 'preset': config['inputs']['remainder']['weights'] = [[.5, .5], [.5, .5]]
            with self.subTest(change=change), self.assertRaises(ValueError):
                preparation.prepare(config, provenance={})

    def test_command_writes_versioned_input_with_source_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'turnout-prior.json'
            preparation.main([str(EXAMPLE), '--output', str(output)])
            value = json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual(value['schema_version'], 1)
            self.assertEqual(len(value['provenance']['configuration_sha256']), 64)
            self.assertEqual(value['provenance']['parameter_preset'], self.config['parameter_preset'])


if __name__ == '__main__':
    unittest.main()

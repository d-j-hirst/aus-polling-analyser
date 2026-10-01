"""Focused checks for conservative evidence selection and refresh detection."""

from dataclasses import replace
import json
from pathlib import Path
import tempfile
import unittest

from lib.shared.turnout_data import (OperationalObservation, SeatTotal,
                                    SourceDefinition, TurnoutDataset, VoteTypeRecord)
from scripts.turnout import turnout_evidence_audit as audit


def observation(**changes):
    row = OperationalObservation(
        election_code='2022vic', source_id='antony-green-2022vic-election-eve',
        measure='postal_applications_cumulative', observed_at='2022-11-24',
        count=8294, geography_basis='elector_division',
        observation_status='contemporaneous', seat_name='Albert Park District')
    return replace(row, **changes)


class TurnoutEvidenceAuditTests(unittest.TestCase):
    def test_point_counts_and_documented_precision_only(self):
        cases = [
            (observation(), 'exact'),
            (observation(count=0), 'exact'),
            (observation(count_basis='forecast'), 'forecast'),
            (observation(count_relation='lower_bound'), 'bound'),
            (observation(count_precision='approximate', source_category='Around 8,000 applications'),
             'coarse_or_unspecified_approximation'),
            (observation(count_precision='approximate', derivation='rounded_rate_times_enrolment',
                         source_category='Published Postal % (17% of enrolment)'), 'close_rate'),
            (observation(source_id='unreviewed-table', count_precision='approximate',
                         derivation='rounded_rate_times_enrolment',
                         source_category='Published Postal % (17% of enrolment)'),
             'coarse_or_unspecified_approximation'),
            (observation(source_id='two-decimal-table', count_precision='approximate',
                         derivation='rounded_rate_times_enrolment',
                         source_category='24.22% of enrolment'), 'close_rate'),
        ]
        for row, expected in cases:
            with self.subTest(row=row):
                self.assertEqual(audit.precision_decision(row)[0], expected)

    def test_latest_control_accepts_downward_revision_without_post_poll_leakage(self):
        rows = [observation(count=100, observed_at='2022-11-23'), observation(count=90),
                observation(count=120, observed_at='2022-11-27'),
                observation(count=91, observation_status='final_reconciled')]
        selected, reasons = audit.latest_controls(rows, '2022-11-26')
        self.assertEqual(sorted(row.count for row, _ in selected), [90, 91])
        self.assertEqual(reasons['after_polling_day'], 1)
        self.assertEqual(reasons['superseded_precise_rows'], 1)

    def test_unsupported_geography_and_category_splits_stay_out(self):
        row = observation(measure='prepoll_votes_cast_cumulative')
        self.assertEqual(audit.target_definition(row, 'sa', {'declaration_combined'})[2], 'combined_only')
        self.assertEqual(audit.target_definition(row, 'fed', {'ordinary_combined', 'declaration_early'})[2],
                         'needs_split')
        self.assertEqual(audit.target_definition(replace(row, geography_basis='administering_division'),
                                               'qld', {'early_in_person'})[2], 'aggregate_only')
        self.assertEqual(audit.target_definition(replace(row, election_code='2015nsw'),
                                               'nsw', {'early_combined'})[1], 'Total Pre-Poll Ordinary Votes')

    def test_source_updates_change_fingerprint_but_formatting_does_not(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / '2026sa.json'
            payload = dict(count=454862, source='mutable-publication')
            path.write_text(json.dumps(payload), encoding='utf-8')
            original = audit.source_fingerprint([path])
            path.write_text(json.dumps(payload, indent=2), encoding='utf-8')
            self.assertEqual(audit.source_fingerprint([path]), original)
            payload['count'] = 454860
            path.write_text(json.dumps(payload), encoding='utf-8')
            self.assertNotEqual(audit.source_fingerprint([path]), original)

    def test_missing_district_category_rows_cannot_make_a_state_target(self):
        # Final totals alone do not fill a missing category partition. This is
        # the older Victorian source gap discovered by the first audit run.
        dataset = TurnoutDataset(
            sources=[SourceDefinition('final-results', '2022vic', 'Commission',
                                      'retained-source', 'test-adapter', 'final', 'test-regime')],
            seat_totals=[SeatTotal('2022vic', name, 'final-results', 1000, 800, 50, 850)
                         for name in ['A', 'B']],
            vote_types=[VoteTypeRecord('2022vic', 'A', 'final-results', 'final-partition',
                                       'Postal', 'postal', 100, 10, 110, 'partial')])
        target, problem = audit.final_target(dataset, observation(seat_name='', geography_basis='state'),
                                            ['postal'], '')
        self.assertIsNone(target)
        self.assertIn('incomplete', problem)


if __name__ == '__main__':
    unittest.main()

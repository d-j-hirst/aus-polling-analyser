import json
from pathlib import Path
import tempfile
import unittest

import turnout_data


class TurnoutDataTests(unittest.TestCase):
    def make_dataset(self):
        return turnout_data.TurnoutDataset(
            elections=[
                turnout_data.ElectionDefinition('2025fed', '2025-05-03', 'fed')
            ],
            sources=[
                turnout_data.SourceDefinition(
                    source_id='aec-2025-final',
                    election_code='2025fed',
                    authority='Australian Electoral Commission',
                    locator='https://results.aec.gov.au/31496/',
                    adapter='aec-final-vote-types-v1',
                    status='final',
                    category_regime='aec-ordinary-combined-v1',
                )
            ],
            seat_totals=[
                turnout_data.SeatTotal(
                    election_code='2025fed',
                    seat_name='Example',
                    source_id='aec-2025-final',
                    enrolment=1200,
                    formal_votes=1000,
                    informal_votes=50,
                    total_ballots=1050,
                    source_seat_id='999',
                )
            ],
            vote_types=[
                turnout_data.VoteTypeRecord(
                    election_code='2025fed',
                    seat_name='Example',
                    source_id='aec-2025-final',
                    partition_id='final-formal-vote-types',
                    source_category='OrdinaryVotes',
                    canonical_category='ordinary_combined',
                    formal_votes=800,
                    informal_votes=None,
                    total_ballots=None,
                    coverage='complete',
                ),
                turnout_data.VoteTypeRecord(
                    election_code='2025fed',
                    seat_name='Example',
                    source_id='aec-2025-final',
                    partition_id='final-formal-vote-types',
                    source_category='PostalVotes',
                    canonical_category='postal',
                    formal_votes=200,
                    informal_votes=None,
                    total_ballots=None,
                    coverage='complete',
                ),
            ],
        )

    def test_complete_vote_type_partition_must_match_seat_formal_total(self):
        dataset = self.make_dataset()
        dataset.validate()

        dataset.vote_types[1] = turnout_data.VoteTypeRecord(
            **{
                **dataset.vote_types[1].__dict__,
                'formal_votes': 199,
            }
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'formal votes total 999, expected 1000',
        ):
            dataset.validate()

    def test_combined_official_category_is_preserved_without_invented_split(self):
        dataset = self.make_dataset()
        dataset.validate()

        ordinary = dataset.vote_types[0]
        self.assertEqual(ordinary.source_category, 'OrdinaryVotes')
        self.assertEqual(ordinary.canonical_category, 'ordinary_combined')

    def test_unknown_informal_count_is_distinct_from_zero(self):
        dataset = self.make_dataset()
        dataset.validate()

        self.assertIsNone(dataset.vote_types[0].informal_votes)

    def test_partial_partition_can_be_less_than_seat_total(self):
        dataset = self.make_dataset()
        dataset.vote_types = [
            turnout_data.VoteTypeRecord(
                **{
                    **dataset.vote_types[0].__dict__,
                    'coverage': 'partial',
                }
            )
        ]

        dataset.validate()

    def test_operational_observations_are_separate_from_final_votes(self):
        dataset = self.make_dataset()
        dataset.sources.append(
            turnout_data.SourceDefinition(
                source_id='aec-2025-daily',
                election_code='2025fed',
                authority='Australian Electoral Commission',
                locator='https://example.test/daily',
                adapter='aec-operational-v1',
                status='operational',
                category_regime='aec-pre-election-v1',
            )
        )
        dataset.operational_observations.append(
            turnout_data.OperationalObservation(
                election_code='2025fed',
                source_id='aec-2025-daily',
                measure='postal_applications',
                observed_at='2025-05-01T17:00:00+10:00',
                count=250,
                geography_basis='elector_division',
                observation_status='contemporaneous',
                seat_name='Example',
                source_category='Postal applications',
            )
        )

        dataset.validate()
        self.assertEqual(dataset.operational_observations[0].count, 250)

    def test_operational_only_dataset_can_identify_districts_before_results(self):
        dataset = self.make_dataset()
        dataset.seat_totals = []
        dataset.vote_types = []
        dataset.sources = [turnout_data.SourceDefinition(
            source_id='ecsa-2026-daily',
            election_code='2025fed',
            authority='Electoral Commission SA',
            locator='https://example.test/daily',
            adapter='ecsa-operational-v1',
            status='operational',
            category_regime='ecsa-early-vote-v1',
        )]
        dataset.operational_observations = [
            turnout_data.OperationalObservation(
                election_code='2025fed',
                source_id='ecsa-2026-daily',
                measure='prepoll_votes_cast_cumulative',
                observed_at='2025-05-02',
                count=100,
                geography_basis='elector_division',
                observation_status='contemporaneous',
                seat_name='Example',
            )
        ]

        dataset.validate()

    def test_operational_observation_requires_explicit_geography(self):
        observation = turnout_data.OperationalObservation(
            election_code='2025fed',
            source_id='aec-2025-daily',
            measure='prepoll_votes_issued_cumulative',
            observed_at='2025-05-01',
            count=250,
            geography_basis='unknown',
            observation_status='contemporaneous',
            seat_name='Example',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'unsupported geography_basis',
        ):
            observation.validate()

    def test_division_observation_requires_a_seat(self):
        observation = turnout_data.OperationalObservation(
            election_code='2025fed',
            source_id='aec-2025-daily',
            measure='postal_applications',
            observed_at='2025-05-01',
            count=250,
            geography_basis='elector_division',
            observation_status='contemporaneous',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'requires seat_name',
        ):
            observation.validate()

    def test_operational_observation_rejects_null_count(self):
        observation = turnout_data.OperationalObservation(
            election_code='2025fed',
            source_id='aec-2025-daily',
            measure='postal_applications',
            observed_at='2025-05-01',
            count=None,
            geography_basis='elector_division',
            observation_status='contemporaneous',
            seat_name='Example',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'count must be an integer',
        ):
            observation.validate()

    def test_approximate_operational_observation_preserves_its_derivation(self):
        dataset = self.make_dataset()
        dataset.sources.append(
            turnout_data.SourceDefinition(
                source_id='published-election-eve',
                election_code='2025fed',
                authority='Example publisher',
                locator='https://example.test/election-eve',
                adapter='published-operational-v1',
                status='operational',
                category_regime='published-election-eve-v1',
            )
        )
        dataset.operational_observations.append(
            turnout_data.OperationalObservation(
                election_code='2025fed',
                source_id='published-election-eve',
                measure='prepoll_votes_cast_cumulative',
                observed_at='2025-05-02',
                count=321,
                geography_basis='elector_division',
                observation_status='contemporaneous',
                seat_name='Example',
                count_precision='approximate',
                derivation='rounded_rate_times_enrolment',
            )
        )

        restored = turnout_data.dataset_from_dict(
            turnout_data.dataset_to_dict(dataset)
        )

        self.assertEqual(
            restored.operational_observations[0].count_precision,
            'approximate',
        )
        self.assertEqual(
            restored.operational_observations[0].derivation,
            'rounded_rate_times_enrolment',
        )

    def test_rounded_rate_count_cannot_be_labelled_exact(self):
        observation = turnout_data.OperationalObservation(
            election_code='2025fed',
            source_id='published-election-eve',
            measure='prepoll_votes_cast_cumulative',
            observed_at='2025-05-02',
            count=321,
            geography_basis='elector_division',
            observation_status='contemporaneous',
            seat_name='Example',
            derivation='rounded_rate_times_enrolment',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'derived from a rounded rate must be approximate',
        ):
            observation.validate()

    def test_enrolment_only_seat_can_anchor_pre_election_observations(self):
        dataset = self.make_dataset()
        dataset.seat_totals[0] = turnout_data.SeatTotal(
            election_code='2025fed',
            seat_name='Example',
            source_id='aec-2025-final',
            enrolment=1200,
            formal_votes=None,
            informal_votes=None,
            total_ballots=None,
        )
        dataset.vote_types = []

        dataset.validate()

    def test_operational_source_cannot_supply_final_vote_types(self):
        dataset = self.make_dataset()
        dataset.sources[0] = turnout_data.SourceDefinition(
            **{
                **dataset.sources[0].__dict__,
                'status': 'operational',
            }
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'cannot supply final seat totals',
        ):
            dataset.validate()

    def test_vote_type_record_must_reference_its_sources_election(self):
        dataset = self.make_dataset()
        dataset.sources[0] = turnout_data.SourceDefinition(
            **{
                **dataset.sources[0].__dict__,
                'election_code': '2022fed',
            }
        )
        dataset.elections.append(
            turnout_data.ElectionDefinition('2022fed', '2022-05-21', 'fed')
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'belongs to 2022fed, not 2025fed',
        ):
            dataset.validate()

    def test_negative_and_internally_inconsistent_counts_are_rejected(self):
        dataset = self.make_dataset()
        dataset.seat_totals[0] = turnout_data.SeatTotal(
            **{
                **dataset.seat_totals[0].__dict__,
                'informal_votes': 60,
            }
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'do not equal total ballots',
        ):
            dataset.validate()

    def test_json_round_trip_preserves_null_counts(self):
        dataset = self.make_dataset()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'turnout.json'
            turnout_data.write_dataset_atomically(path, dataset)
            restored = turnout_data.load_dataset(path)

            self.assertEqual(restored, dataset)
            self.assertIsNone(restored.vote_types[0].informal_votes)
            with open(path, encoding='utf-8') as source:
                self.assertEqual(json.load(source)['schema_version'], 5)

    def test_operational_count_relation_round_trips_and_is_validated(self):
        dataset = self.make_dataset()
        dataset.sources.append(turnout_data.SourceDefinition(
            source_id='aec-2025-daily',
            election_code='2025fed',
            authority='Australian Electoral Commission',
            locator='https://example.test/daily',
            adapter='aec-operational-v1',
            status='operational',
            category_regime='aec-pre-election-v1',
        ))
        observation = turnout_data.OperationalObservation(
            election_code='2025fed',
            source_id='aec-2025-daily',
            measure='postal_votes_ready',
            observed_at='2025-05-02',
            count=1000,
            geography_basis='national',
            observation_status='contemporaneous',
            count_precision='approximate',
            count_relation='lower_bound',
        )
        dataset.operational_observations.append(observation)

        restored = turnout_data.dataset_from_dict(
            turnout_data.dataset_to_dict(dataset)
        )
        self.assertEqual(
            restored.operational_observations[0].count_relation,
            'lower_bound',
        )

        invalid = turnout_data.OperationalObservation(
            **{
                **observation.__dict__,
                'count_relation': 'unsupported',
            }
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'unsupported count_relation'
        ):
            invalid.validate()

    def test_operational_forecast_basis_round_trips_and_is_validated(self):
        dataset = self.make_dataset()
        dataset.sources.append(turnout_data.SourceDefinition(
            source_id='aec-2025-daily',
            election_code='2025fed',
            authority='Australian Electoral Commission',
            locator='https://example.test/daily',
            adapter='aec-operational-v1',
            status='operational',
            category_regime='aec-pre-election-v1',
        ))
        observation = turnout_data.OperationalObservation(
            election_code='2025fed',
            source_id='aec-2025-daily',
            measure='expected_prepoll_votes',
            observed_at='2025-05-02',
            count=1000,
            geography_basis='national',
            observation_status='contemporaneous',
            count_precision='approximate',
            count_basis='forecast',
        )
        dataset.operational_observations.append(observation)

        restored = turnout_data.dataset_from_dict(
            turnout_data.dataset_to_dict(dataset)
        )
        self.assertEqual(
            restored.operational_observations[0].count_basis,
            'forecast',
        )

        invalid = turnout_data.OperationalObservation(
            **{
                **observation.__dict__,
                'count_basis': 'unsupported',
            }
        )
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError, 'unsupported count_basis'
        ):
            invalid.validate()

    def test_version_two_operational_records_default_to_exact_direct_counts(self):
        dataset = self.make_dataset()
        payload = turnout_data.dataset_to_dict(dataset)
        payload['schema_version'] = 2

        restored = turnout_data.dataset_from_dict(payload)

        self.assertEqual(restored, dataset)

    def test_version_one_without_operational_records_remains_readable(self):
        payload = turnout_data.dataset_to_dict(self.make_dataset())
        payload['schema_version'] = 1

        restored = turnout_data.dataset_from_dict(payload)

        self.assertEqual(restored, self.make_dataset())

    def test_version_one_operational_records_require_regeneration(self):
        payload = turnout_data.dataset_to_dict(self.make_dataset())
        payload['schema_version'] = 1
        payload['operational_observations'] = [{
            'election_code': '2025fed',
            'source_id': 'aec-2025-daily',
            'measure': 'postal_applications',
            'observed_at': '2025-05-01',
            'count': 250,
            'seat_name': 'Example',
            'source_category': 'Postal applications',
        }]

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'do not identify geography',
        ):
            turnout_data.dataset_from_dict(payload)


if __name__ == '__main__':
    unittest.main()

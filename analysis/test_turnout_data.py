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
                seat_name='Example',
                source_category='Postal applications',
            )
        )

        dataset.validate()
        self.assertEqual(dataset.operational_observations[0].count, 250)

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


if __name__ == '__main__':
    unittest.main()

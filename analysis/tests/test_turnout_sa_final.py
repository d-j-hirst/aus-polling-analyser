"""Check SA final category accounting, source status and operational retention."""

import json
import unittest

from lib.shared import turnout_data
from scripts.turnout import turnout_sa_final as adapter
from scripts.turnout import turnout_evidence_audit as audit


def source_fixture():
    # Small counts repeated over 47 districts exercise the real source contract
    # without a network dependency or a large copy of election data in tests.
    structures, districts = [], []
    for index in range(47):
        name = 'District {}'.format(index)
        structures.append(dict(districtName=name, districtEnrolled=100, pollingPlaces=[
            dict(pollingPlaceName='Booth', pollingPlaceType='Polling Booth'),
            dict(pollingPlaceName='EVC', pollingPlaceType='Early Voting Centre')]))
        districts.append(dict(districtId=name, informalDeclarationVotes=1,
            pollingPlaces=[dict(pollingPlaceName=label, pollingPlaceTypeIdName=kind,
                informalVotes=1, pollingCandidates=[dict(candidateId=1, formalVotes=formal)])
                for label, kind, formal in [('Booth', 'PB', 55), ('EVC', 'PP', 10)]],
            candidates=[dict(candidateId=1, ordinaryVotes=65, declarationVotes=9)],
            declarations=[dict(declarationType=label, formalVotes=formal, informalVotes=informal,
                candidateVotes=[dict(candidateId=1, votes=formal)])
                for label, formal, informal in [('Postal - Declaration 1', 7, 1),
                                               ('Early Voting - Declaration 1', 2, 0)]],
            absentOrdinary=[dict(absentOrdinaryType='Early Voting - Absent Declaration 1',
                formalVotes=5, informalVotes=2, candidateVotes=[dict(candidateId=1, votes=5)])]))
    return {name: json.dumps(value).encode() for name, value in dict(
        elections=dict(elections=[dict(electionDate=adapter.ELECTION_DATE, electionStatus='final')]),
        static=dict(electionDate=adapter.ELECTION_DATE, districts=structures),
        results=dict(electionDate=adapter.ELECTION_DATE, electionStatus='final',
                     lastUpdated='2026-09-21T14:59:56.227', dataVersion=912, districts=districts)).items()}


class SaFinalTurnoutTests(unittest.TestCase):
    def test_absent_ordinary_is_added_once_and_all_early_modes_form_the_target(self):
        dataset = adapter.build_dataset(source_fixture())
        self.assertEqual(dataset.seat_totals[0].formal_votes, 79)
        self.assertEqual(dataset.seat_totals[0].total_ballots, 84)
        control = turnout_data.OperationalObservation(adapter.ELECTION_CODE, adapter.SOURCE_ID,
            'prepoll_votes_cast_cumulative', '2026-03-20', 20, 'elector_division',
            'contemporaneous', seat_name=dataset.seat_totals[0].seat_name)
        pool, label, status, _ = audit.target_definition(control, 'sa',
            {r.canonical_category for r in dataset.vote_types})
        self.assertEqual(status, 'supported')
        target, problem = audit.final_target(dataset, control, pool, label)
        self.assertEqual(problem, '')
        self.assertEqual(target['formal_votes'], 17)
        self.assertEqual(target['total_ballots'], 20)

    def test_nonfinal_source_and_inconsistent_batch_are_rejected(self):
        raw = source_fixture()
        value = json.loads(raw['results'])
        value['electionStatus'] = 'recheck'
        raw['results'] = json.dumps(value).encode()
        with self.assertRaisesRegex(turnout_data.TurnoutDataError, 'reviewed final'):
            adapter.build_dataset(raw)
        value['electionStatus'] = 'final'
        value['districts'][0]['declarations'][0]['formalVotes'] += 1
        raw['results'] = json.dumps(value).encode()
        with self.assertRaisesRegex(turnout_data.TurnoutDataError, 'batch total'):
            adapter.build_dataset(raw)

    def test_refresh_retains_operational_counts_without_duplicate_final_rows(self):
        final = adapter.build_dataset(source_fixture())
        source = turnout_data.SourceDefinition('early-source', adapter.ELECTION_CODE, 'ECSA',
            'https://example.test/tally', 'operational-test', 'operational', 'early-markoffs')
        control = turnout_data.OperationalObservation(adapter.ELECTION_CODE, source.source_id,
            'prepoll_votes_cast_cumulative', '2026-03-20', 20, 'state', 'contemporaneous')
        previous = turnout_data.TurnoutDataset(elections=final.elections,
            sources=[source], operational_observations=[control])
        merged = adapter.merge_dataset(previous, final)
        refreshed = adapter.merge_dataset(merged, adapter.build_dataset(source_fixture()))
        self.assertEqual(refreshed.operational_observations, [control])
        self.assertEqual(len(refreshed.seat_totals), 47)
        self.assertEqual(len(refreshed.vote_types), len(final.vote_types))


if __name__ == '__main__':
    unittest.main()

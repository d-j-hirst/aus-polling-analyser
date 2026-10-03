"""Check that the official polling-place split preserves formal vote counts."""

import unittest

from lib.shared.turnout_data import (
    ElectionDefinition, SeatTotal, TurnoutDataError, TurnoutDataset, VoteTypeRecord,
)
from scripts.turnout import turnout_federal_prepoll as prepoll


def fixture():
    dataset = TurnoutDataset(
        elections=[ElectionDefinition('2025fed', '2025-05-03', 'fed')],
        seat_totals=[SeatTotal('2025fed', 'Example', 'final', 1000, 780, 25, 805,
                               source_seat_id='7')],
        vote_types=[
            VoteTypeRecord('2025fed', 'Example', 'final', 'partition', 'OrdinaryVotes',
                           'ordinary_combined', 700, 25, 725, 'complete'),
            VoteTypeRecord('2025fed', 'Example', 'final', 'partition', 'PrePollVotes',
                           'declaration_early', 80, 0, 80, 'complete'),
        ])
    places = b'DivisionID,PollingPlaceID,PollingPlaceTypeID\n7,10,5\n7,20,1\n'
    header = b'DivisionID,PollingPlaceID,PartyNm,OrdinaryVotes\n'
    files = {state: b'Final results metadata\n' + header for state in prepoll.STATES}
    files['places'] = places
    files['ACT'] += (b'7,10,Party A,100\n7,10,Party B,200\n7,10,Informal,20\n'
                     b'7,20,Party A,400\n7,20,Informal,5\n')
    return dataset, files


class FederalPrepollTests(unittest.TestCase):
    def test_formal_early_excludes_informal_and_adds_declarations_once(self):
        dataset, files = fixture()
        result = prepoll.build_election(dataset, files)['districts'][0]
        self.assertEqual(result['all_ordinary_formal'], 700)
        self.assertEqual(result['ordinary_early_formal'], 300)
        self.assertEqual(result['ordinary_early_informal'], 20)
        self.assertEqual(result['declaration_early_formal'], 80)
        self.assertEqual(result['final_early_formal'], 380)

    def test_changed_result_cannot_silently_disagree_with_normalized_ordinary(self):
        dataset, files = fixture()
        files['ACT'] = files['ACT'].replace(b'Party A,400', b'Party A,401')
        with self.assertRaisesRegex(TurnoutDataError, 'differ from final'):
            prepoll.build_election(dataset, files)

    def test_unmatched_polling_place_cannot_silently_drop_votes(self):
        dataset, files = fixture()
        files['ACT'] = files['ACT'].replace(b'7,20,Party A', b'7,30,Party A')
        with self.assertRaisesRegex(TurnoutDataError, 'identity does not match'):
            prepoll.build_election(dataset, files)


if __name__ == '__main__':
    unittest.main()

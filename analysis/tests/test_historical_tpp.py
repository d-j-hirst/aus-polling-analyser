import unittest
from types import SimpleNamespace

from lib.elections.historical_tpp import parse_aec_tpp, seat_tpp, TppResult, apply_reviewed_corrections, load_tpp_history
from lib.shared.election_code import ElectionCode


class HistoricalTppTests(unittest.TestCase):
    def test_invalid_comparison_preserves_valid_current_share(self):
        text = ('metadata\nDivisionNm,Australian Labor Party Percentage,'
                'Liberal/National Coalition Percentage,Swing\nExample,60,40,60\n')
        self.assertEqual(parse_aec_tpp(text)['Example'], TppResult(60, None))

    def test_reviewed_coalition_pair_changes_survive_regeneration(self):
        for year, name, current, expected in [(2019, 'Whitlam', 60.91, -2.81),
                                              (2025, 'Bendigo', 51.4, -9.81)]:
            repaired = apply_reviewed_corrections(year, {name: TppResult(current, None)})
            self.assertEqual(repaired[name], TppResult(current, expected))
            self.assertEqual(load_tpp_history()[(year, name)], repaired[name])
            with self.assertRaisesRegex(ValueError, 'no longer matches'):
                apply_reviewed_corrections(year, {name: TppResult(current+1, None)})

    def test_swing_sign_follows_column_order_not_leader(self):
        # The same Labor gain appears with either layout, even where Labor
        # leads the count in the Coalition-first download.
        for first_labor, raw_swing in ((True, 3), (False, -3)):
            columns = [('Australian Labor Party Percentage', '60'),
                       ('Liberal/National Coalition Percentage', '40')]
            if not first_labor:
                columns.reverse()
            text = ('AEC metadata\nDivisionNm,PartyAb,' + ','.join(c[0] for c in columns)
                    + ',Swing\nExample,ALP,' + ','.join(c[1] for c in columns)
                    + f',{raw_swing}\n')
            self.assertEqual(parse_aec_tpp(text)['Example'], TppResult(60, 3))

    def test_nonclassic_history_does_not_replace_actual_contest(self):
        pair = [SimpleNamespace(party='Greens', percent=65, swing=4, name='Winner'),
                SimpleNamespace(party='Labor', percent=35, swing=-4, name='Runner')]
        seat = SimpleNamespace(name='Example', tcp=pair)
        history = {(2022, 'Example'): TppResult(75, 6)}
        self.assertEqual(seat_tpp(ElectionCode(2022, 'fed'), seat, history), TppResult(75, 6))
        self.assertIs(seat.tcp, pair)
        self.assertEqual(seat.tcp[0].name, 'Winner')
        self.assertIsNone(seat_tpp(ElectionCode(2019, 'fed'), seat, history))
        self.assertIsNone(seat_tpp(ElectionCode(2022, 'vic'), seat, history))

    def test_classic_history_preserves_existing_swing_and_sign(self):
        seat = SimpleNamespace(name='Example', tcp=[
            SimpleNamespace(party='Liberal', percent=55, swing=2),
            SimpleNamespace(party='Labor', percent=45, swing=-2)])
        self.assertEqual(seat_tpp(ElectionCode(2022, 'fed'), seat,
                                 {(2022, 'Example'): TppResult(46, -1)}), TppResult(45, -2))
        self.assertIsNone(seat_tpp(None, seat, {}))
        self.assertIsNone(seat_tpp(ElectionCode(2022, 'fed'), None, {}))

    def test_invalid_download_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'columns'):
            parse_aec_tpp('metadata\nDivisionNm,TCP\nExample,60\n')
        with self.assertRaisesRegex(ValueError, 'sum to 100'):
            parse_aec_tpp('metadata\nDivisionNm,Australian Labor Party Percentage,'
                          'Liberal/National Coalition Percentage,Swing\nExample,60,45,3\n')


if __name__ == '__main__':
    unittest.main()

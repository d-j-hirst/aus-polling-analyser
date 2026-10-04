"""Check the AEC accounting boundary and the C++ count-rule reference."""

import tempfile
from pathlib import Path
import unittest

from lib.turnout import aec_live, ppvc_sizes
from scripts.turnout import turnout_ppvc_reporting


def booth(identifier, seat, name, counted, classification='Normal'):
    return dict(id=identifier, seat_name=seat, name=name, counted=counted,
                classification=classification, candidate_votes=[dict(candidate_id='7', value=counted)])


def election(seat, booths, declarations=0):
    return dict(seats={seat: dict(booths=list(booths), vote_types={k: declarations for k in aec_live.DECLARATIONS})},
                booths=booths)


class AecLiveTests(unittest.TestCase):
    def test_rockingham_calibration_exception_is_election_specific(self):
        for identifier in ('58768', '97689'):
            self.assertTrue(aec_live.calibration_exclusion_reason('31496', 'Brand', identifier))
            self.assertIsNone(aec_live.calibration_exclusion_reason('27966', 'Brand', identifier))
        self.assertIsNone(aec_live.calibration_exclusion_reason('31496', 'Brand', '83347'))

    def test_reporting_history_separates_feed_additions_from_unknown_venue_counts(self):
        rows = [dict(seat='A', expected=100, final_feed_formal=0,
                     venue_formal=None, status='no_final_return'),
                dict(seat='A', expected=100, final_feed_formal=50,
                     venue_formal=50, status='later_return')]
        summary = turnout_ppvc_reporting.summarise(rows)
        self.assertEqual(summary['feed_total_relative_to_expected'], .25)
        self.assertEqual(summary['known_count_relative_to_expected'], .5)
        self.assertEqual(summary['no_final_return'], 1)
        self.assertEqual(summary['known_formal_zero'], 0)
        self.assertIsNone(rows[0]['venue_formal'])

    def test_remote_city_service_does_not_receive_local_new_centre_size(self):
        # A city service for an outer-suburban electorate must not inherit the
        # large local-centre assumption merely because it is a PPVC. A mixed
        # boundary neighbourhood remains uncertain rather than forced local.
        ordinary = [dict(seat_name='City', position=[-37.80, 144.95]),
                    dict(seat_name='City', position=[-37.81, 144.95]),
                    dict(seat_name='City', position=[-37.82, 144.95]),
                    dict(seat_name='Outer', position=[-37.60, 144.90])]
        screen = ppvc_sizes.LocationScreen(ordinary)
        self.assertEqual(screen.classify('Outer', [-37.80, 144.95]), 'likely_outside')
        self.assertEqual(screen.classify('Outer', [-37.60, 144.90]), 'likely_local')
        self.assertEqual(screen.classify('Outer', None), 'unknown')
        metadata = {str(j): dict(b, classification='Normal') for j, b in enumerate(ordinary)}
        metadata['remote'] = dict(seat_name='Outer', classification='PrePollVotingCentre', position=[-37.80, 144.95])
        metadata['local'] = dict(seat_name='Outer', classification='PrePollVotingCentre', position=[-37.60, 144.90])
        units = [dict(id=k, seat_name='Outer', seat_index=0, group_index=0, kind='ppvc', matched=False)
                 for k in ['remote', 'local']]
        weights, details = ppvc_sizes.starting_weights(units, [500, 500], metadata, dict(median=7500))
        self.assertAlmostEqual(weights['remote'], 500/8000)
        self.assertAlmostEqual(weights['local'], 7500/8000)
        self.assertAlmostEqual(sum(weights.values()), 1)
        self.assertEqual(details['remote']['starting_size_source'], 'existing_count_or_fallback')
        typed, _ = ppvc_sizes.starting_weights(units, [500, 500], metadata,
            dict(median=7500, seat_types={'Outer': '3'}, by_seat_type={'3': dict(median=3760)}), 'median_by_type')
        self.assertAlmostEqual(typed['local'], 3760/4260)

    def test_shared_city_ordinary_venue_cannot_mark_remote_ppvc_local(self):
        ordinary = [dict(seat_name='Outer', position=[-37.80, 144.95]),
                    dict(seat_name='City', position=[-37.80, 144.95]),
                    dict(seat_name='City', position=[-37.81, 144.95]),
                    dict(seat_name='City', position=[-37.82, 144.95]),
                    dict(seat_name='City', position=[-37.83, 144.95]),
                    dict(seat_name='Outer', position=[-37.60, 144.90])]
        screen = ppvc_sizes.LocationScreen(ordinary)
        self.assertEqual(screen.classify('Outer', [-37.80, 144.95]), 'likely_outside')

    def test_delayed_return_rule_waits_for_monday_and_remains_positive(self):
        self.assertEqual(ppvc_sizes.reporting_decay_factor(30), 1)
        self.assertEqual(ppvc_sizes.reporting_decay_factor(48), 1)
        self.assertAlmostEqual(ppvc_sizes.reporting_decay_factor(54), .25)
        self.assertAlmostEqual(ppvc_sizes.reporting_decay_factor(102), .02)
        self.assertGreater(ppvc_sizes.reporting_decay_factor(1000), 0)

    def test_sizes_use_current_counts_ppvc_evidence_and_explicit_fallbacks(self):
        units = [dict(kind='ordinary', baseline_type='ppvc', previous=1000, counted=2000),
                 dict(kind='ordinary', baseline_type='ppvc', previous=1000, counted=0),
                 dict(kind='ordinary', baseline_type='hospital', previous=None, counted=0),
                 dict(kind='ordinary', baseline_type='ordinary', previous=None, counted=0),
                 dict(kind='declaration', baseline_type='declaration', previous=None, counted=0),
                 dict(kind='declaration', baseline_type='declaration', previous=100, counted=0),
                 dict(kind='declaration', baseline_type='declaration', previous=4000, counted=4500)]
        counts, evidence = aec_live.baseline_counts(units)
        self.assertEqual(counts[0], 2000)
        self.assertAlmostEqual(evidence['multiplier'], 4/3, places=6)
        self.assertAlmostEqual(counts[1], 4000/3, places=3)
        self.assertEqual(counts[2:], [50, 500, 31, 104, 4500])
        self.assertIsNone(units[2]['previous'])

    def test_reused_ppvc_id_rejected_and_moved_booth_not_adaptation_evidence(self):
        current = election('New', {'1': booth('1', 'New', 'Brunswick PPVC', 500, 'PrePollVotingCentre'),
                                   '2': booth('2', 'New', 'Same booth', 300)})
        previous = election('Old', {'1': booth('1', 'Old', 'Pascoe Vale PPVC', 700, 'PrePollVotingCentre'),
                                    '2': booth('2', 'Old', 'Same booth', 400)}, 50)
        categories = ['ordinary', 'early_ordinary', *aec_live.DECLARATIONS.values()]
        units = aec_live.make_units(current, previous, categories, ['New'])
        self.assertIsNone(units[0]['previous'])
        self.assertEqual(units[1]['previous'], 400)
        self.assertFalse(units[1]['matched'])
        self.assertIsNone(units[2]['previous'])
        # Borrowing FP declaration results does not make ordinary booths a
        # same-district match. A previousName setting does establish that match.
        borrowed = aec_live.make_units(current, previous, categories, ['New'], {'New': {'useFpResults': 'Old'}})
        self.assertEqual(borrowed[2]['previous'], 50)
        self.assertFalse(borrowed[1]['matched'])
        renamed = aec_live.make_units(current, previous, categories, ['New'], {'New': {'previousName': 'Old'}})
        self.assertTrue(renamed[1]['matched'])

    def test_changed_type_not_adaptation_evidence_and_office_not_ppvc_size(self):
        current = election('A', {'1': booth('1', 'A', 'Same name', 100, 'PrePollVotingCentre'),
                                 '2': booth('2', 'A', 'A Divisional Office', 0, 'PrePollVotingCentre')})
        previous = election('A', {'1': booth('1', 'A', 'Same name', 200),
                                  '2': booth('2', 'A', 'A Divisional Office', 100, 'PrePollVotingCentre')})
        categories = ['ordinary', 'early_ordinary', *aec_live.DECLARATIONS.values()]
        units = aec_live.make_units(current, previous, categories, ['A'])
        self.assertEqual(units[0]['previous'], 200)
        self.assertFalse(units[0]['matched'])
        self.assertEqual(units[1]['baseline_type'], 'other')
        self.assertEqual(units[1]['group'], 'early_ordinary')
        self.assertEqual(aec_live.baseline_counts(units)[0][1], 100)

    def test_actual_candidates_reconcile_without_ghosts_and_missing_is_not_zero(self):
        # This minimal House feed places a historical record beside the actual
        # candidate. It must not enter current ordinary or declaration totals.
        xml = '''<MediaFeed xmlns="http://www.aec.gov.au/xml/schema/mediafeed"
          xmlns:e="urn:oasis:names:tc:evs:schema:eml" Created="2025-05-03T20:00:00">
          <Results><e:EventIdentifier Id="31496"/><Election><e:ElectionIdentifier Id="H"/>
          <House><Contests><Contest><e:ContestIdentifier Id="1"><e:ContestName>A</e:ContestName></e:ContestIdentifier>
          <Enrolment>1000</Enrolment><FirstPreferences><Candidate><VotesByType>
          <Votes Type="Ordinary">200</Votes><Votes Type="Absent">0</Votes><Votes Type="PrePoll">0</Votes>
          <Votes Type="Provisional">0</Votes><Votes Type="Postal">0</Votes></VotesByType></Candidate>
          <Ghost><VotesByType><Votes Type="Ordinary">999</Votes></VotesByType></Ghost>
          <Formal><Votes>200</Votes></Formal></FirstPreferences><PollingPlaces><PollingPlace>
          <PollingPlaceIdentifier Id="1" Name="One"/><FirstPreferences><Candidate><e:CandidateIdentifier Id="7"/>
          <Votes>200</Votes></Candidate><Formal><Votes>200</Votes></Formal></FirstPreferences>
          </PollingPlace></PollingPlaces></Contest></Contests></House></Election></Results></MediaFeed>'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'feed.xml'
            path.write_text(xml)
            parsed = aec_live.read_house(path, event_id='31496')
            self.assertEqual(parsed['seats']['A']['counted'], 200)
            self.assertEqual(parsed['seats']['A']['vote_types']['Absent'], 0)
            path.write_text(xml.replace('<Votes>200</Votes></Candidate>', '<Votes>199</Votes></Candidate>'))
            with self.assertRaisesRegex(ValueError, 'formal booth'):
                aec_live.read_house(path)
        with self.assertRaises(ValueError):
            aec_live.integer(None)
        self.assertEqual(aec_live.integer('0'), 0)


if __name__ == '__main__':
    unittest.main()

"""Exercise VEC delivery, run separation and restart behaviour with fictional counts."""

from contextlib import redirect_stdout
from io import BytesIO, StringIO
import importlib.util
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
from zipfile import ZipFile


MODULE = Path(__file__).resolve().parents[2] / 'live_scripts/vec_2026_feed.py'
spec = importlib.util.spec_from_file_location('vec_2026_feed', MODULE)
vec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vec)


def capture(stamp, event=vec.EVENT, created=None, prefix='State', votes=(0, 0)):
    name = f'{prefix}2026mediafilelitepplhv2_{stamp}.zip'
    created = created or vec.datetime.strptime(stamp, '%Y%m%d_%H%M%S').isoformat()
    candidates = ''.join(f'<Candidate><CandidateIdentifier Id="{i}"/><Votes>{n}</Votes></Candidate>'
                         for i, n in enumerate(votes, 1))
    xml = (f'<MediaFeed Created="{created}"><EventIdentifier Id="{event}"/><Election><ElectionIdentifier>'
           '<ElectionCategory>LowerHouse</ElectionCategory></ElectionIdentifier><House><Contests>'
           '<Contest><PollingDistrictIdentifier Id="123"/><FirstPreferences>' + candidates + '</FirstPreferences></Contest>'
           '</Contests></House></Election></MediaFeed>')
    data = BytesIO()
    with ZipFile(data, 'w') as archive:
        archive.writestr(name[:-4] + '.xml', xml)
    return name, data.getvalue()


class FakeFeed(dict):
    def listing(self):
        return {name: (len(data), str(len(data))) for name, data in self.items()}


class VecDeliveryTests(unittest.TestCase):
    def test_official_opening_uses_publication_time_and_excludes_trial_and_later_stale_xml(self):
        before = capture('20261128_175959', votes=(11, 8))
        opening = capture('20261128_180002', created='2026-11-28T06:59:57Z', votes=(18, 12))
        stale = capture('20261128_180501', created='2026-11-28T17:59:59')
        renamed_trial = capture('20261128_180004', created='2026-11-25T19:59:58')
        later = capture('20261203_130002', votes=(6501, 5123))
        feed = FakeFeed([before, opening, stale, renamed_trial, later])
        with TemporaryDirectory() as temp, redirect_stdout(StringIO()):
            folder = Path(temp)
            self.assertEqual(vec.collect(feed, feed.listing(), 'election', folder, vec.ELECTION_RESULTS), 0)
            with self.assertRaisesRegex(vec.FeedUnavailable, 'outside the official count'):
                vec.collect(feed, feed.listing(), 'election', folder)
            self.assertEqual({p.name: p.read_bytes() for p in (folder/'inbox').iterdir()}, dict([opening, later]))
        with self.assertRaisesRegex(ValueError, 'outside the official count'):
            vec.results_xml(*before, 'election')

    def test_conflicting_reissue_is_preserved_outside_the_inbox_and_later_counts_arrive(self):
        original = capture('20261128_191001', votes=(52, 31))
        replacement = capture('20261128_191001', votes=(60, 308))
        later = capture('20261128_191501', votes=(92, 317))
        with TemporaryDirectory() as temp, redirect_stdout(StringIO()):
            folder = Path(temp)
            feed = FakeFeed([original])
            vec.collect(feed, feed.listing(), 'election', folder)
            feed.update([replacement, later])
            with self.assertRaisesRegex(vec.FeedUnavailable, 'existing timestamp'):
                vec.collect(feed, feed.listing(), 'election', folder)
            self.assertEqual({p.name: p.read_bytes() for p in (folder/'inbox').iterdir()}, dict([original, later]))
            self.assertEqual([p.read_bytes() for p in (folder/'vec-conflicts').glob('*/*.zip')], [replacement[1]])

    def test_trial_opens_with_zeros_and_retains_unusual_results_in_its_own_window(self):
        zero = capture('20261125_173001', created='2026-11-25T17:29:58')
        premature_votes = capture('20261125_174500', votes=(12, 12))
        tied = capture('20261125_180001', created='2026-11-25T17:59:57', votes=(11111, 11111))
        final = capture('20261125_200041', votes=(99999, 0))
        feed = FakeFeed([capture('20261125_170000'), zero, premature_votes, tied, final,
                         capture('20261125_203001'), capture('20261128_180005')])
        with TemporaryDirectory() as temp, redirect_stdout(StringIO()):
            folder = Path(temp)
            self.assertEqual(vec.collect(feed, feed.listing(), 'rehearsal', folder, vec.TRIAL_ZEROS), 0)
            with self.assertRaisesRegex(vec.FeedUnavailable, 'before the trial count opens'):
                vec.collect(feed, feed.listing(), 'rehearsal', folder)
            self.assertEqual({p.name: p.read_bytes() for p in (folder/'inbox').iterdir()}, dict([zero, tied, final]))
            del feed[premature_votes[0]]
            self.assertEqual(vec.collect(feed, feed.listing(), 'rehearsal', folder), 0)

    def test_setup_can_use_a_reissued_zero_after_the_original_is_withdrawn(self):
        zero = capture('20261125_173500')
        feed = FakeFeed([zero, capture('20261125_160000'), capture('20261125_180500', votes=(2, 2))])
        with TemporaryDirectory() as temp:
            cache = Path(temp)
            name, data, _ = vec.matching_zero(feed, feed.listing(), cache, {'123': {'1', '2'}})
            self.assertEqual((name, data), zero)
            (cache/name).write_bytes(data)
            self.assertEqual(vec.matching_zero({}, {}, cache, {'123': {'1', '2'}})[:2], zero)
            with self.assertRaisesRegex(ValueError, 'candidate roster'):
                vec.matching_zero({}, {}, cache, {'123': {'3'}})
            counted = capture('20261125_174001', votes=(8, 8))
            with self.assertRaisesRegex(ValueError, 'zero count'):
                vec.results_xml(*counted, 'prepare')

    def test_thursday_reset_prefers_the_published_roster_even_if_a_cached_zero_is_newer(self):
        published = capture('20261126_120001')
        counted = capture('20261126_125901', votes=(91, 111))
        cache_only = capture('20261127_093002')
        feed = FakeFeed([published, counted])
        with TemporaryDirectory() as temp:
            cache = Path(temp)
            (cache/cache_only[0]).write_bytes(cache_only[1])
            self.assertEqual(vec.matching_zero(feed, feed.listing(), cache, {'123': {'1', '2'}})[:2], published)
        missing = vec.ET.fromstring('<ContestList><Contest><FirstPreferences><Candidate/>'
                                    '</FirstPreferences></Contest></ContestList>')
        self.assertFalse(vec.is_zero_count(missing))

    def test_refresh_sets_official_rules_in_both_files_and_preserves_operator_edits(self):
        # Construct the header, live simulation settings and watcher trailer.
        project_spec = importlib.util.spec_from_file_location('vec_projects', MODULE.with_name('prepare_vec_2026_runs.py'))
        projects = importlib.util.module_from_spec(project_spec)
        with patch.dict(sys.modules, vec_2026_feed=vec):
            project_spec.loader.exec_module(projects)
        enc = projects.encoded
        source = (projects.struct.pack('<i', 71) + enc('Victorian project') + enc('2026 Victorian State Election')
                  + enc('original macro') + enc('Live Simulation') + projects.struct.pack('<iifi', 10000, 0, 54, 1)
                  + enc('2022vic') + projects.struct.pack('<ii', 2, 1) + enc('null'))
        for kind, trial, draws in [('live', False, 50000), ('replay', True, 50000), ('replay', False, 10000)]:
            copied = projects.project_copy(source, dict(display_name='Victoria', kind=kind, local_trial=trial))
            self.assertIn(enc('Live Simulation') + projects.struct.pack('<i', draws), copied)
        with TemporaryDirectory() as temp, patch.object(projects, 'ROOT', Path(temp)), redirect_stdout(StringIO()):
            folder = Path(temp)/'live_runs'/vec.RUN_IDS['election']
            project = folder/(vec.RUN_IDS['election']+'.pol2')
            manifest = folder/'run.json'
            projects.configure('election', source)
            expected = json.loads(manifest.read_bytes())
            self.assertEqual(expected['commentary'], {'enabled': False, 'rules_only': True})
            self.assertEqual(project.read_bytes(), projects.project_copy(source, expected))
            old = dict(expected)
            del old['commentary']
            manifest.write_text(json.dumps(old))
            projects.configure('election', source)
            self.assertEqual(json.loads(manifest.read_bytes()), expected)
            manual = dict(expected, commentary={'enabled': False})
            manifest.write_text(json.dumps(manual))
            projects.configure('election', source)
            self.assertEqual(project.read_bytes(), projects.project_copy(source, manual))
            project.write_bytes(b'operator edits')
            projects.configure('election', source)
            self.assertEqual(project.read_bytes(), b'operator edits')
            self.assertEqual(json.loads(manifest.read_bytes()), manual)

    def test_genuine_periods_separate_and_downloads_resume(self):
        rehearsal = capture('20261125_180505', created='2026-11-25T07:05:01Z')
        election = capture('20261128_180505')
        overnight = capture('20261129_000505')
        samples = capture('20261128_183000', prefix='TestState')
        feed = FakeFeed([rehearsal, election, overnight, samples, capture('20261125_165959'),
                         capture('20261128_165959'), capture('20261110_100505')])
        with TemporaryDirectory() as temp, redirect_stdout(StringIO()):
            for mode, expected in [('rehearsal', [rehearsal]), ('election', [election, overnight])]:
                folder = Path(temp) / mode
                self.assertEqual(vec.collect(feed, feed.listing(), mode, folder), len(expected))
                self.assertEqual(vec.collect(feed, feed.listing(), mode, folder), 0)
                self.assertEqual({p.name: p.read_bytes() for p in (folder / 'inbox').iterdir()}, dict(expected))

    def test_wrong_identity_or_clock_and_corrupt_zip_do_not_block_a_later_delivery(self):
        wrong_event = capture('20261128_180500', event='123')
        wrong_clock = capture('20261128_181000', created='2026-11-25T18:10:00')
        damaged = ('State2026mediafilelitepplhv2_20261128_181500.zip', b'incomplete')
        good = capture('20261128_182000')
        feed = FakeFeed([wrong_event, wrong_clock, damaged, good])
        with TemporaryDirectory() as temp, redirect_stdout(StringIO()):
            folder = Path(temp)
            with self.assertRaises(vec.FeedUnavailable):
                vec.collect(feed, feed.listing(), 'election', folder)
            self.assertEqual([p.name for p in (folder/'inbox').iterdir()], [good[0]])

    def test_container_listing_follows_continuation_markers(self):
        pages = []
        class Paged(vec.Container):
            def get(self, **params):
                pages.append(params['marker'])
                marker, name = ('another+page', 'first') if len(pages) == 1 else ('', 'second')
                return (f'<EnumerationResults><Blobs><Blob><Name>{name}</Name><Properties>'
                        '<Content-Length>8</Content-Length><Etag>v1</Etag></Properties></Blob></Blobs>'
                        f'<NextMarker>{marker}</NextMarker></EnumerationResults>').encode()
        self.assertEqual(set(Paged(vec.CONTAINER + '?example=1').listing()), {'first', 'second'})
        self.assertEqual(pages, ['', 'another+page'])

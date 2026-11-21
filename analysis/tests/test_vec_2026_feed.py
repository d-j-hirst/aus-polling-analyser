"""Exercise VEC delivery, run separation and restart behaviour with fictional counts."""

from contextlib import redirect_stdout
from io import BytesIO, StringIO
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from zipfile import ZipFile


MODULE = Path(__file__).resolve().parents[2] / 'live_scripts/vec_2026_feed.py'
spec = importlib.util.spec_from_file_location('vec_2026_feed', MODULE)
vec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vec)


def capture(stamp, event=vec.EVENT, created=None, prefix='State'):
    name = f'{prefix}2026mediafilelitepplhv2_{stamp}.zip'
    created = created or vec.datetime.strptime(stamp, '%Y%m%d_%H%M%S').isoformat()
    xml = (f'<MediaFeed Created="{created}"><EventIdentifier Id="{event}"/><Election><ElectionIdentifier>'
           '<ElectionCategory>LowerHouse</ElectionCategory></ElectionIdentifier><House><Contests><Contest/>'
           '</Contests></House></Election></MediaFeed>')
    data = BytesIO()
    with ZipFile(data, 'w') as archive:
        archive.writestr(name[:-4] + '.xml', xml)
    return name, data.getvalue()


class FakeFeed(dict):
    def listing(self):
        return {name: (len(data), str(len(data))) for name, data in self.items()}


class VecDeliveryTests(unittest.TestCase):
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

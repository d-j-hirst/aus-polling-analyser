import pickle
import io
from pathlib import Path
import runpy
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import election_data
from lib.shared.election_code import ElectionCode


class ElectionDataTests(unittest.TestCase):
    def test_legacy_election_code_cache_remains_readable(self):
        # Recreate the old protocol-zero module identity while retaining a real
        # object's state. Legacy caches must preserve dictionary-key equality
        # and hashing without a compatibility module at the analysis root.
        original = ElectionCode(2028, 'fed')
        current = pickle.dumps(original, protocol=0)
        legacy = current.replace(
            b'clib.shared.election_code\nElectionCode\n',
            b'celection_code\nElectionCode\n',
        )
        self.assertNotEqual(current, legacy)
        restored = election_data._ElectionCacheUnpickler(
            io.BytesIO(legacy)
        ).load()
        self.assertIsInstance(restored, ElectionCode)
        self.assertEqual(restored, original)
        self.assertEqual(hash(restored), hash(original))

    def test_download_checks_http_status_and_sets_timeout(self):
        response = mock.Mock(content=b'election page')
        requests = SimpleNamespace(
            get=mock.Mock(return_value=response)
        )
        with mock.patch.dict(sys.modules, {'requests': requests}):
            content = election_data._download_page(
                'https://example.test/results',
                {'User-Agent': 'test'},
            )

        self.assertEqual(content, "b'election page'")
        requests.get.assert_called_once_with(
            'https://example.test/results',
            headers={'User-Agent': 'test'},
            timeout=election_data.REQUEST_TIMEOUT_SECONDS,
        )
        response.raise_for_status.assert_called_once_with()

    def test_direct_entry_point_uses_importable_class_identity(self):
        with mock.patch.object(election_data, 'AllElections') as collection:
            runpy.run_path(
                Path(election_data.__file__),
                run_name='__main__',
            )

        collection.assert_called_once_with()

    def test_atomic_pickle_writer_publishes_readable_cache(self):
        value = election_data.SavedResults()
        with tempfile.TemporaryDirectory() as temporary_directory:
            filename = Path(temporary_directory) / 'results.pkl'
            election_data._write_pickle_atomically(filename, value)

            with open(filename, 'rb') as cache_file:
                restored = pickle.load(cache_file)

            self.assertIsInstance(restored, election_data.SavedResults)
            self.assertFalse(
                filename.with_name(filename.name + '.tmp').exists()
            )


if __name__ == '__main__':
    unittest.main()

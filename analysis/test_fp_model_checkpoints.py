import tempfile
import unittest
from pathlib import Path

import fp_model_checkpoints


class CalibrationCheckpointStoreTests(unittest.TestCase):
    def identity(self, fingerprint="inputs-a"):
        return {
            "election": "2025fed",
            "excluded_pollster": "Example Polling",
            "mode": "calibration",
            "base_seed": 20260803,
            "seed_namespace": "fp-model-v1",
            "parties": ["@TPP", "ALP FP"],
            "party_seeds": {"@TPP": 11, "ALP FP": 12},
            "source_fingerprint": fingerprint,
        }

    def test_complete_block_round_trips(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            store = fp_model_checkpoints.CalibrationCheckpointStore(
                temporary_directory
            )
            identity = self.identity()
            path = store.write(
                identity,
                [
                    {
                        "day_index": 3,
                        "party": "@TPP",
                        "poll_index": 7,
                        "values": [51.0, 50.0, 50.5, None, 0.5, None, 2.0],
                    }
                ],
                {"@TPP": [["2025-01-01", 50.0]]},
                [{"party": "@TPP", "seed": 11}],
            )

            self.assertTrue(path.is_file())
            payload = store.load(identity)
            self.assertEqual(payload["identity"], identity)
            self.assertIsNone(
                payload["poll_calibrations"][0]["values"][3]
            )
            self.assertEqual(
                payload["stan_seeds"],
                [{"party": "@TPP", "seed": 11}],
            )

    def test_changed_identity_invalidates_block(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            store = fp_model_checkpoints.CalibrationCheckpointStore(
                temporary_directory
            )
            store.write(self.identity(), [], {})
            self.assertIsNone(store.load(self.identity("inputs-b")))

    def test_corrupt_block_is_not_loaded(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            store = fp_model_checkpoints.CalibrationCheckpointStore(
                temporary_directory
            )
            path = store.path("2025fed", "Example Polling")
            path.parent.mkdir(parents=True)
            path.write_text("{broken", encoding="utf-8")
            self.assertIsNone(store.load(self.identity()))

    def test_clear_election_removes_only_restart_directory(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            store = fp_model_checkpoints.CalibrationCheckpointStore(root)
            store.write(self.identity(), [], {})
            permanent = root / "permanent-calibration.csv"
            permanent.write_text("kept", encoding="utf-8")
            try:
                store.clear_election("2025fed")
                self.assertFalse((root / "2025fed").exists())
                self.assertEqual(permanent.read_text(encoding="utf-8"), "kept")
            finally:
                permanent.unlink()


class CheckpointFingerprintTests(unittest.TestCase):
    def test_file_contents_change_fingerprint(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "input.csv"
            path.write_text("one", encoding="utf-8")
            first = fp_model_checkpoints.fingerprint_files([path])
            path.write_text("two", encoding="utf-8")
            second = fp_model_checkpoints.fingerprint_files([path])
            self.assertNotEqual(first, second)


if __name__ == "__main__":
    unittest.main()

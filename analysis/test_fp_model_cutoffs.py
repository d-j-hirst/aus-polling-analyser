import csv
import tempfile
import unittest
from datetime import date
from pathlib import Path
from unittest import mock

import fp_model_provenance


class CutoffScheduleTests(unittest.TestCase):
    def test_schedule_matches_trend_adjust_triangular_days(self):
        expected = [
            n * (n + 1) // 2
            for n in range(46)
        ]
        self.assertEqual(
            fp_model_provenance.cutoff_schedule(),
            expected,
        )
        self.assertEqual(expected[:5], [0, 1, 3, 6, 10])
        self.assertEqual(expected[-1], 1035)

    def test_effective_schedule_keeps_both_scheduled_and_poll_end_days(self):
        self.assertEqual(
            fp_model_provenance.effective_cutoff_schedule(
                election_day=date(2026, 3, 21),
                poll_dates=[
                    date(2026, 3, 10),
                    date(2026, 3, 15),
                    date(2026, 3, 18),
                ],
                schedule=[0, 1, 3, 6, 10],
            ),
            [(10, 11), (6, 6), (3, 3)],
        )


class CutoffOutputStoreTests(unittest.TestCase):
    def test_rows_are_upserted_atomically(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                store.write(
                    election="2025fed",
                    party="ALP FP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=456,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(30.1, 32.2, 34.3),
                )
                self.assertFalse(store.is_complete("2025fed", 28, 30))
                store.mark_complete("2025fed", 28, 30)

                restored = fp_model_provenance.CutoffOutputStore()
                self.assertTrue(
                    restored.contains("2025fed", "@TPP", 28, 30)
                )
                self.assertTrue(
                    restored.contains("2025fed", "ALP FP", 28, 30)
                )
                self.assertTrue(
                    restored.is_complete("2025fed", 28, 30)
                )

                path = fp_model_provenance.cutoff_output_path("2025fed")
                with path.open(newline="", encoding="utf-8") as source:
                    rows = list(csv.reader(source))

            self.assertEqual(
                rows[0],
                [
                    "ScheduledCutoffDays",
                    "PollTrendEndDays",
                    "Party",
                    "StanSeed",
                    "0%",
                    "50%",
                    "100%",
                ],
            )
            self.assertEqual(len(rows), 4)
            self.assertFalse(path.with_suffix(".csv.tmp").exists())

    def test_reset_removes_existing_rows_and_file(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                path = fp_model_provenance.cutoff_output_path("2025fed")
                self.assertTrue(path.exists())

                store.reset("2025fed")

                self.assertFalse(path.exists())
                self.assertFalse(store.contains("2025fed", "@TPP", 28, 30))


if __name__ == "__main__":
    unittest.main()

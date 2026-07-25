import csv
import math
import tempfile
import unittest
from pathlib import Path

from trend_adjust_cutoffs import (
    CutoffTrendData,
    CutoffTrendError,
    KEY_COLUMNS,
    PERCENTILE_COLUMNS,
    triangular_root,
)


class CutoffTrendTests(unittest.TestCase):
    def _write(self, rows):
        temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_directory.cleanup)
        path = Path(temporary_directory.name) / "cutoffs_2028fed.csv"
        with path.open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(KEY_COLUMNS + PERCENTILE_COLUMNS)
            for scheduled, endpoint, party, value in rows:
                writer.writerow(
                    [scheduled, endpoint, party, ""]
                    + [value] * 101
                )
        return path

    def test_triangular_root_recovers_triangular_indexes(self):
        self.assertAlmostEqual(triangular_root(15), 5)
        self.assertAlmostEqual(triangular_root(21), 6)
        self.assertAlmostEqual(triangular_root(28), 7)

    def test_interpolation_uses_actual_endpoint_in_triangular_space(self):
        data = CutoffTrendData(
            self._write(
                [
                    (21, 15, "@TPP", 40),
                    (28, 28, "@TPP", 60),
                ]
            )
        )

        self.assertEqual(data.value_at("@TPP", 15, 50), 40)
        self.assertAlmostEqual(data.value_at("@TPP", 21, 50), 50)

    def test_wider_symmetric_triangular_indexes_are_equal_weighted(self):
        data = CutoffTrendData(
            self._write(
                [
                    (10, 10, "@TPP", 20),
                    (36, 36, "@TPP", 80),
                ]
            )
        )

        self.assertAlmostEqual(data.value_at("@TPP", 21, 50), 50)

    def test_latest_value_is_retained_but_pre_poll_dates_are_absent(self):
        data = CutoffTrendData(
            self._write(
                [
                    (15, 15, "@TPP", 40),
                    (28, 28, "@TPP", 60),
                ]
            )
        )

        self.assertEqual(data.value_at("@TPP", 0, 50), 40)
        self.assertIsNone(data.value_at("@TPP", 36, 50))
        self.assertEqual(
            data.value_at("@TPP", 36, 50, default_value=-1), -1
        )

    def test_duplicate_party_actual_endpoint_is_rejected(self):
        path = self._write(
            [
                (15, 15, "@TPP", 40),
                (21, 15, "@TPP", 50),
            ]
        )

        with self.assertRaisesRegex(
            CutoffTrendError, "duplicates party @TPP"
        ):
            CutoffTrendData(path)

    def test_non_finite_percentile_is_rejected(self):
        path = self._write([(15, 15, "@TPP", math.nan)])

        with self.assertRaisesRegex(
            CutoffTrendError, "non-finite 0% value"
        ):
            CutoffTrendData(path)

    def test_completion_markers_are_ignored(self):
        path = self._write([(15, 15, "@TPP", 50)])
        with path.open("a", newline="", encoding="utf-8") as output:
            csv.writer(output).writerow(
                [15, 15, "#COMPLETE", ""] + [""] * 101
            )

        data = CutoffTrendData(path)

        self.assertEqual(data.value_at("@TPP", 15, 50), 50)


if __name__ == "__main__":
    unittest.main()

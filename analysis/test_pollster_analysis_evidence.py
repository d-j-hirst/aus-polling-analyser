import csv
import tempfile
import unittest
from pathlib import Path

import calibration_summary
from pollster_analysis_evidence import load_calibration_evidence


class PollsterAnalysisEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write_compact_summary(self, election, rows):
        directory = self.directory / "Summaries"
        directory.mkdir(exist_ok=True)
        path = directory / "{}.csv".format(election)
        with path.open("w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(
                output, fieldnames=calibration_summary.SUMMARY_FIELDS
            )
            writer.writeheader()
            for values in rows:
                row = {field: "" for field in calibration_summary.SUMMARY_FIELDS}
                row.update(
                    {
                        "schema_version": calibration_summary.SCHEMA_VERSION,
                        "election": election,
                    }
                )
                row.update(values)
                writer.writerow(row)
        return path

    def test_compact_summary_supersedes_legacy_files_for_its_election(self):
        compact = self.write_compact_summary(
            "2028fed",
            [
                {
                    "record_type": "leave_one_out",
                    "party": "@TPP",
                    "pollster": "Compact",
                    "weighted_abs_error": "1.5",
                    "error_weight": "2.5",
                },
                {
                    "record_type": "bias_trend",
                    "party": "@TPP",
                    "final_trend_median": "51.5",
                },
                {
                    "record_type": "bias_pollster",
                    "party": "@TPP",
                    "pollster": "Compact",
                    "new_house_effect_median": "0.2",
                    "recent_poll_count": "2",
                },
            ],
        )
        legacy = self.directory / "calib_2028fed_Legacy_@TPP.csv"
        legacy.write_text("9.5,8.5,\n", encoding="utf-8")

        evidence = load_calibration_evidence([compact, legacy])

        self.assertEqual(len(evidence.leave_one_out), 1)
        record = evidence.leave_one_out[0]
        self.assertEqual(record.pollster, "Compact")
        self.assertEqual(record.weighted_abs_error, 1.5)

    def test_legacy_files_are_loaded_when_no_compact_unit_exists(self):
        legacy = self.directory / "calib_2025wa_Legacy_@TPP.csv"
        legacy.write_text("1.5,2.5,\n", encoding="utf-8")

        evidence = load_calibration_evidence([legacy])

        self.assertEqual(len(evidence.leave_one_out), 1)
        self.assertEqual(evidence.leave_one_out[0].election.short(), "2025wa")

    def test_compact_summary_without_bias_bundle_is_rejected(self):
        compact = self.write_compact_summary(
            "2028fed",
            [
                {
                    "record_type": "leave_one_out",
                    "party": "@TPP",
                    "pollster": "Compact",
                    "weighted_abs_error": "1.5",
                    "error_weight": "2.5",
                },
            ],
        )

        with self.assertRaisesRegex(
            ValueError, "no complete bias evidence"
        ):
            load_calibration_evidence([compact])

    def test_incomplete_compact_summary_falls_back_to_legacy_evidence(self):
        compact = self.write_compact_summary(
            "2028fed",
            [
                {
                    "record_type": "bias_trend",
                    "party": "@TPP",
                    "final_trend_median": "51.5",
                },
            ],
        )
        legacy = self.directory / "calib_2028fed_Legacy_@TPP.csv"
        legacy.write_text("1.5,2.5,\n", encoding="utf-8")

        evidence = load_calibration_evidence([compact, legacy])

        self.assertEqual(len(evidence.leave_one_out), 1)
        self.assertEqual(evidence.leave_one_out[0].pollster, "Legacy")

    def test_incomplete_compact_summary_without_legacy_fallback_is_rejected(self):
        compact = self.write_compact_summary(
            "2028fed",
            [
                {
                    "record_type": "bias_trend",
                    "party": "@TPP",
                    "final_trend_median": "51.5",
                },
            ],
        )

        with self.assertRaisesRegex(
            ValueError, "incomplete bias evidence"
        ):
            load_calibration_evidence([compact])

    def test_zero_recent_poll_counts_preserve_legacy_omission(self):
        compact = self.write_compact_summary(
            "2028fed",
            [
                {
                    "record_type": "bias_trend",
                    "party": "@TPP",
                    "final_trend_median": "51.5",
                },
                {
                    "record_type": "bias_pollster",
                    "party": "@TPP",
                    "pollster": "Recent",
                    "new_house_effect_median": "0.1",
                    "recent_poll_count": "2",
                },
                {
                    "record_type": "bias_pollster",
                    "party": "@TPP",
                    "pollster": "Old",
                    "new_house_effect_median": "0.2",
                    "recent_poll_count": "0",
                },
            ],
        )

        evidence = load_calibration_evidence([compact])
        counts = evidence.recent_poll_counts()

        election = evidence.bias[0].election
        self.assertEqual(counts[(election, "Recent", "@TPP")], 2)
        self.assertNotIn((election, "Old", "@TPP"), counts)
        self.assertEqual(counts[(election, "all", "@TPP")], 2)


if __name__ == "__main__":
    unittest.main()

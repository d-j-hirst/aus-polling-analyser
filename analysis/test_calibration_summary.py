import csv
import tempfile
import unittest
from pathlib import Path

import calibration_summary


class CalibrationSummaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name) / "Calibration"
        self.directory.mkdir()

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write(self, name, contents):
        path = self.directory / name
        path.write_text(contents, encoding="utf-8")
        return path

    def write_complete_bias_bundle(self, election="2028fed", party="@TPP"):
        self.write(
            "fp_trend_{}_{}_biascal.csv".format(election, party),
            "Start date day,Month,Year\n"
            "01,01,2025\n"
            "Day,Party,0%,50%,100%\n"
            "0,{0},40,50,60\n"
            "185,{0},41,51.25,61\n".format(party),
        )
        self.write(
            "fp_house_effects_{}_{}_biascal.csv".format(election, party),
            "House,Party,0%,50%,100%\n"
            "New house effects\n"
            "F2F Morgan,{0},-2,-0.25,2\n"
            "DemosAU,{0},-2,0.5,2\n"
            "Old house effects\n".format(party),
        )
        reported = ",{} reported".format(party) if party == "@TPP" else ""
        reported_values = ",50" if party == "@TPP" else ""
        self.write(
            "fp_polls_{}_{}_biascal.csv".format(election, party),
            "Firm,Day,{0},{0} adj{1}\n"
            "F2F Morgan,1,48,48.1{2}\n"
            "DemosAU,2,49,49.1{2}\n"
            "DemosAU,185,50,50.1{2}\n".format(
                party, reported, reported_values
            ),
        )

    def test_compacts_complete_legacy_bundle_with_recent_poll_counts(self):
        self.write(
            "calib_2028fed_F2F Morgan_@TPP.csv",
            "1.5,2.5,\n0,0.1,0.2,1\n",
        )
        self.write_complete_bias_bundle()

        result = calibration_summary.compact(self.directory, ["2028-fed"])

        self.assertEqual(result, [("2028fed", 4)])
        summary = calibration_summary.summary_path(self.directory, "2028fed")
        with summary.open(newline="", encoding="utf-8") as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(
            [row["record_type"] for row in rows],
            ["leave_one_out", "bias_trend", "bias_pollster", "bias_pollster"],
        )
        self.assertEqual(rows[0]["pollster"], "F2F Morgan")
        self.assertEqual(rows[0]["weighted_abs_error"], "1.5")
        self.assertEqual(rows[1]["final_trend_median"], "51.25")
        by_pollster = {row["pollster"]: row for row in rows[2:]}
        self.assertEqual(by_pollster["DemosAU"]["recent_poll_count"], "2")
        self.assertEqual(by_pollster["F2F Morgan"]["recent_poll_count"], "0")

    def test_dry_run_does_not_create_a_summary(self):
        self.write("calib_2028fed_DemosAU_@TPP.csv", "1.5,2.5,\n")

        result = calibration_summary.compact(
            self.directory, ["2028fed"], dry_run=True
        )

        self.assertEqual(result, [("2028fed", 1)])
        self.assertFalse(
            calibration_summary.summary_path(self.directory, "2028fed").exists()
        )

    def test_invalid_component_does_not_replace_existing_summary(self):
        self.write_complete_bias_bundle()
        summary = calibration_summary.summary_path(self.directory, "2028fed")
        summary.parent.mkdir()
        summary.write_text("existing summary\n", encoding="utf-8")
        self.write(
            "calib_2028fed_DemosAU_@TPP.csv", "not-a-number,2.5,\n"
        )

        with self.assertRaisesRegex(
            calibration_summary.CalibrationSummaryError, "not numeric"
        ):
            calibration_summary.compact(self.directory, ["2028fed"])

        self.assertEqual(summary.read_text(encoding="utf-8"), "existing summary\n")
        self.assertFalse(list(summary.parent.glob("*.tmp")))

    def test_rejects_mismatched_house_effect_and_pollster_keys(self):
        self.write_complete_bias_bundle()
        house_path = self.directory / "fp_house_effects_2028fed_@TPP_biascal.csv"
        house_path.write_text(
            house_path.read_text(encoding="utf-8").replace(
                "DemosAU,@TPP", "Resolve,@TPP"
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(
            calibration_summary.CalibrationSummaryError,
            "mismatched pollster keys",
        ):
            calibration_summary.compact(self.directory, ["2028fed"])

    def test_all_discovers_multiple_elections(self):
        self.write("calib_2028fed_DemosAU_@TPP.csv", "1.5,2.5,\n")
        self.write("calib_2026vic_Resolve_@TPP.csv", "1.0,3.0,\n")

        result = calibration_summary.compact(self.directory, "all")

        self.assertEqual(result, [("2026vic", 1), ("2028fed", 1)])
        self.assertTrue(
            calibration_summary.summary_path(self.directory, "2026vic").exists()
        )
        self.assertTrue(
            calibration_summary.summary_path(self.directory, "2028fed").exists()
        )

    def test_targeted_run_ignores_malformed_unrelated_legacy_file(self):
        self.write("calib_2028fed_DemosAU_@TPP.csv", "1.5,2.5,\n")
        self.write("calib_1990fed_broken.csv", "1.5,2.5,\n")

        result = calibration_summary.compact(self.directory, ["2028fed"])

        self.assertEqual(result, [("2028fed", 1)])

    def test_active_provenance_files_exclude_superseded_legacy_inputs(self):
        current = self.write(
            "calib_2028fed_Current_@TPP.csv", "1.5,2.5,\n"
        )
        self.write("calib_2028fed_Obsolete_@TPP.csv", "3.5,4.5,\n")

        result = calibration_summary.compact(
            self.directory,
            ["2028fed"],
            input_paths_for_election=lambda election: {current},
        )

        self.assertEqual(result, [("2028fed", 1)])
        with calibration_summary.summary_path(
            self.directory, "2028fed"
        ).open(newline="", encoding="utf-8") as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(rows[0]["pollster"], "Current")

    def test_promotes_direct_staging_without_legacy_traces(self):
        loo_rows = calibration_summary.build_leave_one_out_rows(
            "2028fed", [("@TPP", "DemosAU", 1.5, 2.5)]
        )
        bias_rows = calibration_summary.build_bias_rows(
            "2028fed",
            [
                (
                    "@TPP",
                    51.25,
                    {"DemosAU": -0.25},
                    {"DemosAU": 2},
                )
            ],
        )
        calibration_summary.write_direct_staging_atomically(
            calibration_summary.direct_staging_path(
                self.directory, "2028fed", "leave-one-out"
            ),
            loo_rows,
        )
        calibration_summary.write_direct_staging_atomically(
            calibration_summary.direct_staging_path(
                self.directory, "2028fed", "bias"
            ),
            bias_rows,
        )

        summary, row_count = calibration_summary.promote_direct_summary(
            self.directory, "2028fed"
        )

        self.assertEqual(row_count, 3)
        self.assertTrue(summary.is_file())
        self.assertFalse(
            calibration_summary.direct_staging_path(
                self.directory, "2028fed", "leave-one-out"
            ).exists()
        )
        self.assertFalse(
            calibration_summary.direct_staging_path(
                self.directory, "2028fed", "bias"
            ).exists()
        )


if __name__ == "__main__":
    unittest.main()

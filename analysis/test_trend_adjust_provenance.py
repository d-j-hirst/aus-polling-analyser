import tempfile
import unittest
from pathlib import Path
from unittest import mock

import generated_provenance
import trend_adjust_provenance


class TrendAdjustmentProvenanceTests(unittest.TestCase):
    def test_historical_records_stop_before_hindcast_target(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            data_directory = Path(temporary_directory)
            (data_directory / "polled-elections.csv").write_text(
                "2022,fed\n2022,vic\n2025,fed\n",
                encoding="utf-8",
            )

            records = (
                trend_adjust_provenance
                .historical_cutoff_record_keys(
                    "2025fed", data_directory
                )
            )

            self.assertEqual(
                records,
                [
                    "cutoff_poll_outputs:2022fed",
                    "cutoff_poll_outputs:2022vic",
                ],
            )

    def test_future_target_uses_all_historical_trends(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            data_directory = Path(temporary_directory)
            (data_directory / "polled-elections.csv").write_text(
                "2022,fed\n2025,fed\n",
                encoding="utf-8",
            )

            records = (
                trend_adjust_provenance
                .historical_cutoff_record_keys(
                    "2028fed", data_directory
                )
            )

            self.assertEqual(
                records,
                [
                    "cutoff_poll_outputs:2022fed",
                    "cutoff_poll_outputs:2025fed",
                ],
            )

    def test_fundamentals_do_not_inherit_cutoff_dependency(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            analysis_directory = Path(temporary_directory)
            data_directory = analysis_directory / "Data"
            adjustments_directory = analysis_directory / "Adjustments"
            fundamentals_directory = (
                analysis_directory / "Fundamentals"
            )
            data_directory.mkdir()
            adjustments_directory.mkdir()
            fundamentals_directory.mkdir()
            source_file = analysis_directory / "source.txt"
            source_file.write_text("source\n", encoding="utf-8")
            cutoff_file = analysis_directory / "cutoff.csv"
            cutoff_file.write_text("cutoff\n", encoding="utf-8")
            adjustment_outputs = {}
            for group in ("ALP", "TPP"):
                output = (
                    adjustments_directory
                    / "adjust_2028fed_{}.csv".format(group)
                )
                output.write_text("output\n", encoding="utf-8")
                adjustment_outputs[group] = output
            fundamentals_output = (
                fundamentals_directory / "fundamentals_2028fed.csv"
            )
            fundamentals_output.write_text(
                "@TPP,50\n", encoding="utf-8"
            )
            recorder = (
                trend_adjust_provenance.TrendAdjustmentRecorder.__new__(
                    trend_adjust_provenance.TrendAdjustmentRecorder
                )
            )
            recorder.run_id = "test-run"
            recorder.run = {
                "generated_at_utc": generated_provenance.utc_now(),
                "command": ["test"],
                "source_revision": {
                    "system": "git",
                    "revision": None,
                    "dirty": True,
                },
                "environment": generated_provenance.current_environment(),
            }
            dependencies = {
                "trend_adjust_script":
                    generated_provenance.file_dependency(
                        "trend_adjust_script",
                        [source_file],
                        analysis_directory,
                    ),
                "cutoff_poll_outputs":
                    generated_provenance.file_dependency(
                        "cutoff_poll_outputs",
                        [cutoff_file],
                        analysis_directory,
                    ),
            }
            manifest_path = (
                adjustments_directory / "generated-provenance.json"
            )

            with mock.patch.object(
                trend_adjust_provenance,
                "ANALYSIS_DIRECTORY",
                analysis_directory,
            ):
                with mock.patch.object(
                    trend_adjust_provenance,
                    "DATA_DIRECTORY",
                    data_directory,
                ):
                    with mock.patch.object(
                        trend_adjust_provenance,
                        "MANIFEST_PATH",
                        manifest_path,
                    ):
                        recorder.record(
                            "2028fed",
                            adjustment_outputs,
                            fundamentals_output,
                            dependencies,
                            expected_groups=("ALP", "TPP"),
                        )

            manifest = generated_provenance.load_manifest(manifest_path)
            adjustment = manifest["records"][
                "trend_adjustments:2028fed:ALP"
            ]
            fundamentals = manifest["records"]["fundamentals:2028fed"]
            self.assertIn(
                "cutoff_poll_outputs", adjustment["dependencies"]
            )
            self.assertNotIn(
                "cutoff_poll_outputs", fundamentals["dependencies"]
            )


if __name__ == "__main__":
    unittest.main()

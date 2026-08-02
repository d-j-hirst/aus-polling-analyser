import tempfile
import unittest
from pathlib import Path
from unittest import mock

import calibration_summary_provenance
import generated_provenance


class CalibrationSummaryProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary_directory.name)
        self.calibration_directory = self.base / "Outputs" / "Calibration"
        self.calibration_directory.mkdir(parents=True)
        self.manifest_path = (
            self.calibration_directory / "generated-provenance.json"
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write_legacy_parent(self):
        source = self.calibration_directory / "calib_2028fed_DemosAU_@TPP.csv"
        source.write_text("1.2,2.3,\n", encoding="utf-8")
        parent = generated_provenance.generation_record(
            category="poll_calibration_compatibility_inputs",
            stage="calibrate_pollsters",
            scope=generated_provenance.generation_scope(
                elections=["2028fed"], parties=["@TPP"]
            ),
            run="legacy-run",
            dependencies={},
            outputs=generated_provenance.output_fingerprints([source], self.base),
            random_seed=None,
            status="legacy",
        )
        generated_provenance.update_manifest(
            self.manifest_path,
            {"poll_calibration_compatibility_inputs:2028fed:@TPP": parent},
            {
                "legacy-run": {
                    "generated_at_utc": "2026-01-01T00:00:00Z",
                    "command": ["legacy"],
                    "source_revision": {
                        "system": "git", "revision": None, "dirty": True
                    },
                    "environment": generated_provenance.current_environment(),
                }
            },
            path_base="../..",
            description="Test calibration provenance.",
        )

    def test_compact_summary_inherits_legacy_parent_issue(self):
        self.write_legacy_parent()
        output = self.calibration_directory / "Summaries" / "2028fed.csv"
        output.parent.mkdir()
        output.write_text("summary\n", encoding="utf-8")

        with mock.patch.object(
            calibration_summary_provenance, "ANALYSIS_DIRECTORY", self.base
        ), mock.patch.object(
            calibration_summary_provenance,
            "CALIBRATION_DIRECTORY",
            self.calibration_directory,
        ), mock.patch.object(
            calibration_summary_provenance, "MANIFEST_PATH", self.manifest_path
        ), mock.patch.object(
            calibration_summary_provenance,
            "_source_dependencies",
            return_value={},
        ):
            recorder = calibration_summary_provenance.CalibrationSummaryRecorder(
                ["python3", "calibration_summary.py"]
            )
            recorder.record("2028fed", output)
            recorder.flush()

        manifest = generated_provenance.load_manifest(self.manifest_path)
        record = manifest["records"]["poll_calibration_summaries:2028fed:compact"]
        self.assertEqual(record["status"], "generated")
        self.assertEqual(
            record["dependencies"]["poll_calibration_compatibility_inputs"][
                "records"
            ],
            ["poll_calibration_compatibility_inputs:2028fed:@TPP"],
        )
        issues = generated_provenance.check_manifest(self.manifest_path)[
            "poll_calibration_summaries:2028fed:compact"
        ]
        self.assertTrue(
            any(
                issue.startswith(
                    "stale generated dependency poll_calibration_compatibility_inputs"
                )
                for issue in issues
            )
        )

    def test_baseline_marks_preexisting_summary_as_legacy(self):
        output = self.calibration_directory / "Summaries" / "2028fed.csv"
        output.parent.mkdir()
        output.write_text("summary\n", encoding="utf-8")

        with mock.patch.object(
            calibration_summary_provenance, "ANALYSIS_DIRECTORY", self.base
        ), mock.patch.object(
            calibration_summary_provenance,
            "CALIBRATION_DIRECTORY",
            self.calibration_directory,
        ), mock.patch.object(
            calibration_summary_provenance, "MANIFEST_PATH", self.manifest_path
        ):
            calibration_summary_provenance.baseline_existing_summaries()

        manifest = generated_provenance.load_manifest(self.manifest_path)
        record = manifest["records"]["poll_calibration_summaries:2028fed:compact"]
        self.assertEqual(record["status"], "legacy")
        self.assertEqual(record["dependencies"], {})

    def test_direct_summary_records_sources_without_temporary_staging(self):
        output = self.calibration_directory / "Summaries" / "2028fed.csv"
        output.parent.mkdir()
        output.write_text("summary\n", encoding="utf-8")

        with mock.patch.object(
            calibration_summary_provenance, "ANALYSIS_DIRECTORY", self.base
        ), mock.patch.object(
            calibration_summary_provenance,
            "CALIBRATION_DIRECTORY",
            self.calibration_directory,
        ), mock.patch.object(
            calibration_summary_provenance, "MANIFEST_PATH", self.manifest_path
        ), mock.patch.object(
            calibration_summary_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            calibration_summary_provenance.calibration_provenance,
            "_source_dependencies",
            return_value={},
        ):
            calibration_summary_provenance.record_direct_summary(
                "2028fed", output, ["python3", "fp_model.py", "--bias"]
            )

        manifest = generated_provenance.load_manifest(self.manifest_path)
        record = manifest["records"]["poll_calibration_summaries:2028fed:compact"]
        self.assertEqual(record["stage"], "compact_calibration_summaries")
        self.assertEqual(record["dependencies"], {})

    def test_old_summary_category_is_a_compatibility_parent_only_for_calib_files(self):
        manifest = {
            "records": {
                "old-calib": {
                    "category": "poll_calibration_summaries",
                    "scope": {"elections": ["2028fed"]},
                    "outputs": {
                        "Outputs/Calibration/calib_2028fed_DemosAU_@TPP.csv": {}
                    },
                },
                "new-compact": {
                    "category": "poll_calibration_summaries",
                    "scope": {"elections": ["2028fed"]},
                    "outputs": {
                        "Outputs/Calibration/Summaries/2028fed.csv": {}
                    },
                },
            }
        }

        self.assertEqual(
            calibration_summary_provenance.compatibility_record_keys(
                "2028fed", "poll_calibration_compatibility_inputs", manifest
            ),
            ["old-calib"],
        )

    def test_current_compatibility_records_supersede_legacy_leftovers(self):
        manifest = {
            "records": {
                "legacy": {
                    "category": "poll_calibration_traces",
                    "scope": {"elections": ["2028fed"]},
                    "outputs": {
                        "Outputs/Calibration/calib_2028fed_Old_@TPP.csv": {},
                    },
                },
                "current": {
                    "category": "poll_calibration_compatibility_inputs",
                    "scope": {"elections": ["2028fed"]},
                    "outputs": {
                        "Outputs/Calibration/calib_2028fed_Current_@TPP.csv": {},
                    },
                },
            }
        }

        self.assertEqual(
            calibration_summary_provenance.compatibility_record_keys(
                "2028fed", "poll_calibration_compatibility_inputs", manifest
            ),
            ["current"],
        )

    def test_records_only_parents_with_consumed_outputs(self):
        manifest = {
            "records": {
                "used": {
                    "category": "poll_calibration_compatibility_inputs",
                    "scope": {"elections": ["2028fed"]},
                    "outputs": {
                        "Outputs/Calibration/calib_2028fed_Used_@TPP.csv": {},
                    },
                },
                "not-used": {
                    "category": "poll_calibration_compatibility_inputs",
                    "scope": {"elections": ["2028fed"]},
                    "outputs": {
                        "Outputs/Calibration/calib_2028fed_Old_@TPP.csv": {},
                    },
                },
            }
        }
        used_path = (
            self.base
            / "Outputs"
            / "Calibration"
            / "calib_2028fed_Used_@TPP.csv"
        )

        with mock.patch.object(
            calibration_summary_provenance, "ANALYSIS_DIRECTORY", self.base
        ):
            record_keys = (
                calibration_summary_provenance.compatibility_record_keys_for_paths(
                    "2028fed",
                    "poll_calibration_compatibility_inputs",
                    {used_path},
                    manifest,
                )
            )

        self.assertEqual(record_keys, ["used"])


if __name__ == "__main__":
    unittest.main()

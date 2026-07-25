import tempfile
import unittest
from pathlib import Path
from unittest import mock

import generated_provenance
import pollster_analysis_provenance


class PollsterAnalysisProvenanceTests(unittest.TestCase):
    def test_baseline_groups_only_canonical_outputs_by_election(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            calibration_directory = base / "Outputs" / "Calibration"
            calibration_directory.mkdir(parents=True)
            manifest_path = (
                calibration_directory
                / "pollster-generated-provenance.json"
            )
            for prefix in ("variability", "he_weighting", "biases"):
                (calibration_directory / f"{prefix}-2028fed.csv").write_text(
                    f"{prefix}\n", encoding="utf-8"
                )
                (
                    calibration_directory
                    / f"{prefix}-2028fed BASELINE CHECK.csv"
                ).write_text("backup\n", encoding="utf-8")

            with mock.patch.object(
                pollster_analysis_provenance,
                "ANALYSIS_DIRECTORY",
                base,
            ), mock.patch.object(
                pollster_analysis_provenance,
                "CALIBRATION_DIRECTORY",
                calibration_directory,
            ), mock.patch.object(
                pollster_analysis_provenance,
                "MANIFEST_PATH",
                manifest_path,
            ):
                pollster_analysis_provenance.baseline_existing_outputs()

            manifest = generated_provenance.load_manifest(manifest_path)
            self.assertEqual(
                set(manifest["records"]),
                {"pollster_parameters:2028fed"},
            )
            record = manifest["records"]["pollster_parameters:2028fed"]
            self.assertEqual(record["status"], "legacy")
            self.assertEqual(len(record["outputs"]), 3)
            self.assertTrue(
                all(
                    "BASELINE CHECK" not in path
                    for path in record["outputs"]
                )
            )

    def test_dependency_selection_uses_relevant_compact_calibration(self):
        manifest = {
            "records": {
                "summary-old": {
                    "category": "poll_calibration_summaries",
                    "scope": {"elections": ["2025fed"]},
                },
                "bias-target": {
                    "category": "bias_calibration_outputs",
                    "scope": {"elections": ["2028fed"]},
                },
                "trace-target": {
                    "category": "poll_calibration_traces",
                    "scope": {"elections": ["2028fed"]},
                },
                "bias-future": {
                    "category": "bias_calibration_outputs",
                    "scope": {"elections": ["2030fed"]},
                },
            }
        }

        with mock.patch.object(
            generated_provenance,
            "load_manifest",
            return_value=manifest,
        ):
            selected = (
                pollster_analysis_provenance._calibration_record_keys(
                    "2028fed",
                    lambda candidate, target: candidate <= target,
                )
            )

        self.assertEqual(
            selected,
            {
                "poll_calibration_summaries": ["summary-old"],
                "bias_calibration_outputs": ["bias-target"],
            },
        )

    def test_completed_work_unit_records_stale_calibration_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            calibration_directory = base / "Outputs" / "Calibration"
            calibration_directory.mkdir(parents=True)
            manifest_path = (
                calibration_directory
                / "pollster-generated-provenance.json"
            )
            outputs = []
            for prefix in ("variability", "he_weighting", "biases"):
                path = calibration_directory / f"{prefix}-2028fed.csv"
                path.write_text(f"{prefix}\n", encoding="utf-8")
                outputs.append(path)

            dependency = {
                "kind": "generated_manifest",
                "digest": "a" * 64,
                "semantic_revision": None,
                "manifest": (
                    "Outputs/Calibration/generated-provenance.json"
                ),
                "files": [],
                "records": ["calibration-record"],
            }
            with mock.patch.object(
                pollster_analysis_provenance,
                "ANALYSIS_DIRECTORY",
                base,
            ), mock.patch.object(
                pollster_analysis_provenance,
                "MANIFEST_PATH",
                manifest_path,
            ), mock.patch.object(
                pollster_analysis_provenance,
                "_source_dependencies",
                return_value={},
            ):
                recorder = (
                    pollster_analysis_provenance.PollsterAnalysisRecorder(
                        ["python3", "pollster_analysis.py"]
                    )
                )
                recorder.record(
                    "2028fed",
                    outputs,
                    {"bias_calibration_outputs": dependency},
                )

            manifest = generated_provenance.load_manifest(manifest_path)
            record = manifest["records"]["pollster_parameters:2028fed"]
            self.assertEqual(record["status"], "generated")
            self.assertIn(
                "bias_calibration_outputs", record["dependencies"]
            )
            self.assertIsNone(record["random_seed"])

    def test_missing_summary_records_use_prior_only_analysis(self):
        generated_dependency = {
            "kind": "generated_manifest",
            "digest": "a" * 64,
            "semantic_revision": None,
            "manifest": "Outputs/Calibration/generated-provenance.json",
            "files": [],
            "records": ["bias-record"],
        }
        with mock.patch.object(
            pollster_analysis_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            pollster_analysis_provenance,
            "_calibration_record_keys",
            return_value={
                "poll_calibration_summaries": [],
                "bias_calibration_outputs": ["bias-record"],
            },
        ), mock.patch.object(
            generated_provenance,
            "generated_manifest_dependency",
            return_value=generated_dependency,
        ):
            recorder = pollster_analysis_provenance.PollsterAnalysisRecorder(
                ["python3", "pollster_analysis.py"]
            )
            dependencies = recorder.dependencies_for(
                "1972fed", lambda candidate, target: True
            )

        self.assertNotIn(
            "poll_calibration_summaries", dependencies
        )
        self.assertIn("bias_calibration_outputs", dependencies)

    def test_missing_bias_calibration_still_fails(self):
        with mock.patch.object(
            pollster_analysis_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            pollster_analysis_provenance,
            "_calibration_record_keys",
            return_value={
                "poll_calibration_summaries": [],
                "bias_calibration_outputs": [],
            },
        ):
            recorder = pollster_analysis_provenance.PollsterAnalysisRecorder(
                ["python3", "pollster_analysis.py"]
            )
            with self.assertRaisesRegex(
                generated_provenance.GeneratedProvenanceError,
                "no bias_calibration_outputs records apply",
            ):
                recorder.dependencies_for(
                    "1972fed", lambda candidate, target: True
                )


if __name__ == "__main__":
    unittest.main()

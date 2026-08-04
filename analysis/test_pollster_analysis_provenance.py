import tempfile
import unittest
from pathlib import Path
from unittest import mock

import generated_provenance
import pollster_analysis_provenance


class PollsterAnalysisProvenanceTests(unittest.TestCase):
    def test_selected_inputs_exclude_unrecorded_legacy_files(self):
        manifest = {
            "records": {
                "current-summary": {
                    "outputs": {
                        (
                            "Outputs/Calibration/"
                            "calib_2028fed_Fox & Hedgehog_@TPP.csv"
                        ): {},
                    },
                },
                "unselected-legacy": {
                    "outputs": {
                        (
                            "Outputs/Calibration/"
                            "calib_2028fed_Fox&Hedgehog_@TPP.csv"
                        ): {},
                    },
                },
                "bias": {
                    "outputs": {
                        (
                            "Outputs/Calibration/"
                            "fp_polls_2028fed_@TPP_biascal.csv"
                        ): {},
                    },
                },
            }
        }
        selected = {
            "poll_calibration_compatibility_inputs": [
                "current-summary", "bias"
            ],
        }

        self.assertEqual(
            [
                path.name
                for path in pollster_analysis_provenance
                ._calibration_input_paths(manifest, selected)
            ],
            [
                "calib_2028fed_Fox & Hedgehog_@TPP.csv",
                "fp_polls_2028fed_@TPP_biascal.csv",
            ],
        )

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
                    "outputs": {
                        "Outputs/Calibration/Summaries/2025fed.csv": {},
                    },
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
                "poll_calibration_compatibility_inputs": [
                    "trace-target"
                ],
                "bias_calibration_compatibility_inputs": ["bias-target"],
            },
        )

    def test_dependency_selection_excludes_unused_party_records(self):
        manifest = {
            "records": {
                "bias-alp": {
                    "category": "bias_calibration_outputs",
                    "scope": {
                        "elections": ["2025fed"],
                        "parties": ["ALP FP"],
                    },
                },
                "bias-uap": {
                    "category": "bias_calibration_outputs",
                    "scope": {
                        "elections": ["2025fed"],
                        "parties": ["UAP FP"],
                    },
                },
                "summary-unscoped": {
                    "category": "poll_calibration_summaries",
                    "scope": {"elections": ["2025fed"]},
                    "outputs": {
                        "Outputs/Calibration/Summaries/2025fed.csv": {},
                    },
                },
            }
        }

        selected = pollster_analysis_provenance._calibration_record_keys(
            "2028fed",
            lambda candidate, target: candidate <= target,
            target_parties=["ALP FP"],
            manifest=manifest,
        )

        self.assertEqual(
            selected,
            {
                "poll_calibration_summaries": ["summary-unscoped"],
                "poll_calibration_compatibility_inputs": [],
                "bias_calibration_compatibility_inputs": [],
            },
        )

    def test_obsolete_calibration_party_dependency_is_reported(self):
        record = {
            "category": "pollster_parameters",
            "scope": {"elections": ["2028fed"]},
            "dependencies": {
                "bias_calibration_compatibility_inputs": {
                    "manifest": "calibration-generated-provenance.json",
                    "records": ["bias-alp", "bias-uap"],
                },
            },
        }
        calibration_manifest = {
            "records": {
                "bias-alp": {
                    "scope": {"parties": ["ALP FP"]},
                },
                "bias-uap": {
                    "scope": {"parties": ["UAP FP"]},
                },
            }
        }
        with mock.patch.object(
            pollster_analysis_provenance,
            "_target_parties",
            return_value={"ALP FP"},
        ), mock.patch.object(
            generated_provenance,
            "load_manifest",
            return_value=calibration_manifest,
        ):
            issues = (
                pollster_analysis_provenance
                .obsolete_calibration_dependency_issues(record, Path("."))
            )

        self.assertEqual(
            issues,
            [
                "obsolete calibration-party dependency "
                    "bias_calibration_compatibility_inputs (bias-uap)"
            ],
        )

    def test_obsolete_issues_reuse_check_context_manifest_cache(self):
        record = {
            "category": "pollster_parameters",
            "scope": {"elections": ["2028fed"]},
            "dependencies": {
                "bias_calibration_outputs": {
                    "manifest": "calibration-generated-provenance.json",
                    "records": ["bias-alp", "bias-uap"],
                },
            },
        }
        calibration_manifest = {
            "path_base": ".",
            "records": {
                "bias-alp": {
                    "scope": {"parties": ["ALP FP"]},
                    "outputs": {},
                    "dependencies": {},
                    "status": "generated",
                    "category": "bias_calibration_outputs",
                    "stage": "calibrate_pollster_bias",
                },
                "bias-uap": {
                    "scope": {"parties": ["UAP FP"]},
                    "outputs": {},
                    "dependencies": {},
                    "status": "generated",
                    "category": "bias_calibration_outputs",
                    "stage": "calibrate_pollster_bias",
                },
            },
        }
        context = generated_provenance.ManifestCheckContext()
        with mock.patch.object(
            pollster_analysis_provenance,
            "_target_parties",
            return_value={"ALP FP"},
        ), mock.patch.object(
            context,
            "resolve_path",
            side_effect=lambda path: Path(path),
        ), mock.patch.object(
            context,
            "load_manifest",
            return_value=calibration_manifest,
        ) as load_manifest, mock.patch.object(
            generated_provenance,
            "load_manifest",
            side_effect=AssertionError(
                "obsolete check should use the shared context"
            ),
        ):
            first = (
                pollster_analysis_provenance
                .obsolete_calibration_dependency_issues(
                    record, Path("."), check_context=context
                )
            )
            second = (
                pollster_analysis_provenance
                .obsolete_calibration_dependency_issues(
                    record, Path("."), check_context=context
                )
            )

        self.assertEqual(first, second)
        self.assertEqual(
            first,
            [
                "obsolete calibration-party dependency "
                "bias_calibration_outputs (bias-uap)"
            ],
        )
        self.assertEqual(load_manifest.call_count, 2)

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
                    {"bias_calibration_compatibility_inputs": dependency},
                )

            manifest = generated_provenance.load_manifest(manifest_path)
            record = manifest["records"]["pollster_parameters:2028fed"]
            self.assertEqual(record["status"], "generated")
            self.assertIn(
                "bias_calibration_compatibility_inputs", record["dependencies"]
            )
            self.assertIsNone(record["random_seed"])

    def test_missing_compatibility_records_fail(self):
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
                "poll_calibration_compatibility_inputs": [],
                "bias_calibration_compatibility_inputs": [],
            },
        ):
            recorder = pollster_analysis_provenance.PollsterAnalysisRecorder(
                ["python3", "pollster_analysis.py"]
            )
            with self.assertRaisesRegex(
                generated_provenance.GeneratedProvenanceError,
                "no calibration evidence records apply",
            ):
                recorder.dependencies_for(
                    "1972fed", lambda candidate, target: True
                )

    def test_missing_calibration_evidence_still_fails(self):
        with mock.patch.object(
            pollster_analysis_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            pollster_analysis_provenance,
            "_calibration_record_keys",
            return_value={
                "poll_calibration_summaries": [],
                "poll_calibration_compatibility_inputs": [],
                "bias_calibration_compatibility_inputs": [],
            },
        ):
            recorder = pollster_analysis_provenance.PollsterAnalysisRecorder(
                ["python3", "pollster_analysis.py"]
            )
            with self.assertRaisesRegex(
                generated_provenance.GeneratedProvenanceError,
                "no calibration evidence records apply",
            ):
                recorder.dependencies_for(
                    "1972fed", lambda candidate, target: True
                )


if __name__ == "__main__":
    unittest.main()

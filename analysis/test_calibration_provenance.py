import tempfile
import unittest
from pathlib import Path
from unittest import mock

import calibration_provenance
import generated_provenance


class CalibrationProvenanceTests(unittest.TestCase):
    def test_configured_parties_are_loaded_by_election(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "significant-parties.csv"
            path.write_text(
                "2025,fed,ALP FP,LNP FP,@TPP\n"
                "2026,vic,ALP FP,LNP FP,GRN FP,@TPP\n",
                encoding="utf-8",
            )

            parties = calibration_provenance.configured_parties_by_election(
                path
            )

        self.assertEqual(
            parties["2025fed"], {"ALP FP", "LNP FP", "@TPP"}
        )
        self.assertEqual(
            parties["2026vic"],
            {"ALP FP", "LNP FP", "GRN FP", "@TPP"},
        )

    def test_stan_seed_is_stable_and_work_unit_specific(self):
        first = calibration_provenance.derive_stan_seed(
            1234, "2028fed", "@TPP", "Newspoll", "pollster-calibration"
        )
        repeated = calibration_provenance.derive_stan_seed(
            1234, "2028fed", "@TPP", "Newspoll", "pollster-calibration"
        )
        different = calibration_provenance.derive_stan_seed(
            1234, "2028fed", "@TPP", "Resolve", "pollster-calibration"
        )

        self.assertEqual(first, repeated)
        self.assertNotEqual(first, different)
        self.assertGreaterEqual(first, 1)
        self.assertLess(first, 2 ** 31)

    def test_baseline_groups_existing_files_without_certifying_them(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            calibration_directory = base / "Outputs" / "Calibration"
            calibration_directory.mkdir(parents=True)
            manifest_path = (
                calibration_directory / "generated-provenance.json"
            )
            names = []
            for kind in ("trend", "polls", "house_effects"):
                names.extend(
                    (
                        "fp_{}_2028fed_@TPP.csv".format(kind),
                        "fp_{}_2028fed_@TPP_Newspoll.csv".format(kind),
                        "fp_{}_2028fed_@TPP_biascal.csv".format(kind),
                    )
                )
            names.extend(
                (
                    "calib_2028fed_Newspoll_@TPP.csv",
                    "calib_2028fed_Resolve_@TPP.csv",
                    "variability-2028fed.csv",
                )
            )
            for name in names:
                (calibration_directory / name).write_text(
                    "{}\n".format(name), encoding="utf-8"
                )

            with mock.patch.object(
                calibration_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                calibration_provenance,
                "CALIBRATION_DIRECTORY",
                calibration_directory,
            ), mock.patch.object(
                calibration_provenance, "MANIFEST_PATH", manifest_path
            ):
                calibration_provenance.baseline_existing_outputs()

            manifest = generated_provenance.load_manifest(manifest_path)
            self.assertEqual(len(manifest["records"]), 4)
            self.assertEqual(
                {
                    record["category"]
                    for record in manifest["records"].values()
                },
                {
                    "poll_calibration_compatibility_inputs",
                    "bias_calibration_compatibility_inputs",
                },
            )
            self.assertTrue(
                all(
                    record["status"] == "legacy"
                    and not record["dependencies"]
                    and record["random_seed"] is None
                    for record in manifest["records"].values()
                )
            )
            recorded_outputs = {
                Path(path).name
                for record in manifest["records"].values()
                for path in record["outputs"]
            }
            self.assertNotIn("variability-2028fed.csv", recorded_outputs)

    def test_completed_model_unit_replaces_legacy_status(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            calibration_directory = base / "Outputs" / "Calibration"
            calibration_directory.mkdir(parents=True)
            manifest_path = (
                calibration_directory / "generated-provenance.json"
            )
            outputs = []
            for kind in ("trend", "polls", "house_effects"):
                path = calibration_directory / (
                    "fp_{}_2028fed_@TPP_Newspoll.csv".format(kind)
                )
                path.write_text("{}\n".format(kind), encoding="utf-8")
                outputs.append(path)

            with mock.patch.object(
                calibration_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                calibration_provenance,
                "CALIBRATION_DIRECTORY",
                calibration_directory,
            ), mock.patch.object(
                calibration_provenance, "MANIFEST_PATH", manifest_path
            ), mock.patch.object(
                calibration_provenance,
                "_source_dependencies",
                return_value={},
            ):
                recorder = calibration_provenance.CalibrationRecorder(
                    ["python3", "fp_model.py", "--calibrate"]
                )
                recorder.record_model_outputs(
                    election="2028fed",
                    party="@TPP",
                    excluded_pollster="Newspoll",
                    bias_calibration=False,
                    outputs=outputs,
                    random_seed=123456,
                    feedback_files=[],
                )
                recorder.flush()

            manifest = generated_provenance.load_manifest(manifest_path)
            record = next(iter(manifest["records"].values()))
            self.assertEqual(record["status"], "generated")
            self.assertEqual(record["random_seed"], 123456)
            self.assertEqual(
                record["scope"]["qualifiers"]["excluded_pollster"],
                "Newspoll",
            )
            self.assertEqual(
                generated_provenance.check_manifest(manifest_path),
                {next(iter(manifest["records"])): []},
            )

    def test_federal_prior_is_recorded_as_calibration_output(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            calibration_directory = base / "Outputs" / "Calibration"
            prior = calibration_directory / "Priors" / "2028fed.csv"
            prior.parent.mkdir(parents=True)
            prior.write_text(
                "Date,Party,50%\n2026-01-01,ONP FP,6.25\n",
                encoding="utf-8",
            )
            manifest_path = calibration_directory / "generated-provenance.json"

            with mock.patch.object(
                calibration_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                calibration_provenance, "MANIFEST_PATH", manifest_path
            ), mock.patch.object(
                calibration_provenance,
                "_source_dependencies",
                return_value={},
            ):
                recorder = calibration_provenance.CalibrationRecorder(
                    ["python3", "fp_model.py", "--calibrate"]
                )
                recorder.record_federal_priors("2028fed", prior)
                recorder.flush()

            manifest = generated_provenance.load_manifest(manifest_path)
            record = manifest["records"]["federal_calibration_priors:2028fed"]
            self.assertEqual(
                record["category"], "federal_calibration_priors"
            )
            self.assertEqual(record["stage"], "calibrate_pollsters")
            self.assertEqual(record["scope"]["elections"], ["2028fed"])

    def test_required_federal_priors_follow_state_overlap(self):
        import pandas as pd

        cycles = {
            ("1983", "fed"): (
                pd.Timestamp("1980-10-18"),
                pd.Timestamp("1983-03-05"),
            ),
            ("1984", "fed"): (
                pd.Timestamp("1983-03-06"),
                pd.Timestamp("1984-12-02"),
            ),
            ("1988", "nsw"): (
                pd.Timestamp("1984-03-25"),
                pd.Timestamp("1988-03-19"),
            ),
        }

        self.assertEqual(
            calibration_provenance.required_federal_prior_work_units(
                {"1988nsw"},
                election_cycles=cycles,
            ),
            {"federal_calibration_priors:1984fed"},
        )
        self.assertEqual(
            calibration_provenance.required_federal_prior_work_units(
                {"1983fed"},
                election_cycles=cycles,
            ),
            set(),
        )
        self.assertEqual(
            calibration_provenance.required_federal_prior_work_units(
                {"1984fed"},
                election_cycles=cycles,
            ),
            {"federal_calibration_priors:1984fed"},
        )
        self.assertEqual(
            calibration_provenance.required_federal_prior_work_units(
                None,
                election_cycles=cycles,
            ),
            {"federal_calibration_priors:1984fed"},
        )

    def test_residual_evidence_has_a_separate_generated_record(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            calibration_directory = base / "Outputs" / "Calibration"
            staging = (
                calibration_directory
                / "Staging"
                / "2028fed-leave-one-out.csv"
            )
            evidence = (
                calibration_directory / "Evidence" / "2028fed.csv"
            )
            staging.parent.mkdir(parents=True)
            evidence.parent.mkdir(parents=True)
            staging.write_text("staging\n", encoding="utf-8")
            evidence.write_text("evidence\n", encoding="utf-8")
            manifest_path = calibration_directory / "generated-provenance.json"

            with mock.patch.object(
                calibration_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                calibration_provenance, "MANIFEST_PATH", manifest_path
            ), mock.patch.object(
                calibration_provenance,
                "_source_dependencies",
                return_value={},
            ):
                recorder = calibration_provenance.CalibrationRecorder(
                    ["python3", "fp_model.py", "--calibrate"]
                )
                recorder.record_summaries(
                    "2028fed",
                    [staging],
                    [],
                    residual_evidence=evidence,
                )
                recorder.flush()

            manifest = generated_provenance.load_manifest(manifest_path)
            record = manifest["records"][
                "poll_calibration_residual_evidence:2028fed"
            ]
            self.assertEqual(
                record["category"],
                "poll_calibration_residual_evidence",
            )
            self.assertEqual(
                list(record["outputs"]),
                ["Outputs/Calibration/Evidence/2028fed.csv"],
            )

    def test_mode_specific_seed_manifest_has_separate_category(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output = (
                base
                / "Outputs"
                / "Calibration"
                / "Seeds"
                / "2028fed-calibration.csv"
            )
            output.parent.mkdir(parents=True)
            output.write_text("seeds\n", encoding="utf-8")
            manifest_path = (
                base
                / "Outputs"
                / "Calibration"
                / "generated-provenance.json"
            )
            with mock.patch.object(
                calibration_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                calibration_provenance, "MANIFEST_PATH", manifest_path
            ), mock.patch.object(
                calibration_provenance,
                "_source_dependencies",
                return_value={},
            ):
                recorder = calibration_provenance.CalibrationRecorder(
                    ["python3", "fp_model.py", "--calibrate"]
                )
                recorder.record_seed_manifest(
                    "2028fed", "calibration", output
                )
                recorder.flush()

            manifest = generated_provenance.load_manifest(manifest_path)
            record = manifest["records"][
                "poll_calibration_stan_seeds:2028fed:calibration"
            ]
            self.assertEqual(
                record["category"], "poll_calibration_stan_seeds"
            )


if __name__ == "__main__":
    unittest.main()

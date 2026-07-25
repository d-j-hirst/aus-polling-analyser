import tempfile
import unittest
from pathlib import Path
from unittest import mock

import calibration_provenance
import generated_provenance


class CalibrationProvenanceTests(unittest.TestCase):
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
                    "poll_calibration_traces",
                    "bias_calibration_outputs",
                    "poll_calibration_summaries",
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


if __name__ == "__main__":
    unittest.main()

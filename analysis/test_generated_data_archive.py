import json
import tempfile
import unittest
from pathlib import Path

import generated_data_archive


class GeneratedDataArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.analysis = Path(self.temporary_directory.name) / "analysis"
        self.analysis.mkdir()
        for root in generated_data_archive.REQUIRED_FULL_ROOTS:
            path = self.analysis / root / "sample.csv"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("{}\n".format(root), encoding="utf-8")
        (self.analysis / "Regional").mkdir()
        (self.analysis / "Regional" / "2028fed-polls.csv").write_text(
            "authored\n", encoding="utf-8"
        )
        (self.analysis / "Regional" / "2028fed-swing-deviations.csv").write_text(
            "generated\n", encoding="utf-8"
        )
        (self.analysis / "Federal-State").mkdir()
        (self.analysis / "Federal-State" / "booths-2028fed.txt").write_text(
            "authored\n", encoding="utf-8"
        )
        (self.analysis / "Federal-State" / "2028fed.pkl").write_bytes(b"cache")

    def tearDown(self):
        self.temporary_directory.cleanup()

    def current_audit(self):
        return {
            "summary": {"has_blockers": False},
            "source_issues": [],
            "manifest_issues": [],
            "work_units": [{"status": "current"}],
        }

    def test_builds_validated_archive_without_temporary_diagnostics(self):
        diagnostic = self.analysis / "Outputs" / "Calibration" / "Diagnostics" / "run" / "trace.csv"
        diagnostic.parent.mkdir(parents=True)
        diagnostic.write_text("diagnostic\n", encoding="utf-8")
        legacy = self.analysis / "Outputs" / "Calibration" / "calib_2028fed_Test_@TPP.csv"
        legacy.write_text("legacy trace\n", encoding="utf-8")
        summary = self.analysis / "Outputs" / "Calibration" / "Summaries" / "2028fed.csv"
        summary.parent.mkdir()
        summary.write_text("compact summary\n", encoding="utf-8")

        result = generated_data_archive.build_archive(
            self.analysis, audit_runner=self.current_audit
        )

        archive = result["archive_directory"]
        self.assertEqual(result["files"], 9)
        self.assertTrue((archive / generated_data_archive.ARCHIVE_MANIFEST_NAME).is_file())
        self.assertFalse((archive / "Outputs" / "Calibration" / "Diagnostics").exists())
        self.assertFalse((archive / "Outputs" / "Calibration" / legacy.name).exists())
        self.assertEqual(
            (archive / "Outputs" / "Calibration" / "Summaries" / "2028fed.csv").read_text(
                encoding="utf-8"
            ),
            "compact summary\n",
        )
        self.assertEqual(
            generated_data_archive.validate_archive(archive)["schema_version"],
            generated_data_archive.ARCHIVE_SCHEMA_VERSION,
        )

    def test_preflight_rejects_noncurrent_work_or_calibration_staging(self):
        stale = self.current_audit()
        stale["work_units"] = [{"status": "legacy", "stage": "calibrate_pollsters", "record_key": "x"}]
        with self.assertRaisesRegex(
            generated_data_archive.GeneratedDataArchiveError, "non-current work"
        ):
            generated_data_archive.preflight_build(
                self.analysis, audit_runner=lambda: stale
            )

        staging = self.analysis / "Outputs" / "Calibration" / "Staging" / "2028fed-bias.csv"
        staging.parent.mkdir(parents=True)
        staging.write_text("partial\n", encoding="utf-8")
        with self.assertRaisesRegex(
            generated_data_archive.GeneratedDataArchiveError, "staging files"
        ):
            generated_data_archive.preflight_build(
                self.analysis, audit_runner=self.current_audit
            )

    def test_restore_replaces_generated_data_and_preserves_authored_mixed_inputs(self):
        archive = generated_data_archive.build_archive(
            self.analysis, audit_runner=self.current_audit
        )["archive_directory"]
        (self.analysis / "Outputs" / "sample.csv").write_text("changed\n", encoding="utf-8")
        (self.analysis / "Outputs" / "extra.csv").write_text("extra\n", encoding="utf-8")
        (self.analysis / "Regional" / "2028fed-polls.csv").write_text(
            "updated authored\n", encoding="utf-8"
        )
        (self.analysis / "Regional" / "2028fed-swing-deviations.csv").write_text(
            "changed generated\n", encoding="utf-8"
        )
        (self.analysis / "Federal-State" / "booths-2028fed.txt").write_text(
            "updated authored\n", encoding="utf-8"
        )

        result = generated_data_archive.restore_archive(self.analysis, archive)

        self.assertIn("Outputs", result["roots"])
        self.assertEqual(
            (self.analysis / "Outputs" / "sample.csv").read_text(encoding="utf-8"),
            "Outputs\n",
        )
        self.assertFalse((self.analysis / "Outputs" / "extra.csv").exists())
        self.assertEqual(
            (self.analysis / "Regional" / "2028fed-polls.csv").read_text(encoding="utf-8"),
            "updated authored\n",
        )
        self.assertEqual(
            (self.analysis / "Regional" / "2028fed-swing-deviations.csv").read_text(encoding="utf-8"),
            "generated\n",
        )
        self.assertEqual(
            (self.analysis / "Federal-State" / "booths-2028fed.txt").read_text(encoding="utf-8"),
            "updated authored\n",
        )

    def test_restore_rejects_a_tampered_payload_before_replacing_outputs(self):
        archive = generated_data_archive.build_archive(
            self.analysis, audit_runner=self.current_audit
        )["archive_directory"]
        archived_output = archive / "Outputs" / "sample.csv"
        archived_output.write_text("tampered\n", encoding="utf-8")
        (self.analysis / "Outputs" / "sample.csv").write_text("local\n", encoding="utf-8")

        with self.assertRaisesRegex(
            generated_data_archive.GeneratedDataArchiveError, "fingerprint"
        ):
            generated_data_archive.restore_archive(self.analysis, archive)

        self.assertEqual(
            (self.analysis / "Outputs" / "sample.csv").read_text(encoding="utf-8"),
            "local\n",
        )

    def test_manifest_rejects_paths_outside_managed_roots(self):
        archive = generated_data_archive.build_archive(
            self.analysis, audit_runner=self.current_audit
        )["archive_directory"]
        manifest_path = archive / generated_data_archive.ARCHIVE_MANIFEST_NAME
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["files"][0]["path"] = "Data/poll-data-fed.csv"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        with self.assertRaisesRegex(
            generated_data_archive.GeneratedDataArchiveError, "unsupported root"
        ):
            generated_data_archive.validate_archive(archive)

    def test_manifest_rejects_a_declared_root_without_payload(self):
        archive = generated_data_archive.build_archive(
            self.analysis, audit_runner=self.current_audit
        )["archive_directory"]
        manifest_path = archive / generated_data_archive.ARCHIVE_MANIFEST_NAME
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["full_roots"].append("Synthetic TPPs")
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        with self.assertRaisesRegex(
            generated_data_archive.GeneratedDataArchiveError,
            "roots do not match",
        ):
            generated_data_archive.validate_archive(archive)


if __name__ == "__main__":
    unittest.main()

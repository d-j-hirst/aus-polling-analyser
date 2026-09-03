import json
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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

    def _write_scoped_generated_manifest(self):
        outputs = self.analysis / "Outputs"
        active_path = outputs / "active.csv"
        inactive_path = outputs / "inactive.csv"
        active_path.write_text("active\n", encoding="utf-8")
        inactive_path.write_text("inactive\n", encoding="utf-8")

        def fingerprint(path):
            content = path.read_bytes()
            return {
                "sha256": hashlib.sha256(content).hexdigest(),
                "size_bytes": len(content),
                "mtime_ns": path.stat().st_mtime_ns,
            }

        def run(command):
            return {
                "generated_at_utc": "2026-08-31T00:00:00Z",
                "command": [command],
                "source_revision": {
                    "system": "git",
                    "revision": "test",
                    "dirty": False,
                },
                "environment": {
                    "python_version": "3",
                    "python_implementation": "CPython",
                    "platform": "test",
                    "packages": {},
                },
            }

        def record(election, run_id, output_path, path):
            return {
                "status": "generated",
                "category": "poll_trend_outputs",
                "stage": "generate_poll_trends",
                "scope": {
                    "all": False,
                    "elections": [election],
                    "parties": ["@TPP"],
                    "qualifiers": {},
                },
                "run": run_id,
                "random_seed": 1,
                "dependencies": {},
                "outputs": {output_path: fingerprint(path)},
                "provenance_maintenance": [],
            }

        manifest = {
            "$schema": "../generated_provenance.schema.json",
            "schema_version": 1,
            "path_base": "..",
            "description": "Archive selection test.",
            "updated_at_utc": "2026-08-31T00:00:00Z",
            "runs": {
                "active-run": run("active"),
                "inactive-run": run("inactive"),
            },
            "records": {
                "active": record(
                    "2028fed", "active-run", "Outputs/active.csv", active_path
                ),
                "inactive": record(
                    "2028qld",
                    "inactive-run",
                    "Outputs/inactive.csv",
                    inactive_path,
                ),
            },
        }
        (outputs / "generated-provenance.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )

    def required_audit(self):
        manifest = "Outputs/generated-provenance.json"
        active_id = "{}::active".format(manifest)
        inactive_id = "{}::inactive".format(manifest)
        return {
            "summary": {"has_blockers": True},
            "internal_errors": [],
            "source_issues": [],
            "manifest_issues": [],
            "work_units": [
                {
                    "id": active_id,
                    "manifest": manifest,
                    "record_key": "active",
                    "status": "current",
                    "stage": "generate_poll_trends",
                },
                {
                    "id": inactive_id,
                    "manifest": manifest,
                    "record_key": "inactive",
                    "status": "stale",
                    "stage": "generate_poll_trends",
                },
            ],
            "required_graph": {
                "required_work_unit_ids": [active_id],
                "inactive_only_work_unit_ids": [inactive_id],
                "unreferenced_work_unit_ids": [],
            },
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
        evidence = (
            self.analysis
            / "Outputs"
            / "Calibration"
            / "Evidence"
            / "2028fed.csv"
        )
        evidence.parent.mkdir()
        evidence.write_text("residual evidence\n", encoding="utf-8")
        seed_manifest = (
            self.analysis
            / "Outputs"
            / "Calibration"
            / "Seeds"
            / "2028fed-calibration.csv"
        )
        seed_manifest.parent.mkdir()
        seed_manifest.write_text("resolved seeds\n", encoding="utf-8")
        checkpoint = (
            self.analysis
            / "Outputs"
            / "Calibration"
            / "Checkpoints"
            / "2028fed"
            / "full.json"
        )
        checkpoint.parent.mkdir(parents=True)
        checkpoint.write_text("{}\n", encoding="utf-8")
        cutoff_draft = (
            self.analysis
            / "Outputs"
            / "Cutoffs"
            / "fp_cutoffs_2025fed.csv.in-progress"
        )
        cutoff_draft.parent.mkdir(parents=True, exist_ok=True)
        cutoff_draft.write_text("draft\n", encoding="utf-8")
        cutoff_sidecar = cutoff_draft.with_suffix(
            cutoff_draft.suffix + ".json"
        )
        cutoff_sidecar.write_text("{}\n", encoding="utf-8")

        result = generated_data_archive.build_archive(
            self.analysis, audit_runner=self.current_audit
        )

        archive = result["archive_directory"]
        self.assertEqual(result["files"], 11)
        self.assertTrue((archive / generated_data_archive.ARCHIVE_MANIFEST_NAME).is_file())
        self.assertFalse((archive / "Outputs" / "Calibration" / "Diagnostics").exists())
        self.assertFalse((archive / "Outputs" / "Calibration" / "Checkpoints").exists())
        self.assertFalse((archive / "Outputs" / "Calibration" / legacy.name).exists())
        self.assertFalse(
            (
                archive
                / "Outputs"
                / "Cutoffs"
                / cutoff_draft.name
            ).exists()
        )
        self.assertFalse(
            (
                archive
                / "Outputs"
                / "Cutoffs"
                / cutoff_sidecar.name
            ).exists()
        )
        self.assertEqual(
            (archive / "Outputs" / "Calibration" / "Summaries" / "2028fed.csv").read_text(
                encoding="utf-8"
            ),
            "compact summary\n",
        )
        self.assertEqual(
            (
                archive
                / "Outputs"
                / "Calibration"
                / "Evidence"
                / "2028fed.csv"
            ).read_text(encoding="utf-8"),
            "residual evidence\n",
        )
        self.assertEqual(
            (
                archive
                / "Outputs"
                / "Calibration"
                / "Seeds"
                / "2028fed-calibration.csv"
            ).read_text(encoding="utf-8"),
            "resolved seeds\n",
        )
        self.assertEqual(
            generated_data_archive.validate_archive(archive)["schema_version"],
            generated_data_archive.ARCHIVE_SCHEMA_VERSION,
        )

    def test_archive_promotion_retries_a_transient_directory_lock(self):
        staging = self.analysis / ".Archived-build"
        archive = self.analysis / "Archived"
        staging.mkdir()
        archive.mkdir()
        (staging / "new.txt").write_text("new\n", encoding="utf-8")
        (archive / "old.txt").write_text("old\n", encoding="utf-8")
        real_replace = generated_data_archive.os.replace
        failed_once = False

        def transient_replace(source, destination):
            nonlocal failed_once
            if Path(source) == staging and not failed_once:
                failed_once = True
                raise PermissionError(13, "temporary Windows directory lock")
            return real_replace(source, destination)

        with mock.patch.object(
            generated_data_archive.os,
            "replace",
            side_effect=transient_replace,
        ), mock.patch.object(generated_data_archive.time, "sleep") as sleep:
            generated_data_archive._promote_directory(staging, archive)

        self.assertTrue(failed_once)
        sleep.assert_called_once_with(0.25)
        self.assertEqual(
            (archive / "new.txt").read_text(encoding="utf-8"),
            "new\n",
        )
        self.assertFalse((archive / "old.txt").exists())

    def test_preflight_rejects_noncurrent_work(self):
        stale = self.current_audit()
        stale["work_units"] = [{"status": "legacy", "stage": "calibrate_pollsters", "record_key": "x"}]
        with self.assertRaisesRegex(
            generated_data_archive.GeneratedDataArchiveError, "non-current work"
        ):
            generated_data_archive.preflight_build(
                self.analysis, audit_runner=lambda: stale
            )

    def test_preflight_ignores_unreferenced_calibration_staging(self):
        staging = self.analysis / "Outputs" / "Calibration" / "Staging" / "2028fed-bias.csv"
        staging.parent.mkdir(parents=True)
        staging.write_text("partial\n", encoding="utf-8")

        preflight = generated_data_archive.preflight_build(
            self.analysis, audit_runner=self.current_audit
        )

        self.assertNotIn(
            "Outputs/Calibration/Staging/2028fed-bias.csv",
            preflight["managed_files"],
        )

    def test_archive_ignores_inactive_staleness_and_filters_its_manifest(self):
        self._write_scoped_generated_manifest()

        result = generated_data_archive.build_archive(
            self.analysis, audit_runner=self.required_audit
        )

        archive = result["archive_directory"]
        self.assertTrue((archive / "Outputs" / "active.csv").is_file())
        self.assertFalse((archive / "Outputs" / "inactive.csv").exists())
        self.assertFalse((archive / "Outputs" / "sample.csv").exists())
        filtered = json.loads(
            (archive / "Outputs" / "generated-provenance.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(set(filtered["records"]), {"active"})
        self.assertEqual(set(filtered["runs"]), {"active-run"})

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

    def test_booth_results_are_an_optional_full_archive_root(self):
        booth_directory = self.analysis / "Booth Results"
        booth_directory.mkdir()
        booth_json = booth_directory / "2022sa.json"
        booth_json.write_text("{}\n", encoding="utf-8")

        managed = generated_data_archive._managed_relative_paths(
            self.analysis
        )

        self.assertIn("Booth Results/2022sa.json", managed)
        self.assertTrue(
            generated_data_archive._archive_eligible_path(
                "Booth Results/2022sa.json"
            )
        )
        self.assertFalse(
            generated_data_archive._archive_eligible_path(
                "downloads/2026sa_zeros.xml"
            )
        )


if __name__ == "__main__":
    unittest.main()

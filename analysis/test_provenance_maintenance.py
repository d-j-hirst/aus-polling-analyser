import tempfile
import unittest
from pathlib import Path
from unittest import mock

import generated_provenance
import provenance_maintenance
import source_provenance


class ProvenanceMaintenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)
        self.source = self.folder / "source.py"
        self.output = self.folder / "output.csv"
        self.source.write_text("version = 1\n", encoding="utf-8")
        self.output.write_text("value\n1\n", encoding="utf-8")
        self.source_manifest = self.folder / "provenance.json"
        source_provenance.initialize_manifest(
            self.source_manifest, "Test sources."
        )
        source_provenance.add_category(
            self.source_manifest,
            "generator_script",
            "Test generator.",
            ["source.py"],
        )
        self.generated_manifest = (
            self.folder / "generated-provenance.json"
        )
        run_id, run = generated_provenance.generation_run(
            ["test-generator"],
            {"system": "git", "revision": None, "dirty": True},
            generated_provenance.current_environment(),
        )
        record = generated_provenance.generation_record(
            category="test_outputs",
            stage="test_stage",
            scope=generated_provenance.generation_scope(
                elections=["2026vic"]
            ),
            run=run_id,
            dependencies={
                "generator_script":
                    generated_provenance.source_manifest_dependency(
                        "generator_script",
                        self.source_manifest,
                        self.folder,
                    )
            },
            outputs=generated_provenance.output_fingerprints(
                [self.output], self.folder
            ),
            random_seed=123,
        )
        generated_provenance.update_manifest(
            self.generated_manifest,
            {"test_outputs:2026vic": record},
            {run_id: run},
            path_base=".",
            description="Test generated data.",
        )

    def tearDown(self):
        self.temporary.cleanup()

    def _record_provenance_change(self):
        self.source.write_text("version = 1  # metadata\n", encoding="utf-8")
        source_provenance.record_change(
            self.source_manifest,
            "generator_script",
            "Updated metadata bookkeeping.",
            "schema",
            "provenance-only",
            False,
            source_provenance._build_scope(all_scopes=True),
            provenance_upgrade="refresh-source-dependency-v1",
        )

    def test_metadata_upgrade_preserves_generation_identity_and_outputs(self):
        self._record_provenance_change()
        before = generated_provenance.load_manifest(
            self.generated_manifest
        )["records"]["test_outputs:2026vic"]
        issues = generated_provenance.check_manifest(
            self.generated_manifest
        )["test_outputs:2026vic"]
        self.assertTrue(
            any(
                issue.startswith("provenance-only dependency revision ")
                for issue in issues
            )
        )

        applied = provenance_maintenance.maintain_record(
            self.generated_manifest, "test_outputs:2026vic"
        )

        after = generated_provenance.load_manifest(
            self.generated_manifest
        )["records"]["test_outputs:2026vic"]
        self.assertEqual(applied, 1)
        self.assertEqual(after["run"], before["run"])
        self.assertEqual(after["random_seed"], before["random_seed"])
        self.assertEqual(after["outputs"], before["outputs"])
        self.assertEqual(
            after["provenance_maintenance"][0]["upgrade"],
            "refresh-source-dependency-v1",
        )
        self.assertEqual(
            generated_provenance.check_manifest(
                self.generated_manifest
            )["test_outputs:2026vic"],
            [],
        )

    def test_data_staleness_blocks_metadata_upgrade(self):
        self.source.write_text("version = 2\n", encoding="utf-8")
        source_provenance.record_change(
            self.source_manifest,
            "generator_script",
            "Changed generated values.",
            "methodology",
            "minor",
            True,
            source_provenance._build_scope(all_scopes=True),
        )

        with self.assertRaisesRegex(
            provenance_maintenance.ProvenanceMaintenanceError,
            "cannot receive metadata maintenance",
        ):
            provenance_maintenance.maintain_record(
                self.generated_manifest, "test_outputs:2026vic"
            )

    def test_metadata_upgrade_retries_after_concurrent_publication(self):
        self._record_provenance_change()
        original_update = generated_provenance.update_manifest
        attempts = 0

        def update_with_one_conflict(*args, **kwargs):
            nonlocal attempts
            attempts += 1
            if attempts == 1:
                raise generated_provenance.ConcurrentManifestUpdate(
                    "test conflict"
                )
            return original_update(*args, **kwargs)

        with mock.patch.object(
            generated_provenance,
            "update_manifest",
            side_effect=update_with_one_conflict,
        ):
            applied = provenance_maintenance.maintain_record(
                self.generated_manifest, "test_outputs:2026vic"
            )

        self.assertEqual(attempts, 2)

    def test_generated_dependency_refresh_preserves_completed_output(self):
        """A dependency-only upstream correction need not rerun Stan work."""

        upstream_source = self.folder / "upstream.py"
        upstream_source.write_text("version = 1\n", encoding="utf-8")
        upstream_manifest = self.folder / "upstream-provenance.json"
        source_provenance.initialize_manifest(
            upstream_manifest, "Upstream sources."
        )
        source_provenance.add_category(
            upstream_manifest,
            "upstream_script",
            "Upstream generator.",
            ["upstream.py"],
        )
        upstream_generated = (
            self.folder / "upstream-generated-provenance.json"
        )
        run_id, run = generated_provenance.generation_run(
            ["upstream"],
            {"system": "git", "revision": None, "dirty": True},
            generated_provenance.current_environment(),
        )
        upstream_record = generated_provenance.generation_record(
            category="upstream_outputs",
            stage="upstream",
            scope=generated_provenance.generation_scope(
                elections=["2026vic"]
            ),
            run=run_id,
            dependencies={
                "upstream_script":
                    generated_provenance.source_manifest_dependency(
                        "upstream_script", upstream_manifest, self.folder
                    )
            },
            outputs=generated_provenance.output_fingerprints(
                [self.output], self.folder
            ),
            random_seed=None,
        )
        generated_provenance.update_manifest(
            upstream_generated,
            {"upstream_outputs:2026vic": upstream_record},
            {run_id: run},
            path_base=".",
            description="Upstream generated data.",
        )
        downstream_record = generated_provenance.generation_record(
            category="test_outputs",
            stage="test_stage",
            scope=generated_provenance.generation_scope(
                elections=["2026vic"]
            ),
            run=run_id,
            dependencies={
                "generator_script":
                    generated_provenance.source_manifest_dependency(
                        "generator_script", self.source_manifest, self.folder
                    ),
                "upstream_outputs":
                    generated_provenance.generated_manifest_dependency(
                        "upstream_outputs",
                        upstream_generated,
                        ["upstream_outputs:2026vic"],
                        self.folder,
                    ),
            },
            outputs=generated_provenance.output_fingerprints(
                [self.output], self.folder
            ),
            random_seed=123,
        )
        generated_provenance.update_manifest(
            self.generated_manifest,
            {"test_outputs:2026vic": downstream_record},
            {run_id: run},
            path_base=".",
        )

        # First repair the upstream source metadata. It changes its record
        # fingerprint while leaving its output untouched.
        upstream_source.write_text("version = 1 # metadata\n", encoding="utf-8")
        source_provenance.record_change(
            upstream_manifest,
            "upstream_script",
            "Refresh upstream metadata.",
            "schema",
            "provenance-only",
            False,
            source_provenance._build_scope(all_scopes=True),
            provenance_upgrade="refresh-source-dependency-v1",
        )
        provenance_maintenance.maintain_record(
            upstream_generated, "upstream_outputs:2026vic"
        )
        self.assertTrue(
            generated_provenance.check_manifest(
                self.generated_manifest
            )["test_outputs:2026vic"]
        )

        self.source.write_text("version = 1 # metadata\n", encoding="utf-8")
        source_provenance.record_change(
            self.source_manifest,
            "generator_script",
            "Refresh downstream dependency metadata.",
            "schema",
            "provenance-only",
            False,
            source_provenance._build_scope(all_scopes=True),
            provenance_upgrade="refresh-direct-generated-dependencies-v1",
        )
        before = generated_provenance.load_manifest(
            self.generated_manifest
        )["records"]["test_outputs:2026vic"]

        applied = provenance_maintenance.maintain_record(
            self.generated_manifest, "test_outputs:2026vic"
        )
        after = generated_provenance.load_manifest(
            self.generated_manifest
        )["records"]["test_outputs:2026vic"]
        self.assertEqual(applied, 1)
        self.assertEqual(after["run"], before["run"])
        self.assertEqual(after["outputs"], before["outputs"])
        self.assertEqual(
            generated_provenance.check_manifest(
                self.generated_manifest
            )["test_outputs:2026vic"],
            [],
        )
        self.assertEqual(applied, 1)


if __name__ == "__main__":
    unittest.main()

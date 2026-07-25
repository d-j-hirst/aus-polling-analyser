import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import generated_provenance
import source_provenance


class GeneratedProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary_directory.name)
        self.data_directory = self.base / "Data"
        self.output_directory = self.base / "elections"
        self.data_directory.mkdir()
        self.output_directory.mkdir()

        self.rules_path = self.data_directory / "party-simplification.csv"
        self.rules_path.write_text("Original,Grouped\n", encoding="utf-8")
        self.source_manifest_path = self.data_directory / "provenance.json"
        source_provenance.initialize_manifest(
            self.source_manifest_path, "Test authored inputs."
        )
        source_provenance.add_category(
            self.source_manifest_path,
            "election_result_rules",
            "Test party simplification rules.",
            ["party-simplification.csv"],
        )

        self.cache_path = self.output_directory / "2025fed_results.pkl"
        self.cache_path.write_bytes(b"cached results")
        self.output_path = self.output_directory / "results_2025fed.csv"
        self.output_path.write_text("Election results\n", encoding="utf-8")
        self.generated_manifest_path = (
            self.output_directory / "generated-provenance.json"
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_generated_manifest_rejects_source_manifest_filename(self):
        with self.assertRaisesRegex(
            generated_provenance.GeneratedProvenanceError,
            "plain provenance.json is reserved",
        ):
            generated_provenance.update_manifest(
                self.output_directory / "provenance.json",
                {},
                {},
                description="Ambiguous generated provenance.",
            )

        with self.assertRaisesRegex(
            generated_provenance.GeneratedProvenanceError,
            "plain provenance.json is reserved",
        ):
            generated_provenance.load_manifest(
                self.output_directory / "provenance.json"
            )

    def _record(self, election="2025fed", output_path=None):
        output_path = output_path or self.output_path
        cache_path = self.output_directory / "{}_results.pkl".format(election)
        if not cache_path.exists():
            cache_path.write_bytes(
                "cached results for {}".format(election).encode("utf-8")
            )
        return generated_provenance.generation_record(
            category="election_result_exports",
            stage="export_election_results",
            scope=generated_provenance.generation_scope(
                elections=[election]
            ),
            run="test-run",
            dependencies={
                "election_result_rules":
                    generated_provenance.source_manifest_dependency(
                        "election_result_rules",
                        self.source_manifest_path,
                        self.base,
                    ),
            },
            outputs=generated_provenance.output_fingerprints(
                [cache_path, output_path], self.base
            ),
            random_seed=None,
        )

    def _write_manifest(self, records):
        return generated_provenance.update_manifest(
            self.generated_manifest_path,
            records,
            {
                "test-run": {
                    "generated_at_utc": "2026-01-01T00:00:00Z",
                    "command": ["python3", "election_store.py"],
                    "source_revision": {
                        "system": "git",
                        "revision": "a" * 40,
                        "dirty": False,
                    },
                    "environment": {
                        "python_version": "3.8.0",
                        "python_implementation": "CPython",
                        "platform": "test",
                    },
                }
            },
            path_base="..",
            description="Test generated election exports.",
        )

    def test_schema_is_draft_2020_12(self):
        with generated_provenance.SCHEMA_PATH.open(
            "r", encoding="utf-8"
        ) as schema_file:
            schema = json.load(schema_file)
        self.assertEqual(
            schema["$schema"],
            "https://json-schema.org/draft/2020-12/schema",
        )
        self.assertEqual(schema["properties"]["schema_version"]["const"], 1)

    def test_cross_drive_schema_reference_uses_file_uri(self):
        with mock.patch(
            "generated_provenance.os.path.relpath",
            side_effect=ValueError("different drives"),
        ):
            reference = generated_provenance._schema_reference(
                self.generated_manifest_path
            )
        self.assertEqual(
            reference,
            generated_provenance.SCHEMA_PATH.resolve().as_uri(),
        )

    def test_multiple_work_units_share_one_manifest(self):
        second_output = self.output_directory / "results_2026sa.csv"
        second_output.write_text("More election results\n", encoding="utf-8")
        records = {
            "election_result_exports:2025fed": self._record(),
            "election_result_exports:2026sa": self._record(
                election="2026sa", output_path=second_output
            ),
        }
        manifest = self._write_manifest(records)

        self.assertEqual(set(manifest["records"]), set(records))
        self.assertEqual(
            generated_provenance.check_manifest(
                self.generated_manifest_path
            ),
            {
                "election_result_exports:2025fed": [],
                "election_result_exports:2026sa": [],
            },
        )

    def test_check_manifest_can_limit_verification_to_selected_records(self):
        second_output = self.output_directory / "results_2026sa.csv"
        second_output.write_text("More election results\n", encoding="utf-8")
        self._write_manifest(
            {
                "election_result_exports:2025fed": self._record(),
                "election_result_exports:2026sa": self._record(
                    election="2026sa", output_path=second_output
                ),
            }
        )
        second_output.write_text("Changed later result\n", encoding="utf-8")

        self.assertEqual(
            generated_provenance.check_manifest(
                self.generated_manifest_path,
                record_keys=["election_result_exports:2025fed"],
            ),
            {"election_result_exports:2025fed": []},
        )

    def test_dependency_ignores_unselected_stale_records(self):
        second_output = self.output_directory / "results_2026sa.csv"
        second_output.write_text("More election results\n", encoding="utf-8")
        self._write_manifest(
            {
                "election_result_exports:2025fed": self._record(),
                "election_result_exports:2026sa": self._record(
                    election="2026sa", output_path=second_output
                ),
            }
        )
        second_output.write_text("Changed later result\n", encoding="utf-8")

        dependency = generated_provenance.generated_manifest_dependency(
            "election_result_exports",
            self.generated_manifest_path,
            ["election_result_exports:2025fed"],
            self.base,
        )

        self.assertEqual(
            dependency["records"],
            ["election_result_exports:2025fed"],
        )

    def test_updating_one_record_preserves_other_work_units(self):
        second_output = self.output_directory / "results_2026sa.csv"
        second_output.write_text("More election results\n", encoding="utf-8")
        self._write_manifest(
            {
                "election_result_exports:2025fed": self._record(),
                "election_result_exports:2026sa": self._record(
                    election="2026sa", output_path=second_output
                ),
            }
        )
        self.output_path.write_text("Updated election results\n", encoding="utf-8")
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )

        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        self.assertEqual(len(manifest["records"]), 2)
        self.assertEqual(
            generated_provenance.check_manifest(
                self.generated_manifest_path
            )["election_result_exports:2025fed"],
            [],
        )

    def test_changed_export_or_internal_cache_is_detected(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        self.output_path.write_text("Changed output\n", encoding="utf-8")
        self.cache_path.write_bytes(b"changed cache")

        issues = generated_provenance.check_manifest(
            self.generated_manifest_path
        )["election_result_exports:2025fed"]
        self.assertIn("changed output elections/results_2025fed.csv", issues)
        self.assertIn(
            "changed output elections/2025fed_results.pkl", issues
        )

    def test_timestamp_only_output_change_is_not_stale(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        stat = self.output_path.stat()
        os.utime(
            self.output_path,
            ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000),
        )

        self.assertEqual(
            generated_provenance.check_manifest(
                self.generated_manifest_path
            )["election_result_exports:2025fed"],
            [],
        )

    def test_scoped_source_change_only_stales_matching_work_unit(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        self.rules_path.write_text("Original,New Group\n", encoding="utf-8")
        source_provenance.record_change(
            self.source_manifest_path,
            "election_result_rules",
            "Changed rules for one later election.",
            "correction",
            "minor",
            True,
            source_provenance._build_scope(elections=["2026sa"]),
        )
        self.assertEqual(
            generated_provenance.check_manifest(
                self.generated_manifest_path
            )["election_result_exports:2025fed"],
            [],
        )

        self.rules_path.write_text("Original,Newest Group\n", encoding="utf-8")
        source_provenance.record_change(
            self.source_manifest_path,
            "election_result_rules",
            "Changed rules for the generated election.",
            "correction",
            "minor",
            True,
            source_provenance._build_scope(elections=["2025fed"]),
        )
        issues = generated_provenance.check_manifest(
            self.generated_manifest_path
        )["election_result_exports:2025fed"]
        self.assertEqual(
            issues,
            [
                "new semantic dependency revision "
                "election_result_rules (3)"
            ],
        )

    def test_generated_manifest_dependency_propagates_staleness(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        downstream_directory = self.base / "Seat Statistics"
        downstream_directory.mkdir()
        downstream_output = downstream_directory / "statistics.csv"
        downstream_output.write_text("statistics\n", encoding="utf-8")
        dependency = generated_provenance.generated_manifest_dependency(
            "election_result_exports",
            self.generated_manifest_path,
            ["election_result_exports:2025fed"],
            self.base,
        )
        downstream_record = generated_provenance.generation_record(
            category="seat_statistics",
            stage="analyse_elections",
            scope=generated_provenance.generation_scope(all_scopes=True),
            run="downstream-run",
            dependencies={"election_result_exports": dependency},
            outputs=generated_provenance.output_fingerprints(
                [downstream_output], self.base
            ),
            random_seed=None,
        )
        downstream_manifest = (
            downstream_directory / "generated-provenance.json"
        )
        generated_provenance.update_manifest(
            downstream_manifest,
            {"seat_statistics:all": downstream_record},
            {
                "downstream-run": {
                    "generated_at_utc": "2026-01-02T00:00:00Z",
                    "command": ["python3", "election_analysis.py"],
                    "source_revision": {
                        "system": "git",
                        "revision": "b" * 40,
                        "dirty": False,
                    },
                    "environment": {
                        "python_version": "3.8.0",
                        "python_implementation": "CPython",
                        "platform": "test",
                    },
                }
            },
            path_base="..",
            description="Test downstream generated data.",
        )
        self.assertEqual(
            generated_provenance.check_manifest(downstream_manifest)[
                "seat_statistics:all"
            ],
            [],
        )

        self.cache_path.write_bytes(b"changed cache")
        issues = generated_provenance.check_manifest(downstream_manifest)[
            "seat_statistics:all"
        ]
        self.assertTrue(
            any(
                issue.startswith(
                    "stale generated dependency election_result_exports"
                )
                for issue in issues
            )
        )

    def test_early_v1_dependency_shape_is_upgraded_in_memory(self):
        manifest = self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        dependency = manifest["records"][
            "election_result_exports:2025fed"
        ]["dependencies"]["election_result_rules"]
        del dependency["records"]
        del manifest["records"]["election_result_exports:2025fed"]["status"]
        del manifest["runs"]["test-run"]["environment"]["packages"]
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        loaded = generated_provenance.load_manifest(
            self.generated_manifest_path
        )

        self.assertEqual(
            loaded["records"]["election_result_exports:2025fed"][
                "dependencies"
            ]["election_result_rules"]["records"],
            [],
        )
        self.assertEqual(
            loaded["records"]["election_result_exports:2025fed"]["status"],
            "generated",
        )
        self.assertEqual(
            loaded["runs"]["test-run"]["environment"]["packages"], {}
        )

    def test_legacy_record_is_preserved_but_reported_unknown(self):
        record = self._record()
        record["status"] = "legacy"
        record["dependencies"] = {}
        record["random_seed"] = None
        self._write_manifest(
            {"election_result_exports:2025fed": record}
        )

        issues = generated_provenance.check_manifest(
            self.generated_manifest_path
        )["election_result_exports:2025fed"]

        self.assertEqual(
            issues,
            [
                "legacy provenance baseline; generation inputs unknown"
            ],
        )


if __name__ == "__main__":
    unittest.main()

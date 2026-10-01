import json
import os
import tempfile
import threading
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

    def test_manifest_write_lock_waits_only_for_same_manifest(self):
        first_entered = threading.Event()
        release_first = threading.Event()
        second_entered = threading.Event()

        def hold_first_lock():
            with generated_provenance._ManifestWriteLock(
                self.generated_manifest_path
            ):
                first_entered.set()
                release_first.wait(timeout=2)

        def wait_for_same_manifest():
            first_entered.wait(timeout=2)
            with generated_provenance._ManifestWriteLock(
                self.generated_manifest_path
            ):
                second_entered.set()

        first = threading.Thread(target=hold_first_lock)
        second = threading.Thread(target=wait_for_same_manifest)
        first.start()
        second.start()
        self.assertTrue(first_entered.wait(timeout=2))
        self.assertFalse(second_entered.wait(timeout=0.1))
        with generated_provenance._ManifestWriteLock(
            self.output_directory / "other-generated-provenance.json"
        ):
            self.assertFalse(second_entered.is_set())
        release_first.set()
        first.join(timeout=2)
        second.join(timeout=2)
        self.assertFalse(first.is_alive())
        self.assertFalse(second.is_alive())
        self.assertTrue(second_entered.is_set())

    def test_manifest_updates_use_the_write_lock(self):
        events = []

        class TrackingLock:
            def __init__(self, path):
                events.append(("create", Path(path)))

            def __enter__(self):
                events.append(("enter", None))

            def __exit__(self, exception_type, exception, traceback):
                events.append(("exit", exception_type))

        with mock.patch.object(
            generated_provenance,
            "_ManifestWriteLock",
            TrackingLock,
        ):
            self._write_manifest({"test_outputs:2025fed": self._record()})

        self.assertEqual(
            events,
            [
                ("create", self.generated_manifest_path),
                ("enter", None),
                ("exit", None),
            ],
        )

    def test_conditional_update_preserves_a_concurrent_record(self):
        record_key = "test_outputs:2025fed"
        self._write_manifest({record_key: self._record()})
        original = generated_provenance.load_manifest(
            self.generated_manifest_path
        )["records"][record_key]
        newer = json.loads(json.dumps(original))
        newer["random_seed"] = 456
        self._write_manifest({record_key: newer})
        stale_replacement = json.loads(json.dumps(original))
        stale_replacement["random_seed"] = 789

        with self.assertRaises(
            generated_provenance.ConcurrentManifestUpdate
        ):
            generated_provenance.update_manifest(
                self.generated_manifest_path,
                {record_key: stale_replacement},
                {},
                path_base="..",
                expected_records={record_key: original},
            )

        current = generated_provenance.load_manifest(
            self.generated_manifest_path
        )["records"][record_key]
        self.assertEqual(current["random_seed"], 456)

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

    def test_dependency_allows_provenance_only_upgrades(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        self.rules_path.write_text(
            "Original,Grouped\n# metadata only\n", encoding="utf-8"
        )
        source_provenance.record_change(
            self.source_manifest_path,
            "election_result_rules",
            "Document a metadata-only source update.",
            "formatting",
            "provenance-only",
            False,
            source_provenance._build_scope(all_scopes=True),
            provenance_upgrade="no-generated-metadata-change-v1",
        )

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

    def test_provenance_only_upgrades_do_not_stale_downstream_records(self):
        record_key = "election_result_exports:2025fed"
        self._write_manifest({record_key: self._record()})
        dependency = generated_provenance.generated_manifest_dependency(
            "election_result_exports",
            self.generated_manifest_path,
            [record_key],
            self.base,
        )
        downstream_directory = self.base / "Seat Statistics"
        downstream_directory.mkdir()
        downstream_output = downstream_directory / "statistics.csv"
        downstream_output.write_text("statistics\n", encoding="utf-8")
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
            description="Test downstream output.",
        )
        self.rules_path.write_text(
            "Original,Grouped\n# metadata only\n", encoding="utf-8"
        )
        source_provenance.record_change(
            self.source_manifest_path,
            "election_result_rules",
            "Document a metadata-only source update.",
            "formatting",
            "provenance-only",
            False,
            source_provenance._build_scope(all_scopes=True),
            provenance_upgrade="no-generated-metadata-change-v1",
        )

        self.assertEqual(
            generated_provenance.check_manifest(downstream_manifest),
            {"seat_statistics:all": []},
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

    def test_recorded_output_metadata_avoids_rehashing_file_dependency(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        downstream_output = self.output_directory / "downstream.csv"
        downstream_output.write_text("downstream\n", encoding="utf-8")
        downstream_record = generated_provenance.generation_record(
            category="downstream",
            stage="test_downstream",
            scope=generated_provenance.generation_scope(
                elections=["2025fed"]
            ),
            run="test-run",
            dependencies={
                "election_result_exports":
                    generated_provenance.file_dependency(
                        "election_result_exports",
                        [self.output_path],
                        self.base,
                    ),
            },
            outputs=generated_provenance.output_fingerprints(
                [downstream_output], self.base
            ),
            random_seed=None,
        )
        context = generated_provenance.ManifestCheckContext()
        context.load_manifest(self.generated_manifest_path)

        with mock.patch(
            "generated_provenance._hash_file",
            side_effect=AssertionError("unchanged dependency was rehashed"),
        ):
            issues = generated_provenance.check_record(
                downstream_record,
                self.base,
                check_context=context,
            )

        self.assertEqual(issues, [])

    def test_changed_dependency_metadata_still_uses_content_hash(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        dependency = generated_provenance.file_dependency(
            "election_result_exports",
            [self.output_path],
            self.base,
        )
        context = generated_provenance.ManifestCheckContext()
        context.load_manifest(self.generated_manifest_path)
        stat = self.output_path.stat()
        os.utime(
            self.output_path,
            ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000),
        )

        with mock.patch(
            "generated_provenance._hash_file",
            wraps=generated_provenance._hash_file,
        ) as hash_file:
            current = generated_provenance.file_dependency(
                "election_result_exports",
                [self.output_path],
                self.base,
                fingerprint_cache={
                    str(self.output_path):
                        context.fingerprint_file(self.output_path)
                },
            )

        self.assertEqual(current["digest"], dependency["digest"])
        hash_file.assert_called_once_with(self.output_path)

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

        upstream_manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        upstream_manifest["records"][
            "election_result_exports:2025fed"
        ]["random_seed"] = 123
        self.generated_manifest_path.write_text(
            json.dumps(upstream_manifest), encoding="utf-8"
        )
        issues = generated_provenance.check_manifest(downstream_manifest)[
            "seat_statistics:all"
        ]
        self.assertIn(
            "changed dependency election_result_exports", issues
        )

    def test_non_invalidating_generated_record_preserves_lineage(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        record_key = "election_result_exports:2025fed"
        dependency = generated_provenance.generated_manifest_dependency(
            "election_result_exports",
            self.generated_manifest_path,
            [record_key],
            self.base,
            non_invalidating_records=[record_key],
        )
        self.assertEqual(dependency["records"], [record_key])
        self.assertEqual(
            dependency["non_invalidating_records"], [record_key]
        )

        downstream_output = self.base / "downstream.csv"
        downstream_output.write_text("downstream\n", encoding="utf-8")
        downstream_manifest = (
            self.base / "downstream-generated-provenance.json"
        )
        downstream_record = generated_provenance.generation_record(
            category="downstream",
            stage="test",
            scope=generated_provenance.generation_scope(all_scopes=True),
            run="downstream-run",
            dependencies={"election_result_exports": dependency},
            outputs=generated_provenance.output_fingerprints(
                [downstream_output], self.base
            ),
            random_seed=None,
        )
        generated_provenance.update_manifest(
            downstream_manifest,
            {"downstream:all": downstream_record},
            {
                "downstream-run": {
                    "generated_at_utc": "2026-01-02T00:00:00Z",
                    "command": ["python3", "test.py"],
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
            path_base=".",
            description="Test non-invalidating dependency.",
        )

        self.output_path.write_text("changed result\n", encoding="utf-8")
        self.assertEqual(
            generated_provenance.check_manifest(downstream_manifest)[
                "downstream:all"
            ],
            [],
        )

    def test_non_invalidating_records_must_be_dependencies(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        with self.assertRaisesRegex(
            generated_provenance.GeneratedProvenanceError,
            "are not dependencies",
        ):
            generated_provenance.generated_manifest_dependency(
                "election_result_exports",
                self.generated_manifest_path,
                ["election_result_exports:2025fed"],
                self.base,
                non_invalidating_records=[
                    "election_result_exports:2026sa"
                ],
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

    def test_resolve_path_uses_abspath_on_posix(self):
        context = generated_provenance.ManifestCheckContext()
        with mock.patch("generated_provenance.os.name", "posix"), mock.patch(
            "generated_provenance.Path.resolve",
            side_effect=AssertionError(
                "POSIX resolve_path must avoid resolve()"
            ),
        ):
            resolved = context.resolve_path(self.output_path)
            expected = os.path.abspath(str(self.output_path))
            self.assertEqual(
                os.path.normcase(str(resolved)),
                os.path.normcase(expected),
            )
            self.assertIs(context.resolve_path(self.output_path), resolved)

    def test_load_manifest_primes_expected_outputs_for_selected_records_only(
        self,
    ):
        sa_output = self.output_directory / "results_2026sa.csv"
        sa_output.write_text("South Australia results\n", encoding="utf-8")
        self._write_manifest(
            {
                "election_result_exports:2025fed": self._record(),
                "election_result_exports:2026sa": self._record(
                    "2026sa", sa_output
                ),
            }
        )
        context = generated_provenance.ManifestCheckContext()
        context.load_manifest(self.generated_manifest_path)
        self.assertEqual(context.expected_output_fingerprints, {})

        context.prime_expected_outputs(
            self.generated_manifest_path,
            ["election_result_exports:2025fed"],
        )
        primed_keys = set(context.expected_output_fingerprints)
        self.assertEqual(len(primed_keys), 2)
        self.assertIn(context.path_key(self.output_path), primed_keys)
        self.assertIn(context.path_key(self.cache_path), primed_keys)
        self.assertNotIn(context.path_key(sa_output), primed_keys)

    def test_check_manifest_short_circuits_when_records_are_cached(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        context = generated_provenance.ManifestCheckContext()
        first = generated_provenance.check_manifest(
            self.generated_manifest_path,
            _context=context,
        )
        with mock.patch.object(
            context,
            "prime_expected_outputs",
            side_effect=AssertionError("cached check_manifest re-primed"),
        ), mock.patch(
            "generated_provenance.check_record",
            side_effect=AssertionError("cached check_manifest rechecked"),
        ):
            second = generated_provenance.check_manifest(
                self.generated_manifest_path,
                record_keys=["election_result_exports:2025fed"],
                _context=context,
            )
        self.assertEqual(second, first)

    def test_load_manifest_defers_output_owner_indexing(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        context = generated_provenance.ManifestCheckContext()
        context.load_manifest(self.generated_manifest_path)
        self.assertEqual(context.output_owners, {})
        self.assertEqual(context.indexed_manifests, set())

        context.prime_expected_outputs(
            self.generated_manifest_path,
            ["election_result_exports:2025fed"],
        )
        output_key = context.path_key(self.output_path)
        self.assertIn(output_key, context.output_owners)

    def test_load_manifest_indexes_output_owners_for_file_dep_priming(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        context = generated_provenance.ManifestCheckContext()
        context.load_manifest(self.generated_manifest_path)
        output_key = context.path_key(self.output_path)
        self.assertNotIn(output_key, context.output_owners)

        downstream_manifest = (
            self.output_directory / "downstream-generated-provenance.json"
        )
        downstream_output = self.output_directory / "downstream.csv"
        downstream_output.write_text("downstream\n", encoding="utf-8")
        generated_provenance.update_manifest(
            downstream_manifest,
            {
                "downstream:2025fed": generated_provenance.generation_record(
                    category="downstream",
                    stage="test_downstream",
                    scope=generated_provenance.generation_scope(
                        elections=["2025fed"]
                    ),
                    run="test-run",
                    dependencies={
                        "election_result_exports":
                            generated_provenance.file_dependency(
                                "election_result_exports",
                                [self.output_path],
                                self.base,
                            ),
                    },
                    outputs=generated_provenance.output_fingerprints(
                        [downstream_output], self.base
                    ),
                    random_seed=None,
                )
            },
            {
                "test-run": {
                    "generated_at_utc": "2026-01-01T00:00:00Z",
                    "command": ["python3", "downstream.py"],
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
            description="Downstream test outputs.",
        )
        context.load_manifest(downstream_manifest)
        context.prime_expected_file_dependencies(
            downstream_manifest, ["downstream:2025fed"]
        )
        self.assertIn(output_key, context.output_owners)
        self.assertEqual(
            context.expected_output_fingerprints[output_key],
            context.output_owners[output_key],
        )

    def test_generated_records_digest_is_cached_on_context(self):
        self._write_manifest(
            {"election_result_exports:2025fed": self._record()}
        )
        context = generated_provenance.ManifestCheckContext()
        manifest = context.load_manifest(self.generated_manifest_path)
        record_keys = ["election_result_exports:2025fed"]
        with mock.patch(
            "generated_provenance._generated_records_digest",
            wraps=generated_provenance._generated_records_digest,
        ) as digest:
            first = context.generated_records_digest(
                self.generated_manifest_path, manifest, record_keys
            )
            second = context.generated_records_digest(
                self.generated_manifest_path, manifest, record_keys
            )
        self.assertEqual(first, second)
        digest.assert_called_once()

    def test_digest_fragments_match_generated_records_digest(self):
        sa_output = self.output_directory / "results_2026sa.csv"
        sa_output.write_text("South Australia results\n", encoding="utf-8")
        self._write_manifest(
            {
                "election_result_exports:2025fed": self._record(),
                "election_result_exports:2026sa": self._record(
                    "2026sa", sa_output
                ),
            }
        )
        context = generated_provenance.ManifestCheckContext()
        manifest = context.load_manifest(self.generated_manifest_path)
        record_keys = [
            "election_result_exports:2025fed",
            "election_result_exports:2026sa",
        ]
        expected = generated_provenance._generated_records_digest(
            manifest, record_keys
        )
        assembled = context.generated_records_digest(
            self.generated_manifest_path, manifest, record_keys
        )
        self.assertEqual(assembled, expected)
        subset = ["election_result_exports:2026sa"]
        self.assertEqual(
            context.generated_records_digest(
                self.generated_manifest_path, manifest, subset
            ),
            generated_provenance._generated_records_digest(manifest, subset),
        )


if __name__ == "__main__":
    unittest.main()

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import pipeline_registry
import source_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
REPOSITORY_MANIFESTS = [
    ANALYSIS_DIRECTORY / "provenance.json",
    ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    ANALYSIS_DIRECTORY / "Regional" / "provenance.json",
    ANALYSIS_DIRECTORY / "Models" / "provenance.json",
    ANALYSIS_DIRECTORY / "seats" / "provenance.json",
    ANALYSIS_DIRECTORY / "Federal-State" / "provenance.json",
]


class SourceProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary_directory.name)
        self.manifest_path = self.folder / "provenance.json"
        self.source_path = self.folder / "polls.csv"
        self.source_path.write_text("date,value\n2026-01-01,50\n", encoding="utf-8")
        source_provenance.initialize_manifest(
            self.manifest_path, "Test source data."
        )
        source_provenance.add_category(
            self.manifest_path,
            "raw_polls",
            "Raw polling observations.",
            ["*.csv"],
            summary="Imported test baseline.",
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_schema_is_valid_json_with_expected_version(self):
        with source_provenance.SCHEMA_PATH.open(
            "r", encoding="utf-8"
        ) as schema_file:
            schema = json.load(schema_file)

        self.assertEqual(
            schema["$schema"],
            "https://json-schema.org/draft/2020-12/schema",
        )
        self.assertEqual(schema["properties"]["schema_version"]["const"], 1)

    def test_baseline_manifest_is_current(self):
        manifest = source_provenance.load_manifest(self.manifest_path)
        self.assertEqual(
            manifest["categories"]["raw_polls"]["semantic_revision"], 1
        )
        comparison = source_provenance.check_manifest(self.manifest_path)[
            "raw_polls"
        ]
        self.assertEqual(
            comparison,
            {"added": [], "removed": [], "modified": [], "touched": []},
        )

    def test_line_endings_do_not_change_source_fingerprint(self):
        self.source_path.write_bytes(
            b"date,value\r\n2026-01-01,50\r\n"
        )
        comparison = source_provenance.check_manifest(self.manifest_path)[
            "raw_polls"
        ]
        self.assertEqual(comparison["modified"], [])

    def test_cross_drive_schema_reference_uses_file_uri(self):
        with mock.patch(
            "source_provenance.os.path.relpath",
            side_effect=ValueError("different drives"),
        ):
            reference = source_provenance._schema_reference(
                self.manifest_path
            )
        self.assertEqual(
            reference,
            source_provenance.SCHEMA_PATH.resolve().as_uri(),
        )

    def test_output_affecting_change_increments_semantic_revision(self):
        self.source_path.write_text(
            "date,value\n2026-01-01,51\n", encoding="utf-8"
        )
        scope = source_provenance._build_scope(elections=["2028fed"])
        event, comparison = source_provenance.record_change(
            self.manifest_path,
            "raw_polls",
            "Corrected one historical poll.",
            "correction",
            "minor",
            True,
            scope,
        )

        self.assertEqual(comparison["modified"], ["polls.csv"])
        self.assertEqual(event["semantic_revision"], 2)
        manifest = source_provenance.load_manifest(self.manifest_path)
        self.assertEqual(
            manifest["categories"]["raw_polls"]["semantic_revision"], 2
        )
        self.assertEqual(
            manifest["categories"]["raw_polls"]["events"][-1]["scope"][
                "elections"
            ],
            ["2028fed"],
        )

    def test_timestamp_only_change_does_not_require_semantic_revision(self):
        original_stat = self.source_path.stat()
        os.utime(
            self.source_path,
            (original_stat.st_atime + 5, original_stat.st_mtime + 5),
        )
        comparison = source_provenance.check_manifest(self.manifest_path)[
            "raw_polls"
        ]
        self.assertEqual(comparison["modified"], [])
        self.assertEqual(comparison["touched"], ["polls.csv"])

        event, _ = source_provenance.record_change(
            self.manifest_path,
            "raw_polls",
            "File timestamp changed without a content change.",
            "formatting",
            "negligible",
            False,
            source_provenance._build_scope(all_scopes=True),
        )
        self.assertEqual(event["semantic_revision"], 1)

    def test_new_matching_file_is_an_unrecorded_content_change(self):
        (self.folder / "new-polls.csv").write_text(
            "date,value\n2026-01-02,52\n", encoding="utf-8"
        )
        comparison = source_provenance.check_manifest(self.manifest_path)[
            "raw_polls"
        ]
        self.assertEqual(comparison["added"], ["new-polls.csv"])

    def test_failed_assessment_does_not_modify_manifest(self):
        self.source_path.write_text(
            "date,value\n2026-01-01,51\n", encoding="utf-8"
        )
        original_manifest = self.manifest_path.read_bytes()

        with self.assertRaisesRegex(
            source_provenance.ProvenanceError,
            "must have negligible magnitude",
        ):
            source_provenance.record_change(
                self.manifest_path,
                "raw_polls",
                "Invalid impact assessment.",
                "correction",
                "minor",
                False,
                source_provenance._build_scope(all_scopes=True),
            )

        self.assertEqual(self.manifest_path.read_bytes(), original_manifest)

    def test_output_affecting_scope_must_be_explicit(self):
        with self.assertRaisesRegex(
            source_provenance.ProvenanceError,
            "must select at least one",
        ):
            source_provenance._build_scope()

    def test_scoped_impact_requires_overlap_in_each_known_dimension(self):
        change_scope = source_provenance._build_scope(
            elections=["2028fed"],
            parties=["ONP FP"],
            stages=["generate_poll_trends"],
        )
        matching_target = source_provenance._build_scope(
            elections=["2028FED"],
            parties=["onp fp"],
            stages=["GENERATE_POLL_TRENDS"],
        )
        self.assertTrue(
            source_provenance.scope_affects_target(
                change_scope, matching_target
            )
        )

        for target_scope in (
            source_provenance._build_scope(
                elections=["2026vic"],
                parties=["ONP FP"],
                stages=["generate_poll_trends"],
            ),
            source_provenance._build_scope(
                elections=["2028fed"],
                parties=["ALP FP"],
                stages=["generate_poll_trends"],
            ),
            source_provenance._build_scope(
                elections=["2028fed"],
                parties=["ONP FP"],
                stages=["generate_trend_adjustments"],
            ),
        ):
            self.assertFalse(
                source_provenance.scope_affects_target(
                    change_scope, target_scope
                )
            )

    def test_scoped_impact_is_conservative_for_unknown_target_dimensions(self):
        change_scope = source_provenance._build_scope(
            elections=["2028fed"],
            parties=["ONP FP"],
        )
        election_level_target = source_provenance._build_scope(
            elections=["2028fed"]
        )
        self.assertTrue(
            source_provenance.scope_affects_target(
                change_scope, election_level_target
            )
        )
        self.assertTrue(
            source_provenance.scope_affects_target(
                change_scope,
                source_provenance._build_scope(all_scopes=True),
            )
        )

    def test_semantic_events_match_after_recorded_revision(self):
        self.source_path.write_text(
            "date,value\n2026-01-01,51\n", encoding="utf-8"
        )
        source_provenance.record_change(
            self.manifest_path,
            "raw_polls",
            "Corrected a Victorian poll.",
            "correction",
            "minor",
            True,
            source_provenance._build_scope(elections=["2026vic"]),
        )
        self.source_path.write_text(
            "date,value\n2026-01-01,52\n", encoding="utf-8"
        )
        second_event, _ = source_provenance.record_change(
            self.manifest_path,
            "raw_polls",
            "Corrected a federal poll.",
            "correction",
            "minor",
            True,
            source_provenance._build_scope(
                elections=["2028fed"],
                parties=["ONP FP"],
            ),
        )
        category = source_provenance.load_manifest(self.manifest_path)[
            "categories"
        ]["raw_polls"]

        events = source_provenance.semantic_events_affecting(
            category,
            after_revision=1,
            target_scope=source_provenance._build_scope(
                elections=["2028fed"],
                parties=["ONP FP"],
            ),
        )
        self.assertEqual([event["id"] for event in events], [second_event["id"]])
        self.assertEqual(
            source_provenance.semantic_events_affecting(
                category,
                after_revision=category["semantic_revision"],
                target_scope=source_provenance._build_scope(
                    elections=["2028fed"]
                ),
            ),
            [],
        )

    def test_non_output_change_does_not_create_semantic_impact(self):
        original_stat = self.source_path.stat()
        os.utime(
            self.source_path,
            (original_stat.st_atime + 5, original_stat.st_mtime + 5),
        )
        source_provenance.record_change(
            self.manifest_path,
            "raw_polls",
            "Timestamp-only source refresh.",
            "formatting",
            "negligible",
            False,
            source_provenance._build_scope(all_scopes=True),
        )
        category = source_provenance.load_manifest(self.manifest_path)[
            "categories"
        ]["raw_polls"]
        self.assertEqual(
            source_provenance.semantic_events_affecting(
                category,
                after_revision=1,
                target_scope=source_provenance._build_scope(all_scopes=True),
            ),
            [],
        )


class RepositorySourceProvenanceTests(unittest.TestCase):
    def test_generated_manifests_are_ignored_but_source_manifests_are_not(self):
        repository_directory = ANALYSIS_DIRECTORY.parent
        generated_paths = (
            "analysis/example/generated-provenance.json",
            "analysis/example/pure-generated-provenance.json",
        )
        for path in generated_paths:
            result = subprocess.run(
                ["git", "check-ignore", "--quiet", "--no-index", path],
                cwd=repository_directory,
                check=False,
            )
            self.assertEqual(result.returncode, 0, "{} is not ignored".format(path))

        source_result = subprocess.run(
            [
                "git",
                "check-ignore",
                "--quiet",
                "--no-index",
                "analysis/example/provenance.json",
            ],
            cwd=repository_directory,
            check=False,
        )
        self.assertEqual(
            source_result.returncode,
            1,
            "plain source provenance must remain eligible for Git",
        )

    def test_authored_registry_categories_have_current_baselines(self):
        registry = pipeline_registry.load_registry()
        expected_categories = {
            category_id
            for category_id, category in registry["categories"].items()
            if category["kind"] in {"authored", "code"}
        }
        recorded_categories = set()

        for manifest_path in REPOSITORY_MANIFESTS:
            manifest = source_provenance.load_manifest(manifest_path)
            overlap = recorded_categories & set(manifest["categories"])
            self.assertFalse(
                overlap,
                "tracked categories occur in multiple manifests: {}".format(
                    ", ".join(sorted(overlap))
                ),
            )
            recorded_categories.update(manifest["categories"])

            comparisons = source_provenance.check_manifest(manifest_path)
            for category_id, comparison in comparisons.items():
                self.assertEqual(
                    comparison["added"],
                    [],
                    "{} has unrecorded files".format(category_id),
                )
                self.assertEqual(
                    comparison["removed"],
                    [],
                    "{} has missing recorded files".format(category_id),
                )
                self.assertEqual(
                    comparison["modified"],
                    [],
                    "{} has unrecorded content changes".format(category_id),
                )

        self.assertEqual(recorded_categories, expected_categories)


if __name__ == "__main__":
    unittest.main()

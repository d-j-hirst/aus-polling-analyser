import json
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

import analysis_provenance
import generated_provenance
import source_provenance


class AnalysisProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary_directory.name)
        self.script_path = self.base / "election_store.py"
        self.script_path.write_text("print('original')\n", encoding="utf-8")
        self.source_manifest_path = self.base / "provenance.json"
        source_provenance.initialize_manifest(
            self.source_manifest_path, "Test tracked code."
        )
        source_provenance.add_category(
            self.source_manifest_path,
            "election_store_script",
            "Test election export script.",
            ["election_store.py"],
        )

        self.output_directory = self.base / "elections"
        self.output_directory.mkdir()
        self.output_path = self.output_directory / "results_2025fed.csv"
        self.output_path.write_text("Election results\n", encoding="utf-8")
        self.generated_manifest_path = (
            self.output_directory / "generated-provenance.json"
        )
        dependency = generated_provenance.source_manifest_dependency(
            "election_store_script",
            self.source_manifest_path,
            self.base,
        )
        record = generated_provenance.generation_record(
            category="election_result_exports",
            stage="export_election_results",
            scope=generated_provenance.generation_scope(
                elections=["2025fed"]
            ),
            run="test-run",
            dependencies={"election_store_script": dependency},
            outputs=generated_provenance.output_fingerprints(
                [self.output_path], self.base
            ),
            random_seed=None,
        )
        generated_provenance.update_manifest(
            self.generated_manifest_path,
            {"election_result_exports:2025fed": record},
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
            description="Test generated data.",
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def _audit(self):
        return analysis_provenance.audit_repository(
            source_manifest_paths=[self.source_manifest_path],
            generated_manifest_paths=[self.generated_manifest_path],
        )

    def test_unregistered_script_change_is_reported(self):
        self.script_path.write_text("print('changed')\n", encoding="utf-8")

        result = self._audit()

        self.assertTrue(
            any("unregistered modified" in issue for issue in result["issues"])
        )

    def test_negligible_script_change_permits_existing_output(self):
        self.script_path.write_text("print('changed')\n", encoding="utf-8")
        analysis_provenance.register_changes(
            [self.script_path],
            "Comment-only change.",
            "negligible",
            source_manifest_paths=[self.source_manifest_path],
        )

        self.assertEqual(self._audit()["issues"], [])

    def test_machine_status_reports_current_and_altered_work_units(self):
        current = self._audit()
        self.assertEqual(current["work_units"][0]["status"], "current")
        self.assertFalse(current["summary"]["has_blockers"])

        self.output_path.write_text(
            "Manually altered results\n", encoding="utf-8"
        )
        altered = self._audit()
        self.assertEqual(altered["work_units"][0]["status"], "altered")
        self.assertTrue(altered["work_units"][0]["blocking"])
        self.assertTrue(altered["summary"]["has_blockers"])
        self.assertEqual(
            altered["work_units"][0]["issues"][0]["code"],
            "altered_output",
        )

    def test_machine_status_reports_unregistered_sources_as_blocking(self):
        self.script_path.write_text("print('changed')\n", encoding="utf-8")

        result = self._audit()

        self.assertEqual(
            result["source_issues"][0]["code"],
            "unregistered_source_change",
        )
        self.assertEqual(result["source_issues"][0]["status"], "blocked")
        self.assertTrue(result["summary"]["has_blockers"])

    def test_audit_json_converts_sets_to_stable_lists(self):
        result = self._audit()
        result["impacts"]["immediate"]["cpp_stan_model"].add(
            "poll_trend_outputs"
        )

        decoded = json.loads(analysis_provenance.audit_json(result))

        self.assertEqual(
            decoded["impacts"]["immediate"]["cpp_stan_model"],
            ["poll_trend_outputs"],
        )

    def test_audit_cli_can_emit_json(self):
        result = self._audit()
        output = StringIO()
        with mock.patch.object(
            analysis_provenance,
            "audit_repository",
            return_value=result,
        ), redirect_stdout(output):
            return_code = analysis_provenance.main(
                ["audit", "--election", "2025fed", "--format", "json"]
            )

        self.assertEqual(return_code, 0)
        decoded = json.loads(output.getvalue())
        self.assertEqual(decoded["target_elections"], [])
        self.assertIn("work_units", decoded)

    def test_material_script_change_stales_existing_output(self):
        self.script_path.write_text("print('changed')\n", encoding="utf-8")
        analysis_provenance.register_changes(
            [self.script_path],
            "Changed export behaviour.",
            "material",
            source_manifest_paths=[self.source_manifest_path],
        )

        result = self._audit()
        self.assertIn(
            "Changed export behaviour. [material; methodology]",
            result["root_causes"]["election_store_script"],
        )
        self.assertIn(
            "election_result_exports",
            result["impacts"]["immediate"]["cpp_seat_simulation"],
        )

    def test_provenance_only_change_requires_metadata_not_data(self):
        self.script_path.write_text(
            "print('metadata only')\n", encoding="utf-8"
        )
        analysis_provenance.register_changes(
            [self.script_path],
            "Changed generated provenance bookkeeping.",
            "provenance-only",
            provenance_upgrade="refresh-source-dependency-v1",
            source_manifest_paths=[self.source_manifest_path],
        )

        result = self._audit()
        matching = [
            work_unit
            for work_unit in result["work_units"]
            if work_unit["record_key"] == "election_result_exports:2025fed"
        ]
        self.assertEqual(matching[0]["status"], "provenance-stale")
        self.assertEqual(len(result["provenance_maintenance"]), 1)
        self.assertEqual(result["impacts"]["immediate"], {})

    def test_terminal_impacts_separate_calibration_paths(self):
        registry = {
            "stages": [
                {
                    "id": "calibrate_pollsters",
                    "inputs": ["raw_polls"],
                    "outputs": ["calibration"],
                },
                {
                    "id": "normal_trend",
                    "inputs": ["raw_polls", "calibration"],
                    "outputs": ["poll_trend"],
                },
            ],
            "consumers": [
                {
                    "id": "cpp_stan_model",
                    "inputs": ["poll_trend"],
                }
            ],
        }

        impacts = analysis_provenance._terminal_impacts(
            {"raw_polls"}, registry
        )

        self.assertEqual(
            impacts["immediate"]["cpp_stan_model"], {"poll_trend"}
        )
        self.assertEqual(
            impacts["calibration"]["cpp_stan_model"], {"poll_trend"}
        )
        self.assertNotIn(
            "cpp_stan_model", impacts["calibration_only"]
        )

    def test_cutoff_stage_is_classified_as_slow_calibration_work(self):
        self.assertIn(
            "generate_cutoff_poll_trends",
            analysis_provenance.CALIBRATION_STAGES,
        )

    def test_missing_required_regional_work_unit_is_reported(self):
        work_unit = (
            "regional_swing_deviations:2027nsw:ONP FP"
        )
        with mock.patch.object(
            analysis_provenance.region_model_provenance,
            "MANIFEST_PATH",
            self.generated_manifest_path,
        ), mock.patch.object(
            analysis_provenance.region_model_provenance,
            "required_work_units",
            return_value={work_unit: {}},
        ):
            result = self._audit()

        self.assertIn(
            "regional_swing_deviations",
            result["other_root_causes"],
        )
        self.assertIn(
            "2027nsw/ONP FP",
            result["other_root_causes"][
                "regional_swing_deviations"
            ][0],
        )

    def test_regional_records_without_current_poll_work_are_ignored(self):
        with mock.patch.object(
            analysis_provenance.region_model_provenance,
            "MANIFEST_PATH",
            self.generated_manifest_path,
        ), mock.patch.object(
            analysis_provenance.region_model_provenance,
            "required_work_units",
            return_value={},
        ):
            result = self._audit()

        self.assertNotIn(
            "regional_swing_deviations",
            result["root_causes"],
        )

    def test_terminal_impacts_separate_synthetic_tpp_paths(self):
        registry = {
            "stages": [
                {
                    "id": "generate_pure_poll_trends",
                    "inputs": ["raw_polls"],
                    "outputs": ["pure_poll_outputs"],
                },
                {
                    "id": "generate_poll_trends",
                    "inputs": ["raw_polls", "pure_poll_outputs"],
                    "dependency_path_classes": {
                        "synthetic_tpp": ["pure_poll_outputs"],
                    },
                    "outputs": ["poll_trend_outputs"],
                },
            ],
            "consumers": [
                {
                    "id": "cpp_stan_model",
                    "inputs": ["poll_trend_outputs"],
                }
            ],
        }

        impacts = analysis_provenance._terminal_impacts(
            {"pure_poll_outputs"}, registry
        )

        self.assertEqual(
            impacts["synthetic_tpp_only"]["cpp_stan_model"],
            {"poll_trend_outputs"},
        )
        self.assertNotIn("cpp_stan_model", impacts["immediate"])
        self.assertNotIn("cpp_stan_model", impacts["calibration_only"])

        direct_impacts = analysis_provenance._terminal_impacts(
            {"raw_polls"}, registry
        )
        self.assertEqual(
            direct_impacts["immediate"]["cpp_stan_model"],
            {"poll_trend_outputs"},
        )

    def test_audited_generated_dependency_is_not_reported_twice(self):
        upstream_manifest = self.base / "upstream.json"
        downstream_manifest = self.base / "downstream.json"
        manifest = {
            "path_base": ".",
        }
        record = {
            "dependencies": {
                "pollster_parameters": {
                    "kind": "generated_manifest",
                    "manifest": "upstream.json",
                }
            }
        }

        self.assertTrue(
            analysis_provenance._is_audited_transitive_issue(
                "stale generated dependency pollster_parameters "
                "(pollster_parameters:2028fed)",
                record,
                downstream_manifest,
                manifest,
                {upstream_manifest.resolve(), downstream_manifest.resolve()},
            )
        )
        self.assertFalse(
            analysis_provenance._is_audited_transitive_issue(
                "stale generated dependency pollster_parameters "
                "(pollster_parameters:2028fed)",
                record,
                downstream_manifest,
                manifest,
                {downstream_manifest.resolve()},
            )
        )

    def test_targeted_audit_excludes_unselected_legacy_elections(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        selected = manifest["records"][
            "election_result_exports:2025fed"
        ]
        selected["status"] = "legacy"
        selected["dependencies"] = {}
        selected["random_seed"] = None
        other = json.loads(json.dumps(selected))
        other["scope"]["elections"] = ["1997nsw"]
        other_output = self.output_directory / "results_1997nsw.csv"
        other_output.write_text("Other election\n", encoding="utf-8")
        other["outputs"] = {
            "elections/results_1997nsw.csv":
                generated_provenance.fingerprint_file(other_output)
        }
        manifest["records"] = {
            "election_result_exports:2025fed": selected,
            "election_result_exports:1997nsw": other,
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        result = analysis_provenance.audit_repository(
            source_manifest_paths=[self.source_manifest_path],
            generated_manifest_paths=[self.generated_manifest_path],
            target_elections=["2025fed"],
        )

        self.assertEqual(result["target_elections"], ["2025fed"])
        self.assertIn(
            "1 pre-provenance work unit(s)",
            result["root_causes"]["election_result_exports"][0],
        )
        self.assertIn(
            "work units: 2025fed",
            result["root_causes"]["election_result_exports"][0],
        )

    def test_work_unit_examples_are_sorted_and_limited(self):
        records = [
            (
                "pure_poll_outputs:2028fed:Party {:02d}".format(index),
                {
                    "category": "pure_poll_outputs",
                    "dependencies": {},
                },
                ["legacy provenance baseline; generation inputs unknown"],
            )
            for index in range(17, 0, -1)
        ]

        description = analysis_provenance._generated_root_description(
            "pure_poll_outputs", records, {}
        )

        self.assertIn(
            "work units: 2028fed/Party 01, 2028fed/Party 02, "
            "2028fed/Party 03",
            description,
        )
        self.assertIn("2028fed/Party 15, ... (+2 more)", description)
        self.assertNotIn("2028fed/Party 16", description)

    def test_target_selection_follows_file_dependency_to_other_election(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        federal = manifest["records"][
            "election_result_exports:2025fed"
        ]
        state_output = self.output_directory / "results_2026vic.csv"
        state_output.write_text("State election\n", encoding="utf-8")
        state = generated_provenance.generation_record(
            category="election_result_exports",
            stage="export_election_results",
            scope=generated_provenance.generation_scope(
                elections=["2026vic"]
            ),
            run=federal["run"],
            dependencies={
                "election_result_exports":
                    generated_provenance.file_dependency(
                        "election_result_exports",
                        [self.output_path],
                        self.base,
                    )
            },
            outputs=generated_provenance.output_fingerprints(
                [state_output], self.base
            ),
            random_seed=None,
        )
        manifest["records"][
            "election_result_exports:2026vic"
        ] = state
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        selected = analysis_provenance._selected_generated_records(
            [self.generated_manifest_path],
            {"2026vic"},
        )

        self.assertEqual(
            selected[self.generated_manifest_path.resolve()],
            {
                "election_result_exports:2025fed",
                "election_result_exports:2026vic",
            },
        )

    def test_target_selection_follows_multiple_federal_prior_files(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        template = manifest["records"][
            "election_result_exports:2025fed"
        ]
        records = {}
        federal_outputs = []
        for election in ("2025fed", "2028fed"):
            output = self.output_directory / (
                "fp_trend_{}_ONP FP_pure.csv".format(election)
            )
            output.write_text("{}\n".format(election), encoding="utf-8")
            record = json.loads(json.dumps(template))
            record["scope"]["elections"] = [election]
            record["outputs"] = {
                output.relative_to(self.base).as_posix():
                    generated_provenance.fingerprint_file(output)
            }
            key = "pure_poll_outputs:{}:ONP FP".format(election)
            records[key] = record
            federal_outputs.append(output)

        state_output = self.output_directory / (
            "fp_trend_2026vic_ONP FP_pure.csv"
        )
        state_output.write_text("2026vic\n", encoding="utf-8")
        state = json.loads(json.dumps(template))
        state["scope"]["elections"] = ["2026vic"]
        state["dependencies"] = {
            "pure_poll_outputs": generated_provenance.file_dependency(
                "pure_poll_outputs", federal_outputs, self.base
            )
        }
        state["outputs"] = {
            state_output.relative_to(self.base).as_posix():
                generated_provenance.fingerprint_file(state_output)
        }
        records["pure_poll_outputs:2026vic:ONP FP"] = state
        manifest["records"] = records
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        selected = analysis_provenance._selected_generated_records(
            [self.generated_manifest_path],
            {"2026vic"},
        )

        self.assertEqual(
            selected[self.generated_manifest_path.resolve()],
            set(records),
        )

    def test_target_selection_skips_non_invalidating_lineage(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        current_key = "pure_poll_outputs:2028fed:@TPP"
        current_record = manifest["records"].pop(
            "election_result_exports:2025fed"
        )
        current_record["category"] = "pure_poll_outputs"
        current_record["scope"]["elections"] = ["2028fed"]

        cutoff_output = self.output_directory / "cutoffs_2026sa.csv"
        cutoff_output.write_text("cutoffs\n", encoding="utf-8")
        cutoff_record = json.loads(json.dumps(current_record))
        cutoff_record["category"] = "cutoff_poll_outputs"
        cutoff_record["stage"] = "generate_cutoff_poll_trends"
        cutoff_record["scope"]["elections"] = ["2026sa"]
        cutoff_record["dependencies"] = {
            "pure_poll_outputs": {
                "kind": "generated_manifest",
                "digest": "0" * 64,
                "semantic_revision": None,
                "manifest": "elections/generated-provenance.json",
                "files": [],
                "records": [current_key],
                "non_invalidating_records": [current_key],
            }
        }
        cutoff_record["outputs"] = {
            cutoff_output.relative_to(self.base).as_posix():
                generated_provenance.fingerprint_file(cutoff_output)
        }
        cutoff_key = "cutoff_poll_outputs:2026sa"
        manifest["records"] = {
            current_key: current_record,
            cutoff_key: cutoff_record,
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        selected = analysis_provenance._selected_generated_records(
            [self.generated_manifest_path],
            {"2026sa"},
        )

        self.assertEqual(
            selected[self.generated_manifest_path.resolve()],
            {cutoff_key},
        )

    def test_generated_calibration_summary_selects_only_its_traces(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        template = manifest["records"].pop(
            "election_result_exports:2025fed"
        )

        current_output = self.output_directory / "current-trace.csv"
        current_output.write_text("current\n", encoding="utf-8")
        current_trace = json.loads(json.dumps(template))
        current_trace["category"] = "poll_calibration_traces"
        current_trace["stage"] = "calibrate_pollsters"
        current_trace["scope"] = generated_provenance.generation_scope(
            elections=["2028fed"],
            parties=["@TPP"],
            qualifiers={"excluded_pollster": "Fox & Hedgehog"},
        )
        current_trace["outputs"] = (
            generated_provenance.output_fingerprints(
                [current_output], self.base
            )
        )

        legacy_output = self.output_directory / "legacy-trace.csv"
        legacy_output.write_text("legacy\n", encoding="utf-8")
        legacy_trace = json.loads(json.dumps(current_trace))
        legacy_trace["status"] = "legacy"
        legacy_trace["scope"]["qualifiers"]["excluded_pollster"] = (
            "Fox&Hedgehog"
        )
        legacy_trace["dependencies"] = {}
        legacy_trace["outputs"] = (
            generated_provenance.output_fingerprints(
                [legacy_output], self.base
            )
        )
        legacy_trace["random_seed"] = None

        summary_output = (
            self.base
            / "Outputs"
            / "Calibration"
            / "Summaries"
            / "2028fed.csv"
        )
        summary_output.parent.mkdir(parents=True)
        summary_output.write_text("summary\n", encoding="utf-8")
        summary = json.loads(json.dumps(template))
        summary["category"] = "poll_calibration_summaries"
        summary["stage"] = "calibrate_pollsters"
        summary["scope"] = generated_provenance.generation_scope(
            elections=["2028fed"]
        )
        summary["dependencies"] = {
            "poll_calibration_compatibility_inputs":
                generated_provenance.file_dependency(
                    "poll_calibration_compatibility_inputs",
                    [current_output],
                    self.base,
                )
        }
        summary["outputs"] = generated_provenance.output_fingerprints(
            [summary_output], self.base
        )

        current_key = (
            "poll_calibration_traces:2028fed:@TPP:Fox & Hedgehog"
        )
        legacy_key = "poll_calibration_traces:2028fed:@TPP:Fox&Hedgehog"
        summary_key = "poll_calibration_summaries:2028fed"
        manifest["records"] = {
            current_key: current_trace,
            legacy_key: legacy_trace,
            summary_key: summary,
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        selected = analysis_provenance._selected_generated_records(
            [self.generated_manifest_path],
            {"2028fed"},
        )

        self.assertEqual(
            selected[self.generated_manifest_path.resolve()],
            {current_key, summary_key},
        )

    def test_missing_compact_summary_is_discovered_from_bias_evidence(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        record = manifest["records"].pop(
            "election_result_exports:2025fed"
        )
        record["category"] = "bias_calibration_compatibility_inputs"
        record["stage"] = "calibrate_pollster_bias"
        record["scope"] = generated_provenance.generation_scope(
            elections=["2028fed"], parties=["@TPP"]
        )
        record["random_seed"] = None
        manifest["records"] = {
            "bias_calibration_compatibility_inputs:2028fed:@TPP": record
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        with mock.patch.object(
            analysis_provenance.calibration_provenance,
            "MANIFEST_PATH",
            self.generated_manifest_path,
        ):
            self.assertEqual(
                analysis_provenance._missing_calibration_summary_work_units(
                    {"2028fed"}
                ),
                ["2028fed"],
            )

        summary_output = (
            self.base
            / "Outputs"
            / "Calibration"
            / "Summaries"
            / "2028fed.csv"
        )
        summary_output.parent.mkdir(parents=True)
        summary_output.write_text("summary\n", encoding="utf-8")
        summary = json.loads(json.dumps(record))
        summary["category"] = "poll_calibration_summaries"
        summary["stage"] = "compact_calibration_summaries"
        summary["scope"] = generated_provenance.generation_scope(
            elections=["2028fed"]
        )
        summary["outputs"] = generated_provenance.output_fingerprints(
            [summary_output], self.base
        )
        manifest["records"]["poll_calibration_summaries:2028fed"] = summary
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        with mock.patch.object(
            analysis_provenance.calibration_provenance,
            "MANIFEST_PATH",
            self.generated_manifest_path,
        ):
            self.assertEqual(
                analysis_provenance._missing_calibration_summary_work_units(
                    {"2028fed"}
                ),
                [],
            )

    def test_target_selection_ignores_superseded_bias_party(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        template = manifest["records"].pop(
            "election_result_exports:2025fed"
        )
        current = json.loads(json.dumps(template))
        current["category"] = "bias_calibration_outputs"
        current["stage"] = "calibrate_pollster_bias"
        current["scope"] = generated_provenance.generation_scope(
            elections=["2025fed"], parties=["@TPP"]
        )
        legacy = json.loads(json.dumps(current))
        legacy["status"] = "legacy"
        legacy["scope"]["parties"] = ["UAP FP"]
        legacy["dependencies"] = {}
        legacy["random_seed"] = None
        legacy_output = self.output_directory / "legacy-uap-bias.csv"
        legacy_output.write_text("legacy\n", encoding="utf-8")
        legacy["outputs"] = generated_provenance.output_fingerprints(
            [legacy_output], self.base
        )
        current_key = "bias_calibration_outputs:2025fed:@TPP"
        legacy_key = "bias_calibration_outputs:2025fed:UAP FP"
        manifest["records"] = {
            current_key: current,
            legacy_key: legacy,
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        with mock.patch.object(
            analysis_provenance.calibration_provenance,
            "configured_parties_by_election",
            return_value={"2025fed": {"@TPP"}},
        ):
            selected = analysis_provenance._selected_generated_records(
                [self.generated_manifest_path],
                {"2025fed"},
            )

        self.assertEqual(
            selected[self.generated_manifest_path.resolve()],
            {current_key},
        )

    def test_dependency_selection_ignores_superseded_bias_party(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        template = manifest["records"].pop(
            "election_result_exports:2025fed"
        )
        legacy_output = self.output_directory / "legacy-uap-bias.csv"
        legacy_output.write_text("legacy\n", encoding="utf-8")
        legacy = json.loads(json.dumps(template))
        legacy["status"] = "legacy"
        legacy["category"] = "bias_calibration_outputs"
        legacy["stage"] = "calibrate_pollster_bias"
        legacy["scope"] = generated_provenance.generation_scope(
            elections=["2025fed"], parties=["UAP FP"]
        )
        legacy["dependencies"] = {}
        legacy["outputs"] = generated_provenance.output_fingerprints(
            [legacy_output], self.base
        )
        legacy["random_seed"] = None

        consumer_output = self.output_directory / "consumer.csv"
        consumer_output.write_text("consumer\n", encoding="utf-8")
        consumer = json.loads(json.dumps(template))
        consumer["scope"]["elections"] = ["2028fed"]
        consumer["dependencies"] = {
            "bias_calibration_outputs":
                generated_provenance.file_dependency(
                    "bias_calibration_outputs",
                    [legacy_output],
                    self.base,
                )
        }
        consumer["outputs"] = generated_provenance.output_fingerprints(
            [consumer_output], self.base
        )
        legacy_key = "bias_calibration_outputs:2025fed:UAP FP"
        consumer_key = "election_result_exports:2028fed"
        manifest["records"] = {
            legacy_key: legacy,
            consumer_key: consumer,
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )

        with mock.patch.object(
            analysis_provenance.calibration_provenance,
            "configured_parties_by_election",
            return_value={"2025fed": {"@TPP"}},
        ):
            selected = analysis_provenance._selected_generated_records(
                [self.generated_manifest_path],
                {"2028fed"},
            )

        self.assertEqual(
            selected[self.generated_manifest_path.resolve()],
            {consumer_key},
        )

    def test_legacy_trend_infers_same_election_pollster_dependency(self):
        pollster_manifest_path = (
            self.output_directory
            / "pollster-generated-provenance.json"
        )
        pollster_output = self.output_directory / "variability-2026vic.csv"
        pollster_output.write_text("pollsters\n", encoding="utf-8")
        pollster_record = generated_provenance.generation_record(
            category="pollster_parameters",
            stage="analyse_pollsters",
            scope=generated_provenance.generation_scope(
                elections=["2026vic"]
            ),
            run="test-run",
            dependencies={},
            outputs=generated_provenance.output_fingerprints(
                [pollster_output], self.base
            ),
            random_seed=None,
        )
        generated_provenance.update_manifest(
            pollster_manifest_path,
            {"pollster_parameters:2026vic": pollster_record},
            {
                "test-run": {
                    "generated_at_utc": "2026-01-01T00:00:00Z",
                    "command": ["python3", "pollster_analysis.py"],
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
            description="Test pollster parameters.",
        )

        trend_manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        trend_record = trend_manifest["records"].pop(
            "election_result_exports:2025fed"
        )
        trend_record["status"] = "legacy"
        trend_record["category"] = "pure_poll_outputs"
        trend_record["stage"] = "generate_pure_poll_trends"
        trend_record["scope"]["elections"] = ["2026vic"]
        trend_record["dependencies"] = {}
        trend_manifest["records"] = {
            "pure_poll_outputs:2026vic:@TPP": trend_record
        }
        self.generated_manifest_path.write_text(
            json.dumps(trend_manifest), encoding="utf-8"
        )

        selected, dependencies = (
            analysis_provenance._selected_generated_records(
                [
                    self.generated_manifest_path,
                    pollster_manifest_path,
                ],
                {"2026vic"},
                include_dependencies=True,
            )
        )

        pollster_id = (
            "{}::pollster_parameters:2026vic".format(
                analysis_provenance._manifest_label(
                    pollster_manifest_path
                )
            )
        )
        trend_id = "{}::pure_poll_outputs:2026vic:@TPP".format(
            analysis_provenance._manifest_label(
                self.generated_manifest_path
            )
        )
        self.assertIn(
            "pollster_parameters:2026vic",
            selected[pollster_manifest_path.resolve()],
        )
        self.assertIn(pollster_id, dependencies[trend_id])

    def test_legacy_calibration_is_reported_as_calibration_path_issue(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        record = manifest["records"]["election_result_exports:2025fed"]
        record["status"] = "legacy"
        record["category"] = "poll_calibration_traces"
        record["stage"] = "calibrate_pollsters"
        record["dependencies"] = {}
        record["random_seed"] = None
        manifest["records"] = {
            "poll_calibration_traces:2025fed:ALP:full": record
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        registry = {
            "stages": [
                {
                    "id": "calibrate_pollsters",
                    "inputs": ["raw_polls"],
                    "outputs": ["poll_calibration_traces"],
                },
                {
                    "id": "normal_trend",
                    "inputs": ["poll_calibration_traces"],
                    "outputs": ["poll_trend"],
                },
            ],
            "consumers": [
                {
                    "id": "cpp_stan_model",
                    "inputs": ["poll_trend"],
                }
            ],
        }

        result = analysis_provenance.audit_repository(
            source_manifest_paths=[self.source_manifest_path],
            generated_manifest_paths=[self.generated_manifest_path],
            registry=registry,
        )
        output = StringIO()
        with redirect_stdout(output):
            analysis_provenance._print_audit(result)

        self.assertEqual(result["other_root_causes"], {})
        self.assertIn(
            "poll_calibration_traces",
            result["calibration_root_causes"],
        )
        self.assertIn(
            "Calibration-path-only provenance issues:",
            output.getvalue(),
        )
        self.assertNotIn("inputs and seeds", output.getvalue())
        self.assertIn(
            "Missing historical seed metadata is informational only",
            output.getvalue(),
        )

    def test_mixed_root_is_not_reported_as_calibration_only(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        immediate_record = manifest["records"][
            "election_result_exports:2025fed"
        ]
        immediate_record["category"] = "poll_trend_outputs"
        immediate_record["stage"] = "generate_poll_trends"
        calibration_record = json.loads(json.dumps(immediate_record))
        calibration_record["category"] = "poll_calibration_traces"
        calibration_record["stage"] = "calibrate_pollsters"
        calibration_record["outputs"] = {
            next(iter(immediate_record["outputs"])): dict(
                next(iter(immediate_record["outputs"].values()))
            )
        }
        calibration_record["outputs"]["elections/calibration.csv"] = (
            calibration_record["outputs"].pop(
                next(iter(calibration_record["outputs"]))
            )
        )
        (self.output_directory / "calibration.csv").write_text(
            "Calibration\n", encoding="utf-8"
        )
        calibration_record["outputs"]["elections/calibration.csv"] = (
            generated_provenance.fingerprint_file(
                self.output_directory / "calibration.csv"
            )
        )
        manifest["records"] = {
            "poll_trend_outputs:2025fed": immediate_record,
            "poll_calibration_traces:2025fed": calibration_record,
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        self.script_path.write_text("print('changed')\n", encoding="utf-8")
        analysis_provenance.register_changes(
            [self.script_path],
            "Changed shared model code.",
            "material",
            source_manifest_paths=[self.source_manifest_path],
        )
        registry = {
            "stages": [
                {
                    "id": "calibrate_pollsters",
                    "inputs": ["election_store_script"],
                    "outputs": ["poll_calibration_traces"],
                },
                {
                    "id": "generate_poll_trends",
                    "inputs": ["election_store_script"],
                    "outputs": ["poll_trend_outputs"],
                },
            ],
            "consumers": [
                {
                    "id": "cpp_stan_model",
                    "inputs": ["poll_trend_outputs"],
                }
            ],
        }

        result = analysis_provenance.audit_repository(
            source_manifest_paths=[self.source_manifest_path],
            generated_manifest_paths=[self.generated_manifest_path],
            registry=registry,
        )

        self.assertIn(
            "election_store_script", result["other_root_causes"]
        )
        self.assertNotIn(
            "election_store_script", result["calibration_root_causes"]
        )
        self.assertIn(
            "poll_trend_outputs",
            result["impacts"]["immediate"]["cpp_stan_model"],
        )

    def test_regenerated_normal_output_leaves_calibration_only_path(self):
        manifest = generated_provenance.load_manifest(
            self.generated_manifest_path
        )
        record = manifest["records"]["election_result_exports:2025fed"]
        record["category"] = "poll_calibration_traces"
        record["stage"] = "calibrate_pollsters"
        manifest["records"] = {
            "poll_calibration_traces:2025fed": record
        }
        self.generated_manifest_path.write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        self.script_path.write_text("print('changed')\n", encoding="utf-8")
        analysis_provenance.register_changes(
            [self.script_path],
            "Changed shared model code.",
            "material",
            source_manifest_paths=[self.source_manifest_path],
        )
        registry = {
            "stages": [
                {
                    "id": "calibrate_pollsters",
                    "inputs": ["election_store_script"],
                    "outputs": ["poll_calibration_traces"],
                },
                {
                    "id": "analyse_pollsters",
                    "inputs": ["poll_calibration_traces"],
                    "outputs": ["pollster_parameters"],
                },
                {
                    "id": "generate_poll_trends",
                    "inputs": [
                        "election_store_script",
                        "pollster_parameters",
                    ],
                    "outputs": ["poll_trend_outputs"],
                },
            ],
            "consumers": [
                {
                    "id": "cpp_stan_model",
                    "inputs": ["poll_trend_outputs"],
                }
            ],
        }

        result = analysis_provenance.audit_repository(
            source_manifest_paths=[self.source_manifest_path],
            generated_manifest_paths=[self.generated_manifest_path],
            registry=registry,
        )

        self.assertIn(
            "election_store_script", result["calibration_root_causes"]
        )
        self.assertEqual(result["other_root_causes"], {})
        self.assertIn(
            "poll_trend_outputs",
            result["impacts"]["calibration_only"]["cpp_stan_model"],
        )

    def test_empty_interactive_selection_cancels_before_configuration(self):
        changes = [
            {
                "path": self.script_path,
                "relative_path": "election_store.py",
                "category": "election_store_script",
                "change_kind": "modified",
            }
        ]
        with mock.patch.object(
            analysis_provenance,
            "_unregistered_files",
            return_value=changes,
        ), mock.patch.object(
            analysis_provenance,
            "_menu_checkbox",
            return_value=[],
        ), mock.patch.object(
            analysis_provenance,
            "_menu_select",
        ) as next_prompt:
            analysis_provenance._interactive_register()

        next_prompt.assert_not_called()

    def test_interactive_scope_defaults_to_specific_elections(self):
        def select_scope(message, choices, default=None):
            self.assertEqual(message, "Change scope")
            self.assertEqual(default, "specific")
            self.assertEqual(choices[0]["value"], "specific")
            return "specific"

        with mock.patch.object(
            analysis_provenance,
            "_menu_select",
            side_effect=select_scope,
        ), mock.patch.object(
            analysis_provenance,
            "_menu_text",
            side_effect=["2028fed", "", ""],
        ):
            scope = analysis_provenance._interactive_scope()

        self.assertFalse(scope["all"])
        self.assertEqual(scope["elections"], ["2028fed"])

    def test_all_election_scope_requires_separate_affirmation(self):
        scope = source_provenance._build_scope(all_scopes=True)
        with mock.patch.object(
            analysis_provenance,
            "_menu_confirm",
            return_value=False,
        ) as confirm:
            accepted = analysis_provenance._confirm_all_election_scope(scope)

        self.assertFalse(accepted)
        confirm.assert_called_once_with(
            "This scope can affect every election. Continue with "
            "all-election impact?",
            default=False,
        )

    def test_election_scope_does_not_require_extra_affirmation(self):
        scope = source_provenance._build_scope(elections=["2028fed"])
        with mock.patch.object(
            analysis_provenance,
            "_menu_confirm",
        ) as confirm:
            accepted = analysis_provenance._confirm_all_election_scope(scope)

        self.assertTrue(accepted)
        confirm.assert_not_called()

    def test_party_only_scope_requires_all_election_affirmation(self):
        scope = source_provenance._build_scope(parties=["ONP FP"])
        with mock.patch.object(
            analysis_provenance,
            "_menu_confirm",
            return_value=True,
        ) as confirm:
            accepted = analysis_provenance._confirm_all_election_scope(scope)

        self.assertTrue(accepted)
        confirm.assert_called_once()

    def test_declined_all_election_scope_cancels_registration(self):
        changes = [
            {
                "path": self.script_path,
                "relative_path": "election_store.py",
                "category": "election_store_script",
                "change_kind": "modified",
            }
        ]
        with mock.patch.object(
            analysis_provenance,
            "_unregistered_files",
            return_value=changes,
        ), mock.patch.object(
            analysis_provenance,
            "_menu_checkbox",
            return_value=[str(self.script_path)],
        ), mock.patch.object(
            analysis_provenance,
            "_menu_select",
            side_effect=["negligible", "formatting"],
        ), mock.patch.object(
            analysis_provenance,
            "_menu_text",
            return_value="Formatting only.",
        ), mock.patch.object(
            analysis_provenance,
            "_interactive_scope",
            return_value=source_provenance._build_scope(all_scopes=True),
        ), mock.patch.object(
            analysis_provenance,
            "_confirm_all_election_scope",
            return_value=False,
        ), mock.patch.object(
            analysis_provenance,
            "register_changes",
        ) as register:
            analysis_provenance._interactive_register()

        register.assert_not_called()


if __name__ == "__main__":
    unittest.main()

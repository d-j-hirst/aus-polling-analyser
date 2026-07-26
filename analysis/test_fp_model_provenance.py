import tempfile
import unittest
from datetime import date
from pathlib import Path
from unittest import mock

import fp_model_provenance
import generated_provenance


class PureTrendProvenanceTests(unittest.TestCase):
    def test_baseline_groups_complete_canonical_work_units(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output_directory = base / "Outputs"
            output_directory.mkdir()
            for kind in ("trend", "polls", "house_effects"):
                (output_directory / (
                    "fp_{}_2028fed_@TPP_pure.csv".format(kind)
                )).write_text("{}\n".format(kind), encoding="utf-8")
            (output_directory / (
                "fp_polls#_2028fed_@TPP_pure.csv"
            )).write_text("backup\n", encoding="utf-8")
            manifest_path = (
                output_directory / "pure-generated-provenance.json"
            )

            with mock.patch.object(
                fp_model_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                fp_model_provenance,
                "OUTPUT_DIRECTORY",
                output_directory,
            ), mock.patch.object(
                fp_model_provenance, "MANIFEST_PATH", manifest_path
            ):
                fp_model_provenance.baseline_existing_outputs()

            manifest = generated_provenance.load_manifest(manifest_path)
            self.assertEqual(
                set(manifest["records"]),
                {"pure_poll_outputs:2028fed:@TPP"},
            )
            record = next(iter(manifest["records"].values()))
            self.assertEqual(record["status"], "legacy")
            self.assertEqual(len(record["outputs"]), 3)
            self.assertFalse(
                any("#" in path for path in record["outputs"])
            )

    def test_incomplete_work_unit_cannot_be_baselined(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory) / "Outputs"
            output_directory.mkdir()
            (output_directory / (
                "fp_trend_2028fed_@TPP_pure.csv"
            )).write_text("trend\n", encoding="utf-8")

            with mock.patch.object(
                fp_model_provenance,
                "OUTPUT_DIRECTORY",
                output_directory,
            ):
                with self.assertRaisesRegex(
                    generated_provenance.GeneratedProvenanceError,
                    "incomplete pure-trend work units",
                ):
                    fp_model_provenance._legacy_records()

    def test_legacy_state_records_include_overlapping_federal_priors(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output_directory = base / "Outputs"
            data_directory = base / "Data"
            output_directory.mkdir()
            data_directory.mkdir()
            (data_directory / "election-cycles.csv").write_text(
                "2022,fed,2019-05-19,2022-05-21\n"
                "2025,fed,2022-05-22,2025-05-03\n"
                "2028,fed,2025-05-04,2028-05-20\n"
                "2026,vic,2022-11-27,2026-11-28\n",
                encoding="utf-8",
            )
            (data_directory / "significant-parties.csv").write_text(
                "2022,fed,OTH FP\n"
                "2025,fed,ONP FP,ALP FP\n"
                "2028,fed,GRN FP,SFF FP\n"
                "2026,vic,ONP FP,GRN FP,UAP FP\n",
                encoding="utf-8",
            )
            for election, party in (
                ("2025fed", "ONP FP"),
                ("2025fed", "ALP FP"),
                ("2025fed", "UAP FP"),
                ("2028fed", "GRN FP"),
                ("2028fed", "SFF FP"),
                ("2022fed", "OTH FP"),
                ("2026vic", "ONP FP"),
            ):
                for kind in ("trend", "polls", "house_effects"):
                    (output_directory / (
                        "fp_{}_{}_{}_pure.csv".format(
                            kind, election, party
                        )
                    )).write_text(
                        "{}\n".format(kind), encoding="utf-8"
                    )

            with mock.patch.object(
                fp_model_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                fp_model_provenance,
                "OUTPUT_DIRECTORY",
                output_directory,
            ):
                records = fp_model_provenance._legacy_records()

            dependency = records[
                "pure_poll_outputs:2026vic:ONP FP"
            ]["dependencies"]["pure_poll_outputs"]
            self.assertEqual(
                dependency["files"],
                [
                    "Outputs/fp_trend_2025fed_ONP FP_pure.csv",
                ],
            )
            self.assertNotIn(
                "Outputs/fp_trend_2025fed_ALP FP_pure.csv",
                dependency["files"],
            )
            self.assertNotIn(
                "Outputs/fp_trend_2025fed_UAP FP_pure.csv",
                dependency["files"],
            )
            self.assertNotIn(
                "Outputs/fp_trend_2028fed_SFF FP_pure.csv",
                dependency["files"],
            )
            self.assertNotIn(
                "Outputs/fp_trend_2022fed_OTH FP_pure.csv",
                dependency["files"],
            )

    def test_dependencies_include_pollster_record_and_feedback_files(self):
        generated_dependency = {
            "kind": "generated_manifest",
            "digest": "a" * 64,
            "semantic_revision": None,
            "manifest": "pollster.json",
            "files": [],
            "records": ["pollster_parameters:2028fed"],
        }
        feedback_dependency = {
            "kind": "files",
            "digest": "b" * 64,
            "semantic_revision": None,
            "manifest": None,
            "files": ["Outputs/fp_trend_2025fed_ONP FP.csv"],
            "records": [],
        }
        with mock.patch.object(
            fp_model_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            generated_provenance,
            "generated_manifest_dependency",
            return_value=generated_dependency,
        ) as generated_call, mock.patch.object(
            generated_provenance,
            "file_dependency",
            return_value=feedback_dependency,
        ) as file_call:
            recorder = fp_model_provenance.PureTrendRecorder(
                ["python3", "fp_model.py", "--pure"]
            )
            dependencies = recorder.dependencies_for(
                "2028fed",
                ["Outputs/fp_trend_2025fed_ONP FP.csv"],
            )

        self.assertEqual(
            dependencies["pollster_parameters"], generated_dependency
        )
        self.assertEqual(
            dependencies["pure_poll_outputs"], feedback_dependency
        )
        self.assertEqual(
            generated_call.call_args.args[2],
            ["pollster_parameters:2028fed"],
        )
        self.assertTrue(generated_call.call_args.kwargs["allow_stale"])
        file_call.assert_called_once()

    def test_baseline_prunes_unchanged_irrelevant_federal_prior(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output_directory = base / "Outputs"
            output_directory.mkdir()
            retained = (
                output_directory
                / "fp_trend_2025fed_ONP FP_pure.csv"
            )
            removed = (
                output_directory
                / "fp_trend_2025fed_UAP FP_pure.csv"
            )
            retained.write_text("ONP\n", encoding="utf-8")
            removed.write_text("UAP\n", encoding="utf-8")
            output = (
                output_directory
                / "fp_trend_2026vic_ONP FP_pure.csv"
            )
            output.write_text("state\n", encoding="utf-8")
            dependency = generated_provenance.file_dependency(
                "pure_poll_outputs", [retained, removed], base
            )
            record = generated_provenance.generation_record(
                category="pure_poll_outputs",
                stage="generate_pure_poll_trends",
                scope=generated_provenance.generation_scope(
                    elections=["2026vic"],
                    parties=["ONP FP"],
                ),
                run="test",
                dependencies={"pure_poll_outputs": dependency},
                outputs=generated_provenance.output_fingerprints(
                    [output], base
                ),
                random_seed=123,
            )
            cycles = {
                "2025fed": (
                    date(2022, 5, 22),
                    date(2025, 5, 3),
                ),
                "2026vic": (
                    date(2022, 11, 27),
                    date(2026, 11, 28),
                ),
            }
            significant_parties = {
                "2025fed": {"ONP FP"},
                "2026vic": {"ONP FP"},
            }

            with mock.patch.object(
                fp_model_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                fp_model_provenance,
                "OUTPUT_DIRECTORY",
                output_directory,
            ):
                changed = (
                    fp_model_provenance
                    ._prune_irrelevant_federal_priors(
                        record, cycles, significant_parties
                    )
                )

            self.assertTrue(changed)
            self.assertEqual(
                record["dependencies"]["pure_poll_outputs"]["files"],
                ["Outputs/fp_trend_2025fed_ONP FP_pure.csv"],
            )

    def test_baseline_does_not_prune_a_changed_dependency(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output_directory = base / "Outputs"
            output_directory.mkdir()
            path = (
                output_directory
                / "fp_trend_2025fed_UAP FP_pure.csv"
            )
            path.write_text("original\n", encoding="utf-8")
            dependency = generated_provenance.file_dependency(
                "pure_poll_outputs", [path], base
            )
            path.write_text("changed\n", encoding="utf-8")
            output = (
                output_directory
                / "fp_trend_2026vic_ONP FP_pure.csv"
            )
            output.write_text("state\n", encoding="utf-8")
            record = generated_provenance.generation_record(
                category="pure_poll_outputs",
                stage="generate_pure_poll_trends",
                scope=generated_provenance.generation_scope(
                    elections=["2026vic"],
                    parties=["ONP FP"],
                ),
                run="test",
                dependencies={"pure_poll_outputs": dependency},
                outputs=generated_provenance.output_fingerprints(
                    [output], base
                ),
                random_seed=123,
            )
            cycles = {
                "2025fed": (
                    date(2022, 5, 22),
                    date(2025, 5, 3),
                ),
                "2026vic": (
                    date(2022, 11, 27),
                    date(2026, 11, 28),
                ),
            }
            significant_parties = {
                "2025fed": {"ONP FP"},
                "2026vic": {"ONP FP"},
            }

            with mock.patch.object(
                fp_model_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                fp_model_provenance,
                "OUTPUT_DIRECTORY",
                output_directory,
            ):
                changed = (
                    fp_model_provenance
                    ._prune_irrelevant_federal_priors(
                        record, cycles, significant_parties
                    )
                )

            self.assertFalse(changed)
            self.assertEqual(
                record["dependencies"]["pure_poll_outputs"],
                dependency,
            )

    def test_completed_work_unit_records_seed_and_three_outputs(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output_directory = base / "Outputs"
            output_directory.mkdir()
            outputs = []
            for kind in ("trend", "polls", "house_effects"):
                path = output_directory / (
                    "fp_{}_2028fed_@TPP_pure.csv".format(kind)
                )
                path.write_text("{}\n".format(kind), encoding="utf-8")
                outputs.append(path)
            manifest_path = (
                output_directory / "pure-generated-provenance.json"
            )

            with mock.patch.object(
                fp_model_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                fp_model_provenance, "MANIFEST_PATH", manifest_path
            ), mock.patch.object(
                fp_model_provenance,
                "_source_dependencies",
                return_value={},
            ):
                recorder = fp_model_provenance.PureTrendRecorder(
                    ["python3", "fp_model.py", "--pure"]
                )
                recorder.record(
                    election="2028fed",
                    party="@TPP",
                    outputs=outputs,
                    dependencies={},
                    random_seed=123456,
                )

            manifest = generated_provenance.load_manifest(manifest_path)
            record = manifest["records"][
                "pure_poll_outputs:2028fed:@TPP"
            ]
            self.assertEqual(record["status"], "generated")
            self.assertEqual(record["random_seed"], 123456)
            self.assertEqual(len(record["outputs"]), 3)


class FinalTrendProvenanceTests(unittest.TestCase):
    def test_baseline_groups_only_canonical_final_work_units(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output_directory = base / "Outputs"
            data_directory = base / "Data"
            output_directory.mkdir()
            data_directory.mkdir()
            (data_directory / "election-cycles.csv").write_text(
                "2028,fed,2025-05-04,2028-05-20\n",
                encoding="utf-8",
            )
            (data_directory / "significant-parties.csv").write_text(
                "2028,fed,@TPP,OTH FP\n", encoding="utf-8"
            )
            for kind in ("trend", "polls", "house_effects"):
                (output_directory / (
                    "fp_{}_2028fed_OTH FP.csv".format(kind)
                )).write_text("{}\n".format(kind), encoding="utf-8")
                (output_directory / (
                    "fp_{}_2028fed_OTH FP_pure.csv".format(kind)
                )).write_text("pure\n", encoding="utf-8")
            (output_directory / (
                "fp_polls#_2028fed_OTH FP.csv"
            )).write_text("backup\n", encoding="utf-8")

            with mock.patch.object(
                fp_model_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                fp_model_provenance,
                "OUTPUT_DIRECTORY",
                output_directory,
            ):
                records = (
                    fp_model_provenance._legacy_final_records()
                )

            self.assertEqual(
                set(records),
                {"poll_trend_outputs:2028fed:OTH FP"},
            )
            record = next(iter(records.values()))
            self.assertEqual(record["status"], "legacy")
            self.assertEqual(len(record["outputs"]), 3)
            self.assertFalse(
                any("_pure.csv" in path for path in record["outputs"])
            )

    def test_final_dependencies_are_scoped_to_actual_inputs(self):
        pollster_dependency = {
            "kind": "generated_manifest",
            "digest": "a" * 64,
            "semantic_revision": None,
            "manifest": "pollster.json",
            "files": [],
            "records": ["pollster_parameters:2026vic"],
        }
        approval_dependency = {
            "kind": "generated_manifest",
            "digest": "b" * 64,
            "semantic_revision": None,
            "manifest": "pure.json",
            "files": [],
            "records": ["pure_poll_outputs:2026vic:@TPP"],
        }
        feedback_dependency = {
            "kind": "files",
            "digest": "c" * 64,
            "semantic_revision": None,
            "manifest": None,
            "files": ["Outputs/fp_trend_2025fed_ONP FP.csv"],
            "records": [],
        }
        with mock.patch.object(
            fp_model_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            fp_model_provenance.approvals_provenance,
            "generation_dependencies",
            return_value={"pure_poll_outputs": approval_dependency},
        ), mock.patch.object(
            generated_provenance,
            "generated_manifest_dependency",
            return_value=pollster_dependency,
        ), mock.patch.object(
            generated_provenance,
            "file_dependency",
            return_value=feedback_dependency,
        ):
            recorder = fp_model_provenance.FinalTrendRecorder(
                ["python3", "fp_model.py"]
            )
            major_dependencies = recorder.dependencies_for(
                "2026vic",
                "ALP FP",
                [],
            )

        self.assertEqual(
            major_dependencies["pollster_parameters"],
            pollster_dependency,
        )
        self.assertEqual(
            major_dependencies["pure_poll_outputs"],
            approval_dependency,
        )
        self.assertNotIn("poll_trend_outputs", major_dependencies)

        with mock.patch.object(
            fp_model_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            fp_model_provenance.approvals_provenance,
            "generation_dependencies",
            return_value={"pure_poll_outputs": approval_dependency},
        ), mock.patch.object(
            generated_provenance,
            "generated_manifest_dependency",
            return_value=pollster_dependency,
        ) as generated_call, mock.patch.object(
            generated_provenance,
            "file_dependency",
            return_value=feedback_dependency,
        ):
            recorder = fp_model_provenance.FinalTrendRecorder(
                ["python3", "fp_model.py"]
            )
            minor_dependencies = recorder.dependencies_for(
                "2026vic",
                "ONP FP",
                ["Outputs/fp_trend_2025fed_ONP FP.csv"],
            )

        self.assertEqual(
            set(minor_dependencies),
            {"pollster_parameters", "poll_trend_outputs"},
        )
        self.assertEqual(
            minor_dependencies["poll_trend_outputs"],
            feedback_dependency,
        )
        generated_call.assert_called_once()


class CutoffTrendProvenanceTests(unittest.TestCase):
    def test_completed_cutoff_records_one_consolidated_election_file(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            output_directory = base / "Outputs"
            cutoff_directory = output_directory / "Cutoffs"
            cutoff_directory.mkdir(parents=True)
            output = cutoff_directory / "cutoffs_2025fed.csv"
            output.write_text(
                "ScheduledCutoffDays,PollTrendEndDays,Party,StanSeed,50%\n"
                "28,30,@TPP,123456,52.1\n",
                encoding="utf-8",
            )
            manifest_path = (
                output_directory / "cutoff-generated-provenance.json"
            )

            with mock.patch.object(
                fp_model_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                fp_model_provenance,
                "CUTOFF_MANIFEST_PATH",
                manifest_path,
            ), mock.patch.object(
                fp_model_provenance,
                "_source_dependencies",
                return_value={},
            ):
                recorder = fp_model_provenance.CutoffTrendRecorder(
                    ["python3", "fp_model.py", "--cutoff"]
                )
                recorder.record(
                    election="2025fed",
                    output=output,
                    dependencies={},
                )

            manifest = generated_provenance.load_manifest(manifest_path)
            record = manifest["records"][
                "cutoff_poll_outputs:2025fed"
            ]
            self.assertEqual(record["status"], "generated")
            self.assertEqual(
                record["random_seed"],
                "stored per row in consolidated cutoff output",
            )
            self.assertEqual(
                record["scope"]["elections"], ["2025fed"]
            )
            self.assertEqual(record["scope"]["parties"], [])
            self.assertEqual(len(record["outputs"]), 1)


if __name__ == "__main__":
    unittest.main()

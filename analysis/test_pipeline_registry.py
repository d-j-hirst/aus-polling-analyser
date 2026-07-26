import copy
import unittest
from pathlib import Path

import pipeline_registry


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent


class PipelineRegistryTests(unittest.TestCase):
    def setUp(self):
        self.registry = pipeline_registry.load_registry()

    def test_repository_registry_is_valid(self):
        pipeline_registry.validate_registry(self.registry)
        pipeline_registry.validate_authored_paths(
            self.registry, ANALYSIS_DIRECTORY
        )

    def test_core_order_respects_principal_dependencies(self):
        order = pipeline_registry.topological_stage_order(self.registry)
        positions = {
            stage_id: position for position, stage_id in enumerate(order)
        }

        self.assertLess(
            positions["cache_election_results"],
            positions["export_election_results"],
        )
        self.assertLess(
            positions["calibrate_pollsters"],
            positions["analyse_pollsters"],
        )
        self.assertLess(
            positions["calibrate_pollster_bias"],
            positions["analyse_pollsters"],
        )
        self.assertLess(
            positions["analyse_pollsters"],
            positions["generate_pure_poll_trends"],
        )
        self.assertLess(
            positions["generate_pure_poll_trends"],
            positions["generate_poll_trends"],
        )
        self.assertLess(
            positions["generate_poll_trends"],
            positions["generate_trend_adjustments"],
        )

    def test_final_trend_embeds_approval_generation(self):
        stages = {
            stage["id"]: stage for stage in self.registry["stages"]
        }
        self.assertNotIn("generate_synthetic_tpp", stages)
        final_stage = stages["generate_poll_trends"]
        self.assertIn("pure_poll_outputs", final_stage["inputs"])
        self.assertIn("approvals_script", final_stage["inputs"])
        self.assertIn("synthetic_tpp_outputs", final_stage["outputs"])
        self.assertIn("poll_trend_outputs", final_stage["outputs"])
        self.assertEqual(
            final_stage["dependency_path_classes"]["synthetic_tpp"],
            [
                "approval_context",
                "pure_poll_outputs",
                "approvals_script",
                "approvals_provenance_script",
            ],
        )

    def test_fp_model_execution_uses_argument_array_and_wrapper(self):
        stages = {
            stage["id"]: stage for stage in self.registry["stages"]
        }
        command = pipeline_registry.stage_command(
            stages["generate_pure_poll_trends"],
            {"election_cli": "2028-fed"},
            python_executable="/test/python",
        )
        self.assertEqual(
            command,
            [
                "/test/python",
                "run_fp_model.py",
                "--pure",
                "--election",
                "2028-fed",
            ],
        )

    def test_missing_execution_template_value_is_rejected(self):
        stage = {
            stage["id"]: stage for stage in self.registry["stages"]
        }["generate_poll_trends"]
        with self.assertRaisesRegex(
            pipeline_registry.RegistryError, "election_cli"
        ):
            pipeline_registry.stage_command(stage, {})

    def test_unknown_dependency_is_rejected(self):
        registry = copy.deepcopy(self.registry)
        registry["stages"][0]["inputs"].append("missing_category")

        with self.assertRaisesRegex(
            pipeline_registry.RegistryError, "unknown categories"
        ):
            pipeline_registry.validate_registry(registry)

    def test_source_acquisition_depends_only_on_its_code(self):
        stages = {
            stage["id"]: stage for stage in self.registry["stages"]
        }
        self.assertEqual(
            stages["cache_election_results"]["inputs"],
            ["election_data_script", "election_code_script"],
        )

    def test_pollster_analysis_uses_compact_calibration_boundary(self):
        stages = {
            stage["id"]: stage for stage in self.registry["stages"]
        }
        stage = stages["analyse_pollsters"]

        self.assertIn(
            "poll_calibration_summaries", stage["optional_inputs"]
        )
        self.assertIn("bias_calibration_outputs", stage["inputs"])
        self.assertNotIn("poll_calibration_traces", stage["inputs"])
        self.assertIn("pollster_analysis_script", stage["inputs"])
        self.assertIn(
            "pollster_analysis_provenance_script", stage["inputs"]
        )

    def test_cutoff_generation_tracks_its_provenance_helpers(self):
        stages = {
            stage["id"]: stage for stage in self.registry["stages"]
        }
        stage = stages["generate_cutoff_poll_trends"]

        self.assertIn("fp_model_provenance_script", stage["inputs"])
        self.assertIn("calibration_provenance_script", stage["inputs"])

    def test_regional_generation_tracks_code_and_provenance(self):
        stages = {
            stage["id"]: stage for stage in self.registry["stages"]
        }
        stage = stages["generate_regional_swings"]

        self.assertIn("regional_poll_inputs", stage["inputs"])
        self.assertIn("region_model_script", stage["inputs"])
        self.assertIn(
            "region_model_provenance_script", stage["inputs"]
        )
        self.assertIn("regional_stan_models", stage["inputs"])

    def test_required_cycle_is_rejected(self):
        registry = copy.deepcopy(self.registry)
        stages = {stage["id"]: stage for stage in registry["stages"]}
        stages["export_election_results"]["inputs"].append("seat_statistics")
        stages["analyse_elections"]["inputs"].append(
            "election_result_exports"
        )

        with self.assertRaisesRegex(
            pipeline_registry.RegistryError, "contains a cycle"
        ):
            pipeline_registry.validate_registry(registry)


if __name__ == "__main__":
    unittest.main()

import unittest
from collections import defaultdict
from contextlib import redirect_stdout
from io import StringIO
from unittest import mock

import generated_provenance
import pipeline
import pipeline_registry


def work_unit(
    record_key,
    category,
    stage,
    status,
    election,
    party=None,
    blocking=False,
    target_match=True,
    dependencies=None,
):
    return {
        "id": "test-generated-provenance.json::{}".format(record_key),
        "record_key": record_key,
        "category": category,
        "stage": stage,
        "scope": generated_provenance.generation_scope(
            elections=[election],
            parties=[party] if party else [],
        ),
        "manifest": "test-generated-provenance.json",
        "target_match": target_match,
        "dependencies": dependencies or [],
        "status": status,
        "blocking": blocking,
        "path_classes": ["immediate"],
        "issues": (
            []
            if status == "current"
            else [
                {
                    "code": status,
                    "root_category": category,
                    "message": "{} test issue".format(status),
                }
            ]
        ),
    }


def audit_result(work_units, source_issues=None, manifest_issues=None):
    counts = {
        status: sum(
            work_unit["status"] == status for work_unit in work_units
        )
        for status in pipeline.STATUS_ORDER
    }
    return {
        "target_elections": ["2026vic"],
        "work_units": work_units,
        "source_issues": source_issues or [],
        "manifest_issues": manifest_issues or [],
        "internal_errors": [],
        "summary": {
            "work_unit_status_counts": counts,
            "has_blockers": False,
        },
        "other_root_causes": {},
        "synthetic_tpp_root_causes": {},
        "calibration_root_causes": {},
        "impacts": {
            "immediate": defaultdict(set),
            "synthetic_tpp": defaultdict(set),
            "calibration": defaultdict(set),
            "synthetic_tpp_only": defaultdict(set),
            "calibration_only": defaultdict(set),
        },
    }


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.registry = pipeline_registry.load_registry()
        pipeline_registry.validate_registry(self.registry)

    def test_status_is_aggregated_unless_details_are_requested(self):
        audit = audit_result(
            [
                work_unit(
                    "poll_trend_outputs:2026vic:@TPP",
                    "poll_trend_outputs",
                    "generate_poll_trends",
                    "stale",
                    "2026vic",
                    "@TPP",
                )
            ]
        )

        status = pipeline.build_status(audit, self.registry)

        self.assertNotIn("work_units", status)
        self.assertEqual(
            status["by_run_class"]["regular"]["stale"], 1
        )
        detailed = pipeline.build_status(
            audit, self.registry, include_details=True
        )
        self.assertEqual(len(detailed["work_units"]), 1)

    def test_regular_plan_groups_party_records_into_one_election(self):
        audit = audit_result(
            [
                work_unit(
                    "poll_trend_outputs:2026vic:@TPP",
                    "poll_trend_outputs",
                    "generate_poll_trends",
                    "legacy",
                    "2026vic",
                    "@TPP",
                ),
                work_unit(
                    "poll_trend_outputs:2026vic:ONP FP",
                    "poll_trend_outputs",
                    "generate_poll_trends",
                    "stale",
                    "2026vic",
                    "ONP FP",
                ),
            ]
        )

        plan = pipeline.build_plan(audit, self.registry, {"regular"})

        self.assertEqual(len(plan["tasks"]), 1)
        task = plan["tasks"][0]
        self.assertEqual(task["election"], "2026vic")
        self.assertEqual(task["status"], "legacy")
        self.assertEqual(task["status_counts"]["legacy"], 1)
        self.assertEqual(task["status_counts"]["stale"], 1)
        self.assertEqual(
            task["command"][1:],
            ["run_fp_model.py", "--election", "2026-vic"],
        )

    def test_calibration_profile_does_not_refresh_downstream(self):
        calibration = work_unit(
            "bias_calibration_outputs:1987fed:@TPP",
            "bias_calibration_outputs",
            "calibrate_pollster_bias",
            "legacy",
            "1987fed",
            "@TPP",
            target_match=False,
        )
        pollsters = work_unit(
            "pollster_parameters:2026vic",
            "pollster_parameters",
            "analyse_pollsters",
            "stale",
            "2026vic",
            dependencies=[calibration["id"]],
        )
        pure = work_unit(
            "pure_poll_outputs:2026vic:@TPP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "stale",
            "2026vic",
            "@TPP",
            dependencies=[pollsters["id"]],
        )
        final = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2026vic",
            "@TPP",
            dependencies=[pollsters["id"], pure["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([calibration, pollsters, pure, final]),
            self.registry,
            {"calibration"},
        )

        self.assertEqual(
            [
                (task["stage"], task["election"])
                for task in plan["tasks"]
            ],
            [("calibrate_pollster_bias", "1987fed")],
        )

    def test_calibration_executor_runs_and_rechecks_each_task(self):
        task_one = {
            "stage": "calibrate_pollsters",
            "run_class": "calibration",
            "election": "1987fed",
            "party": None,
            "status": "legacy",
            "status_counts": {"legacy": 1},
            "work_units": ["poll_calibration_summaries:1987fed"],
            "command": ["python", "run_fp_model.py", "--calibrate"],
            "working_directory": ".",
        }
        task_two = {
            **task_one,
            "stage": "calibrate_pollster_bias",
            "work_units": ["bias_calibration_outputs:1987fed"],
            "command": ["python", "run_fp_model.py", "--bias"],
        }
        initial = {
            "profiles": ["calibration"],
            "blockers": [],
            "tasks": [task_one, task_two],
        }
        refreshed = iter(
            [
                {
                    "profiles": ["calibration"],
                    "blockers": [],
                    "tasks": [task_two],
                },
                {
                    "profiles": ["calibration"],
                    "blockers": [],
                    "tasks": [],
                },
            ]
        )

        with mock.patch.object(
            pipeline.subprocess,
            "run",
            return_value=mock.Mock(returncode=0),
        ) as run:
            pipeline.execute_calibration_plan(initial, lambda: next(refreshed))

        self.assertEqual(run.call_count, 2)
        self.assertEqual(
            run.call_args_list[0].args[0],
            task_one["command"],
        )

    def test_calibration_executor_stops_on_command_failure(self):
        task = {
            "stage": "calibrate_pollsters",
            "run_class": "calibration",
            "election": "1987fed",
            "party": None,
            "status": "legacy",
            "status_counts": {"legacy": 1},
            "work_units": ["poll_calibration_summaries:1987fed"],
            "command": ["python", "run_fp_model.py", "--calibrate"],
            "working_directory": ".",
        }
        plan = {
            "profiles": ["calibration"],
            "blockers": [],
            "tasks": [task],
        }
        refresh = mock.Mock()

        with mock.patch.object(
            pipeline.subprocess,
            "run",
            return_value=mock.Mock(returncode=7),
        ):
            with self.assertRaisesRegex(
                pipeline.PipelineError, "exit code 7"
            ):
                pipeline.execute_calibration_plan(plan, refresh)

        refresh.assert_not_called()

    def test_calibration_executor_requires_task_to_clear(self):
        task = {
            "stage": "calibrate_pollsters",
            "run_class": "calibration",
            "election": "1987fed",
            "party": None,
            "status": "legacy",
            "status_counts": {"legacy": 1},
            "work_units": ["poll_calibration_summaries:1987fed"],
            "command": ["python", "run_fp_model.py", "--calibrate"],
            "working_directory": ".",
        }
        plan = {
            "profiles": ["calibration"],
            "blockers": [],
            "tasks": [task],
        }

        with mock.patch.object(
            pipeline.subprocess,
            "run",
            return_value=mock.Mock(returncode=0),
        ):
            with self.assertRaises(KeyboardInterrupt):
                pipeline.execute_calibration_plan(
                    plan,
                    lambda: plan,
                    input_func=mock.Mock(side_effect=KeyboardInterrupt),
                )

    def test_post_task_provenance_failure_retries_after_input(self):
        task = {
            "stage": "calibrate_pollsters",
            "election": "1987fed",
            "party": None,
            "work_units": ["poll_calibration_summaries:1987fed"],
        }
        blocked = {
            "blockers": [{"message": "temporary source mismatch"}],
            "tasks": [],
        }
        clear = {"blockers": [], "tasks": []}
        refresh = mock.Mock(side_effect=[blocked, clear])
        input_func = mock.Mock(return_value="")

        result = pipeline._refresh_after_task(
            task, refresh, input_func=input_func
        )

        self.assertIs(result, clear)
        input_func.assert_called_once_with()

    def test_metadata_plan_excludes_data_stale_work(self):
        metadata_only = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "provenance-stale",
            "2026vic",
            "@TPP",
        )
        metadata_only["issues"] = [
            {
                "code": "provenance_only_revision",
                "root_category": "fp_model_provenance_script",
                "message": "metadata upgrade required",
            }
        ]
        data_stale = work_unit(
            "poll_trend_outputs:2026vic:ONP FP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2026vic",
            "ONP FP",
        )

        plan = pipeline.build_plan(
            audit_result([metadata_only, data_stale]),
            self.registry,
            {"metadata"},
        )

        self.assertEqual(len(plan["tasks"]), 1)
        self.assertEqual(
            plan["tasks"][0]["work_units"],
            ["poll_trend_outputs:2026vic:@TPP"],
        )

    def test_metadata_executor_uses_maintenance_without_subprocess(self):
        task = {
            "stage": "maintain_provenance",
            "source_stage": "generate_poll_trends",
            "run_class": "metadata",
            "election": "2026vic",
            "party": "@TPP",
            "status": "provenance-stale",
            "status_counts": {"provenance-stale": 1},
            "work_units": ["poll_trend_outputs:2026vic:@TPP"],
            "manifest": "Outputs/test-generated-provenance.json",
            "command": ["metadata-maintenance"],
            "working_directory": ".",
        }
        plan = {
            "profiles": ["metadata"],
            "blockers": [],
            "tasks": [task],
        }
        clear = {
            "profiles": ["metadata"],
            "blockers": [],
            "tasks": [],
        }

        with mock.patch.object(
            pipeline.provenance_maintenance,
            "maintain_record",
            return_value=2,
        ) as maintain, mock.patch.object(
            pipeline.subprocess, "run"
        ) as subprocess_run:
            pipeline.execute_metadata_plan(plan, lambda: clear)

        maintain.assert_called_once()
        subprocess_run.assert_not_called()

    def test_all_profile_refreshes_downstream_of_planned_work(self):
        calibration = work_unit(
            "bias_calibration_outputs:1987fed:@TPP",
            "bias_calibration_outputs",
            "calibrate_pollster_bias",
            "legacy",
            "1987fed",
            "@TPP",
            target_match=False,
        )
        pollsters = work_unit(
            "pollster_parameters:2026vic",
            "pollster_parameters",
            "analyse_pollsters",
            "current",
            "2026vic",
            dependencies=[calibration["id"]],
        )
        pure = work_unit(
            "pure_poll_outputs:2026vic:@TPP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "current",
            "2026vic",
            "@TPP",
            dependencies=[pollsters["id"]],
        )
        final = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "current",
            "2026vic",
            "@TPP",
            dependencies=[pollsters["id"], pure["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([calibration, pollsters, pure, final]),
            self.registry,
            {"all"},
        )

        self.assertEqual(
            [
                (task["stage"], task["status"])
                for task in plan["tasks"]
            ],
            [
                ("calibrate_pollster_bias", "legacy"),
                ("analyse_pollsters", "dependency-refresh"),
                ("generate_pure_poll_trends", "dependency-refresh"),
                ("generate_poll_trends", "dependency-refresh"),
            ],
        )

    def test_regular_profile_refreshes_changed_target_pollster_analysis(self):
        calibration = work_unit(
            "bias_calibration_outputs:1987fed:@TPP",
            "bias_calibration_outputs",
            "calibrate_pollster_bias",
            "current",
            "1987fed",
            "@TPP",
            target_match=False,
        )
        pollsters = work_unit(
            "pollster_parameters:2026vic",
            "pollster_parameters",
            "analyse_pollsters",
            "stale",
            "2026vic",
            dependencies=[calibration["id"]],
        )
        pollsters["issues"] = [
            {
                "code": "changed_generated_dependency",
                "root_category": "bias_calibration_outputs",
                "message": "changed dependency bias_calibration_outputs",
            }
        ]
        final = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2026vic",
            "@TPP",
            dependencies=[pollsters["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([calibration, pollsters, final]),
            self.registry,
            {"regular"},
        )

        self.assertEqual(
            [
                (task["stage"], task["election"])
                for task in plan["tasks"]
            ],
            [
                ("analyse_pollsters", "2026vic"),
                ("generate_poll_trends", "2026vic"),
            ],
        )

    def test_regular_plan_ignores_cutoff_blocker(self):
        audit = audit_result(
            [
                work_unit(
                    "poll_trend_outputs:2026vic:@TPP",
                    "poll_trend_outputs",
                    "generate_poll_trends",
                    "stale",
                    "2026vic",
                    "@TPP",
                ),
                work_unit(
                    "cutoff_poll_outputs:1990fed",
                    "cutoff_poll_outputs",
                    "generate_cutoff_poll_trends",
                    "altered",
                    "1990fed",
                    blocking=True,
                ),
            ]
        )

        plan = pipeline.build_plan(audit, self.registry, {"regular"})

        self.assertEqual(plan["blockers"], [])
        self.assertEqual(len(plan["tasks"]), 1)

    def test_cutoff_plan_is_blocked_by_altered_cutoff(self):
        audit = audit_result(
            [
                work_unit(
                    "cutoff_poll_outputs:1990fed",
                    "cutoff_poll_outputs",
                    "generate_cutoff_poll_trends",
                    "altered",
                    "1990fed",
                    blocking=True,
                )
            ]
        )

        plan = pipeline.build_plan(audit, self.registry, {"cutoffs"})

        self.assertEqual(plan["tasks"], [])
        self.assertEqual(plan["blockers"][0]["stage"],
                         "generate_cutoff_poll_trends")

    def test_cutoff_plan_includes_historical_target_prerequisites(self):
        historical = work_unit(
            "cutoff_poll_outputs:2025wa",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "legacy",
            "2025wa",
            target_match=False,
        )

        plan = pipeline.build_plan(
            audit_result([historical]),
            self.registry,
            {"cutoffs"},
        )

        self.assertEqual(len(plan["tasks"]), 1)
        self.assertEqual(plan["tasks"][0]["election"], "2025wa")

    def test_regular_plan_accepts_inherited_only_staleness(self):
        inherited = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2026vic",
            "@TPP",
        )
        inherited["issues"] = [
            {
                "code": "stale_generated_dependency",
                "root_category": "pollster_parameters",
                "message": "stale generated dependency pollster_parameters",
            },
        ]

        plan = pipeline.build_plan(
            audit_result([inherited]),
            self.registry,
            {"regular"},
        )

        self.assertEqual(plan["tasks"], [])
        self.assertEqual(
            plan["accepted_stale_work_units"],
            ["poll_trend_outputs:2026vic:@TPP"],
        )

    def test_cutoff_plan_refreshes_all_upstream_dependencies_first(self):
        calibration = work_unit(
            "bias_calibration_outputs:1987fed:@TPP",
            "bias_calibration_outputs",
            "calibrate_pollster_bias",
            "legacy",
            "1987fed",
            "@TPP",
            target_match=False,
        )
        pollsters = work_unit(
            "pollster_parameters:1987fed",
            "pollster_parameters",
            "analyse_pollsters",
            "stale",
            "1987fed",
            target_match=False,
            dependencies=[calibration["id"]],
        )
        pure = work_unit(
            "pure_poll_outputs:1987fed:@TPP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "stale",
            "1987fed",
            "@TPP",
            target_match=False,
            dependencies=[pollsters["id"]],
        )
        cutoff = work_unit(
            "cutoff_poll_outputs:1987fed",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "stale",
            "1987fed",
            target_match=False,
            dependencies=[pollsters["id"], pure["id"]],
        )
        unrelated_target_calibration = work_unit(
            "bias_calibration_outputs:2026vic:@TPP",
            "bias_calibration_outputs",
            "calibrate_pollster_bias",
            "legacy",
            "2026vic",
            "@TPP",
            target_match=True,
        )
        for work in (pollsters, pure, cutoff):
            work["issues"] = [
                {
                    "code": "stale_generated_dependency",
                    "root_category": "bias_calibration_outputs",
                    "message": "stale calibration ancestry",
                },
            ]

        plan = pipeline.build_plan(
            audit_result([
                calibration,
                pollsters,
                pure,
                cutoff,
                unrelated_target_calibration,
            ]),
            self.registry,
            {"cutoffs"},
        )

        self.assertTrue(plan["requires_fresh_dependencies"])
        self.assertEqual(plan["root_run_classes"], ["cutoffs"])
        self.assertEqual(plan["accepted_stale_work_units"], [])
        self.assertEqual(
            [task["stage"] for task in plan["tasks"]],
            [
                "calibrate_pollster_bias",
                "analyse_pollsters",
                "generate_pure_poll_trends",
                "generate_cutoff_poll_trends",
            ],
        )

    def test_changed_current_dependency_remains_actionable(self):
        actionable = work_unit(
            "cutoff_poll_outputs:1975fed",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "stale",
            "1975fed",
            target_match=False,
        )
        actionable["issues"] = [
            {
                "code": "stale_generated_dependency",
                "root_category": "pollster_parameters",
                "message": "stale generated dependency pollster_parameters",
            },
            {
                "code": "changed_dependency",
                "root_category": "synthetic_tpp_outputs",
                "message": "changed dependency synthetic_tpp_outputs",
            },
        ]

        plan = pipeline.build_plan(
            audit_result([actionable]),
            self.registry,
            {"cutoffs"},
        )

        self.assertEqual(len(plan["tasks"]), 1)
        self.assertEqual(plan["accepted_stale_work_units"], [])

    def test_approval_profile_orders_pure_before_final(self):
        audit = audit_result(
            [
                work_unit(
                    "poll_trend_outputs:2026vic:@TPP",
                    "poll_trend_outputs",
                    "generate_poll_trends",
                    "stale",
                    "2026vic",
                    "@TPP",
                ),
                work_unit(
                    "pure_poll_outputs:2026vic:@TPP",
                    "pure_poll_outputs",
                    "generate_pure_poll_trends",
                    "legacy",
                    "2026vic",
                    "@TPP",
                ),
            ]
        )

        plan = pipeline.build_plan(
            audit, self.registry, {"regular-with-approvals"}
        )

        self.assertEqual(
            [task["stage"] for task in plan["tasks"]],
            ["generate_pure_poll_trends", "generate_poll_trends"],
        )

    def test_regular_plan_follows_direct_final_trend_dependency(self):
        federal_key = "poll_trend_outputs:2028fed:ONP FP"
        target = work_unit(
            "poll_trend_outputs:2026vic:ONP FP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2026vic",
            "ONP FP",
            dependencies=[
                "test-generated-provenance.json::{}".format(federal_key)
            ],
        )
        federal = work_unit(
            federal_key,
            "poll_trend_outputs",
            "generate_poll_trends",
            "legacy",
            "2028fed",
            "ONP FP",
            target_match=False,
        )

        plan = pipeline.build_plan(
            audit_result([target, federal]),
            self.registry,
            {"regular"},
        )

        self.assertEqual(
            [task["election"] for task in plan["tasks"]],
            ["2028fed", "2026vic"],
        )

    def test_regular_plan_does_not_cross_unselected_pure_branch(self):
        final_key = "poll_trend_outputs:1990fed:DEM FP"
        pure_key = "pure_poll_outputs:1992vic:DEM FP"
        target = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2026vic",
            "@TPP",
            dependencies=[
                "test-generated-provenance.json::{}".format(pure_key)
            ],
        )
        pure = work_unit(
            pure_key,
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "legacy",
            "1992vic",
            "DEM FP",
            target_match=False,
            dependencies=[
                "test-generated-provenance.json::{}".format(final_key)
            ],
        )
        historical_final = work_unit(
            final_key,
            "poll_trend_outputs",
            "generate_poll_trends",
            "legacy",
            "1990fed",
            "DEM FP",
            target_match=False,
        )

        regular = pipeline.build_plan(
            audit_result([target, pure, historical_final]),
            self.registry,
            {"regular"},
        )
        approvals = pipeline.build_plan(
            audit_result([target, pure, historical_final]),
            self.registry,
            {"regular-with-approvals"},
        )

        self.assertEqual(
            [task["election"] for task in regular["tasks"]],
            ["2026vic"],
        )
        self.assertEqual(
            [task["election"] for task in approvals["tasks"]],
            ["1992vic", "2026vic"],
        )

    def test_cutoff_plan_ignores_obsolete_normal_trend_dependency(self):
        final_key = "poll_trend_outputs:1990fed:DEM FP"
        historical_final = work_unit(
            final_key,
            "poll_trend_outputs",
            "generate_poll_trends",
            "legacy",
            "1990fed",
            "DEM FP",
            target_match=False,
        )
        cutoff = work_unit(
            "cutoff_poll_outputs:1993sa",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "stale",
            "1993sa",
            target_match=False,
            dependencies=[historical_final["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([cutoff, historical_final]),
            self.registry,
            {"all"},
        )

        self.assertEqual(
            [
                (task["stage"], task["election"])
                for task in plan["tasks"]
            ],
            [("generate_cutoff_poll_trends", "1993sa")],
        )

    def test_federal_tasks_precede_state_tasks_within_a_stage(self):
        audit = audit_result(
            [
                work_unit(
                    "pure_poll_outputs:2026vic:@TPP",
                    "pure_poll_outputs",
                    "generate_pure_poll_trends",
                    "legacy",
                    "2026vic",
                    "@TPP",
                ),
                work_unit(
                    "pure_poll_outputs:2028fed:@TPP",
                    "pure_poll_outputs",
                    "generate_pure_poll_trends",
                    "stale",
                    "2028fed",
                    "@TPP",
                ),
            ]
        )

        plan = pipeline.build_plan(
            audit, self.registry, {"regular-with-approvals"}
        )

        self.assertEqual(
            [task["election"] for task in plan["tasks"]],
            ["2028fed", "2026vic"],
        )

    def test_unregistered_source_change_blocks_every_profile(self):
        source_issue = {
            "category": "raw_poll_data",
            "status": "blocked",
            "code": "unregistered_source_change",
            "change_kind": "modified",
            "path": "poll-data-vic.csv",
            "message": "unregistered modified file poll-data-vic.csv",
        }
        audit = audit_result([], source_issues=[source_issue])

        plan = pipeline.build_plan(audit, self.registry, {"regular"})

        self.assertEqual(plan["tasks"], [])
        self.assertEqual(
            plan["blockers"][0]["code"], "unregistered_source_change"
        )

    def test_missing_regional_record_maps_to_party_argument(self):
        audit = audit_result(
            [
                work_unit(
                    "regional_swing_deviations:2028qld:ONP FP",
                    "regional_swing_deviations",
                    "generate_regional_swings",
                    "missing",
                    "2028qld",
                    "ONP FP",
                )
            ]
        )

        plan = pipeline.build_plan(audit, self.registry, {"all"})

        self.assertEqual(len(plan["tasks"]), 1)
        self.assertEqual(
            plan["tasks"][0]["command"][1:],
            [
                "region_model.py",
                "--election",
                "2028-qld",
                "--party",
                "ON",
            ],
        )

    def test_no_arguments_open_interactive_interface(self):
        with mock.patch.object(
            pipeline, "_interactive_select", return_value="exit"
        ):
            self.assertEqual(pipeline.main([]), 0)

    def test_text_plan_is_concise_unless_details_are_requested(self):
        audit = audit_result(
            [
                work_unit(
                    "poll_trend_outputs:2026vic:@TPP",
                    "poll_trend_outputs",
                    "generate_poll_trends",
                    "stale",
                    "2026vic",
                    "@TPP",
                )
            ]
        )
        plan = pipeline.build_plan(audit, self.registry, {"regular"})

        concise = StringIO()
        with redirect_stdout(concise):
            pipeline.print_plan(plan)
        self.assertIn("Use --details", concise.getvalue())
        self.assertNotIn("run_fp_model.py", concise.getvalue())

        detailed = StringIO()
        with redirect_stdout(detailed):
            pipeline.print_plan(plan, include_details=True)
        self.assertIn("run_fp_model.py", detailed.getvalue())


if __name__ == "__main__":
    unittest.main()

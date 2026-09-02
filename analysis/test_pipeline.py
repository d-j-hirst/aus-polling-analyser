import json
import sys
import tempfile
import unittest
from collections import defaultdict
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
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
        "cutoff_root_causes": {},
        "calibration_root_causes": {},
        "impacts": {
            "immediate": defaultdict(set),
            "synthetic_tpp": defaultdict(set),
            "cutoff": defaultdict(set),
            "calibration": defaultdict(set),
            "synthetic_tpp_only": defaultdict(set),
            "cutoff_only": defaultdict(set),
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

    def test_diagnostic_calibration_traces_are_not_executable_tasks(self):
        trace = work_unit(
            "poll_calibration_traces:1991nsw:@TPP:full",
            "poll_calibration_traces",
            "calibrate_pollsters",
            "legacy",
            "1991nsw",
            "@TPP",
            target_match=False,
        )
        current_summary = work_unit(
            "poll_calibration_summaries:2028fed",
            "poll_calibration_summaries",
            "calibrate_pollsters",
            "stale",
            "2028fed",
        )

        plan = pipeline.build_plan(
            audit_result([trace, current_summary]),
            self.registry,
            {"calibration"},
        )

        self.assertEqual(
            [(task["stage"], task["election"]) for task in plan["tasks"]],
            [("calibrate_pollsters", "2028fed")],
        )

    def test_run_log_tees_subprocess_output_and_records_lifecycle(self):
        terminal_stdout = StringIO()
        terminal_stderr = StringIO()
        with tempfile.TemporaryDirectory() as temporary_directory:
            with redirect_stdout(terminal_stdout), redirect_stderr(
                terminal_stderr
            ):
                with pipeline.PipelineRunLog(
                    "regular",
                    ["2026vic"],
                    log_directory=Path(temporary_directory),
                ) as run_log:
                    run_log.event("PLAN READY", tasks=1)
                    result = run_log.run_command(
                        [
                            sys.executable,
                            "-c",
                            (
                                "import sys; "
                                "print('standard output', flush=True); "
                                "print('standard error', file=sys.stderr, "
                                "flush=True)"
                            ),
                        ],
                        Path(temporary_directory),
                    )
                log_text = run_log.path.read_text(encoding="utf-8")

        self.assertEqual(result.returncode, 0)
        self.assertIn("standard output", terminal_stdout.getvalue())
        self.assertIn("standard error", terminal_stderr.getvalue())
        self.assertIn("RUN START", log_text)
        self.assertIn("PLAN READY", log_text)
        self.assertIn("standard output", log_text)
        self.assertIn("standard error", log_text)
        self.assertIn("RUN COMPLETE", log_text)

    def test_fresh_plan_uses_a_new_python_process(self):
        expected = {
            "profiles": ["calibration"],
            "blockers": [],
            "tasks": [],
        }
        completed = mock.Mock(
            returncode=0,
            stdout=json.dumps(expected),
            stderr="",
        )

        with mock.patch.object(
            pipeline.subprocess, "run", return_value=completed
        ) as run:
            result = pipeline._load_plan_fresh(
                {"2028fed", "2026sa"}, {"calibration"}
            )

        self.assertEqual(result, expected)
        run.assert_called_once_with(
            [
                sys.executable,
                str(Path(pipeline.__file__).resolve()),
                "plan",
                "--format",
                "json",
                "--election",
                "2026sa",
                "--election",
                "2028fed",
                "--profile",
                "calibration",
            ],
            cwd=str(pipeline.ANALYSIS_DIRECTORY),
            stdout=pipeline.subprocess.PIPE,
            stderr=None,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

    def test_fresh_all_plan_has_no_election_arguments(self):
        expected = {"profiles": ["all"], "blockers": [], "tasks": []}
        completed = mock.Mock(returncode=0, stdout=json.dumps(expected))

        with mock.patch.object(
            pipeline.subprocess, "run", return_value=completed
        ) as run:
            result = pipeline._load_plan_fresh(None, {"all"})

        self.assertEqual(result, expected)
        command = run.call_args.args[0]
        self.assertNotIn("--election", command)
        self.assertEqual(command[-2:], ["--profile", "all"])

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

    def test_calibration_profile_schedules_missing_federal_prior_before_state_bias(self):
        prior = work_unit(
            "federal_calibration_priors:1984fed",
            "federal_calibration_priors",
            "calibrate_pollsters",
            "missing",
            "1984fed",
        )
        bias = work_unit(
            "bias_calibration_compatibility_inputs:1988nsw:@TPP",
            "bias_calibration_compatibility_inputs",
            "calibrate_pollster_bias",
            "stale",
            "1988nsw",
            "@TPP",
            dependencies=[prior["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([bias, prior]),
            self.registry,
            {"calibration"},
        )

        self.assertEqual(
            [
                (task["stage"], task["election"])
                for task in plan["tasks"]
            ],
            [
                ("calibrate_pollsters", "1984fed"),
                ("calibrate_pollster_bias", "1988nsw"),
            ],
        )

    def test_calibration_profile_does_not_refresh_downstream(self):
        calibration = work_unit(
            "bias_calibration_compatibility_inputs:1987fed:@TPP",
            "bias_calibration_compatibility_inputs",
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

    def test_calibration_profile_compacts_current_legacy_inputs(self):
        compatibility = work_unit(
            "bias_calibration_compatibility_inputs:1987fed:@TPP",
            "bias_calibration_compatibility_inputs",
            "calibrate_pollster_bias",
            "current",
            "1987fed",
            "@TPP",
            target_match=False,
        )
        summary = work_unit(
            "poll_calibration_summaries:1987fed:compact",
            "poll_calibration_summaries",
            "compact_calibration_summaries",
            "missing",
            "1987fed",
            target_match=False,
            dependencies=[compatibility["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([compatibility, summary]),
            self.registry,
            {"calibration"},
        )

        self.assertEqual(
            [(task["stage"], task["election"]) for task in plan["tasks"]],
            [("compact_calibration_summaries", "1987fed")],
        )

    def test_calibration_profile_schedules_missing_abridged_loo_before_compact(
        self,
    ):
        loo = work_unit(
            "calibration_compatibility_inputs:1988vic:loo-summary",
            "poll_calibration_compatibility_inputs",
            "calibrate_pollsters",
            "missing",
            "1988vic",
            target_match=False,
        )
        # Calibration path units must carry the calibration path class so
        # profile reachability matches production audits.
        loo["path_classes"] = ["calibration"]
        bias = work_unit(
            "bias_calibration_compatibility_inputs:1988vic:summary:"
            "bias-staging",
            "bias_calibration_compatibility_inputs",
            "calibrate_pollster_bias",
            "current",
            "1988vic",
            target_match=False,
        )
        bias["path_classes"] = ["calibration"]
        summary = work_unit(
            "poll_calibration_summaries:1988vic:compact",
            "poll_calibration_summaries",
            "compact_calibration_summaries",
            "stale",
            "1988vic",
            target_match=False,
            dependencies=[loo["id"], bias["id"]],
        )
        summary["path_classes"] = ["calibration"]

        calibration_plan = pipeline.build_plan(
            audit_result([loo, bias, summary]),
            self.registry,
            {"calibration"},
        )
        self.assertEqual(
            [
                (task["stage"], task["election"])
                for task in calibration_plan["tasks"]
            ],
            [
                ("calibrate_pollsters", "1988vic"),
                ("compact_calibration_summaries", "1988vic"),
            ],
        )

        regular_plan = pipeline.build_plan(
            audit_result([loo, bias, summary]),
            self.registry,
            {"regular"},
        )
        self.assertEqual(regular_plan["tasks"], [])

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

    def test_generation_executor_runs_regular_profile(self):
        task = {
            "stage": "generate_poll_trends",
            "run_class": "regular",
            "election": "2026vic",
            "party": None,
            "status": "stale",
            "status_counts": {"stale": 1},
            "work_units": ["poll_trend_outputs:2026vic:@TPP"],
            "command": [
                "python",
                "run_fp_model.py",
                "--election",
                "2026-vic",
            ],
            "working_directory": ".",
        }
        plan = {
            "profiles": ["regular"],
            "blockers": [],
            "tasks": [task],
        }
        clear = {
            "profiles": ["regular"],
            "blockers": [],
            "tasks": [],
        }

        output = StringIO()
        with redirect_stdout(output), mock.patch.object(
            pipeline.subprocess,
            "run",
            return_value=mock.Mock(returncode=0),
        ) as run:
            pipeline.execute_generation_plan(plan, lambda: clear)

        run.assert_called_once_with(
            task["command"],
            cwd=str(pipeline.ANALYSIS_DIRECTORY.resolve()),
        )
        self.assertIn(
            "============================================================\n"
            "PIPELINE PRIMARY TASK 1/1 RECORDED AS COMPLETE\n"
            "============================================================",
            output.getvalue(),
        )

    def test_generation_executor_records_task_lifecycle(self):
        task = {
            "stage": "generate_poll_trends",
            "run_class": "regular",
            "election": "2026vic",
            "party": None,
            "status": "stale",
            "status_counts": {"stale": 1},
            "work_units": ["poll_trend_outputs:2026vic:@TPP"],
            "command": ["python", "run_fp_model.py", "--election", "2026-vic"],
            "working_directory": ".",
        }
        plan = {
            "profiles": ["regular"],
            "blockers": [],
            "tasks": [task],
        }
        clear = {
            "profiles": ["regular"],
            "blockers": [],
            "tasks": [],
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            with pipeline.PipelineRunLog(
                "regular",
                ["2026vic"],
                log_directory=Path(temporary_directory),
            ) as run_log, mock.patch.object(
                run_log,
                "run_command",
                return_value=mock.Mock(returncode=0),
            ):
                pipeline.execute_generation_plan(
                    plan,
                    lambda: clear,
                    run_log=run_log,
                )
            log_text = run_log.path.read_text(encoding="utf-8")

        self.assertIn("TASK START", log_text)
        self.assertIn("TASK VERIFIED", log_text)
        self.assertIn("PLAN COMPLETE", log_text)
        self.assertIn('"generate_poll_trends"', log_text)

    def test_generation_executor_accepts_all_profile(self):
        plan = {
            "profiles": ["all"],
            "blockers": [],
            "tasks": [],
        }

        result = pipeline.execute_generation_plan(plan, lambda: plan)

        self.assertEqual(result["completed"], 0)

    def test_all_execution_checks_metadata_after_numerical_work(self):
        generation_plan = {
            "profiles": ["all"],
            "blockers": [],
            "warnings": [],
            "accepted_stale_work_units": [],
            "deferred_tasks": [],
            "target_elections": [],
            "tasks": [],
        }
        metadata_plan = {
            "profiles": ["metadata"],
            "blockers": [],
            "warnings": [],
            "accepted_stale_work_units": [],
            "target_elections": [],
            "tasks": [],
        }
        final_audit = {
            "required_graph": {"required_work_unit_ids": []}
        }

        with tempfile.TemporaryDirectory() as temporary_directory, \
                mock.patch.object(
                    pipeline,
                    "PIPELINE_LOG_DIRECTORY",
                    Path(temporary_directory),
                ), mock.patch.object(
                    pipeline,
                    "_load_audit",
                    return_value=(mock.Mock(), final_audit),
                ), mock.patch.object(
                    pipeline,
                    "load_metadata_plan",
                    return_value=metadata_plan,
                ) as load_metadata, mock.patch.object(
                    pipeline,
                    "execute_metadata_plan",
                    return_value={"completed": 0, "deferred": []},
                ) as execute_metadata:
            result = pipeline._execute_plan_with_log(
                generation_plan,
                lambda: generation_plan,
                [],
            )

        load_metadata.assert_called_once()
        execute_metadata.assert_called_once()
        self.assertIn("generation", result)
        self.assertIn("metadata", result)

    def test_generation_executor_runs_one_automatic_follow_up(self):
        upstream = {
            "stage": "analyse_pollsters",
            "run_class": "pollster_analysis",
            "election": "2026vic",
            "party": None,
            "status": "stale",
            "status_counts": {"stale": 1},
            "work_units": ["pollster_parameters:2026vic"],
            "command": [
                "python",
                "pollster_analysis.py",
                "--election",
                "2026-vic",
            ],
            "working_directory": ".",
        }
        downstream = {
            **upstream,
            "stage": "generate_poll_trends",
            "run_class": "regular",
            "work_units": ["poll_trend_outputs:2026vic:@TPP"],
            "command": [
                "python",
                "run_fp_model.py",
                "--election",
                "2026-vic",
            ],
        }
        initial = {
            "profiles": ["regular"],
            "blockers": [],
            "tasks": [upstream],
        }
        refreshed = iter(
            [
                {
                    "profiles": ["regular"],
                    "blockers": [],
                    "tasks": [downstream],
                },
                {
                    "profiles": ["regular"],
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
            result = pipeline.execute_generation_plan(
                initial, lambda: next(refreshed)
            )

        self.assertEqual(run.call_count, 2)
        self.assertEqual(result["completed"], 2)
        self.assertEqual(result["follow_up_passes"], 1)
        self.assertEqual(result["deferred_tasks"], [])

    def test_generation_executor_prompts_before_further_follow_ups(self):
        task = {
            "stage": "generate_poll_trends",
            "run_class": "regular",
            "election": "2028fed",
            "party": None,
            "status": "stale",
            "status_counts": {"stale": 1},
            "work_units": ["poll_trend_outputs:2028fed:@TPP"],
            "record_refs": [
                {
                    "manifest": "Outputs/generated-provenance.json",
                    "record_key": "poll_trend_outputs:2028fed:@TPP",
                }
            ],
            "issue_signatures": ["old source revision"],
            "command": [
                "python",
                "run_fp_model.py",
                "--election",
                "2028-fed",
            ],
            "working_directory": ".",
        }
        first_update = {
            **task,
            "issue_signatures": ["new source revision"],
        }
        second_update = {
            **task,
            "issue_signatures": ["newer source revision"],
        }
        third_update = {
            **task,
            "issue_signatures": ["newest source revision"],
        }
        initial = {
            "profiles": ["regular"],
            "blockers": [],
            "tasks": [task],
        }
        refreshed = iter(
            [
                {
                    "profiles": ["regular"],
                    "blockers": [],
                    "tasks": [first_update],
                },
                {
                    "profiles": ["regular"],
                    "blockers": [],
                    "tasks": [second_update],
                },
                {
                    "profiles": ["regular"],
                    "blockers": [],
                    "tasks": [second_update],
                },
                {
                    "profiles": ["regular"],
                    "blockers": [],
                    "tasks": [third_update],
                },
            ]
        )
        input_func = mock.Mock(side_effect=["y", "n"])

        with mock.patch.object(
            pipeline.subprocess,
            "run",
            return_value=mock.Mock(returncode=0),
        ), mock.patch.object(
            pipeline,
            "_task_record_markers",
            side_effect=[
                {("manifest", "record"): "old-run"},
                {("manifest", "record"): "run-1"},
                {("manifest", "record"): "run-1"},
                {("manifest", "record"): "run-2"},
                {("manifest", "record"): "run-2"},
                {("manifest", "record"): "run-3"},
            ],
        ) as markers:
            result = pipeline.execute_generation_plan(
                initial,
                lambda: next(refreshed),
                input_func=input_func,
            )

        self.assertEqual(result["completed"], 3)
        self.assertEqual(result["follow_up_passes"], 2)
        self.assertEqual(result["deferred_tasks"], [third_update])
        self.assertEqual(input_func.call_args_list, [mock.call(), mock.call()])
        self.assertEqual(markers.call_count, 6)

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
            "bias_calibration_compatibility_inputs:1987fed:@TPP",
            "bias_calibration_compatibility_inputs",
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
            "bias_calibration_compatibility_inputs:1987fed:@TPP",
            "bias_calibration_compatibility_inputs",
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

    def test_regular_plan_precomputes_downstream_refresh(self):
        pollsters = work_unit(
            "pollster_parameters:2026vic",
            "pollster_parameters",
            "analyse_pollsters",
            "stale",
            "2026vic",
        )
        final = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "current",
            "2026vic",
            "@TPP",
            dependencies=[pollsters["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([pollsters, final]),
            self.registry,
            {"regular"},
        )

        self.assertEqual(
            [
                (task["stage"], task["status"])
                for task in plan["tasks"]
            ],
            [
                ("analyse_pollsters", "stale"),
                ("generate_poll_trends", "dependency-refresh"),
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
        self.assertEqual(
            plan["blockers"][0]["stage"],
            "generate_cutoff_poll_trends",
        )

    def test_metadata_plan_prevalidates_candidates_before_confirmation(self):
        manifest_path = pipeline.ANALYSIS_DIRECTORY / "test-metadata.json"
        manifest = {
            "path_base": ".",
            "records": {
                "poll_trend_outputs:2028fed:@TPP": {
                    "stage": "generate_poll_trends",
                    "scope": generated_provenance.generation_scope(
                        elections=["2028fed"], parties=["@TPP"]
                    ),
                }
            },
        }
        selected = {manifest_path: set(manifest["records"])}
        upgrade = {"event": {"id": "metadata-event"}}

        with mock.patch.object(
            pipeline,
            "_metadata_source_blockers",
            return_value=[],
        ), mock.patch.object(
            pipeline.analysis_provenance,
            "_selected_generated_records",
            return_value=(selected, {}, {}),
        ), mock.patch.object(
            pipeline.generated_provenance,
            "load_manifest",
            return_value=manifest,
        ), mock.patch.object(
            pipeline.provenance_maintenance,
            "pending_upgrades",
            return_value=[upgrade],
        ), mock.patch.object(
            pipeline.provenance_maintenance,
            "can_maintain_record",
            return_value=True,
        ) as can_maintain:
            plan = pipeline.load_metadata_plan({"2028fed"})

        can_maintain.assert_called_once()
        self.assertEqual(len(plan["tasks"]), 1)
        self.assertEqual(
            plan["tasks"][0]["work_units"],
            ["poll_trend_outputs:2028fed:@TPP"],
        )

    def test_metadata_plan_can_limit_candidates_to_required_graph(self):
        manifest = {
            "path_base": ".",
            "records": {
                "poll_trend_outputs:2028fed:@TPP": {
                    "stage": "generate_poll_trends",
                    "scope": generated_provenance.generation_scope(
                        elections=["2028fed"], parties=["@TPP"]
                    ),
                },
                "poll_trend_outputs:2028qld:@TPP": {
                    "stage": "generate_poll_trends",
                    "scope": generated_provenance.generation_scope(
                        elections=["2028qld"], parties=["@TPP"]
                    ),
                },
            },
        }

        with mock.patch.object(
            pipeline, "_metadata_source_blockers", return_value=[]
        ), mock.patch.object(
            pipeline.generated_provenance,
            "load_manifest",
            return_value=manifest,
        ), mock.patch.object(
            pipeline.provenance_maintenance,
            "pending_upgrades",
            return_value=[{"event": {"id": "metadata-event"}}],
        ), mock.patch.object(
            pipeline.provenance_maintenance,
            "can_maintain_record",
            return_value=True,
        ):
            plan = pipeline.load_metadata_plan(
                None,
                required_work_unit_ids={
                    "test-metadata.json::"
                    "poll_trend_outputs:2028fed:@TPP"
                },
            )

        self.assertEqual(
            [task["work_units"][0] for task in plan["tasks"]],
            ["poll_trend_outputs:2028fed:@TPP"],
        )

    def test_metadata_plan_defers_candidates_needing_regeneration(self):
        manifest_path = pipeline.ANALYSIS_DIRECTORY / "test-metadata.json"
        manifest = {
            "path_base": ".",
            "records": {
                "poll_trend_outputs:2028fed:@TPP": {
                    "stage": "generate_poll_trends",
                    "scope": generated_provenance.generation_scope(
                        elections=["2028fed"], parties=["@TPP"]
                    ),
                }
            },
        }
        selected = {manifest_path: set(manifest["records"])}
        upgrade = {"event": {"id": "metadata-event"}}

        with mock.patch.object(
            pipeline,
            "_metadata_source_blockers",
            return_value=[],
        ), mock.patch.object(
            pipeline.analysis_provenance,
            "_selected_generated_records",
            return_value=(selected, {}, {}),
        ), mock.patch.object(
            pipeline.generated_provenance,
            "load_manifest",
            return_value=manifest,
        ), mock.patch.object(
            pipeline.provenance_maintenance,
            "pending_upgrades",
            return_value=[upgrade],
        ), mock.patch.object(
            pipeline.provenance_maintenance,
            "can_maintain_record",
            return_value=False,
        ):
            plan = pipeline.load_metadata_plan({"2028fed"})

        self.assertEqual(plan["tasks"], [])
        self.assertEqual(
            plan["warnings"][1]["message"],
            "1 metadata candidate(s) require data regeneration and were deferred.",
        )

    def test_metadata_executor_defers_changed_candidate_and_continues(self):
        first = {
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
        second = dict(first)
        second["work_units"] = ["poll_trend_outputs:2028fed:@TPP"]
        plan = {"profiles": ["metadata"], "blockers": [], "tasks": [first, second]}

        with mock.patch.object(
            pipeline.provenance_maintenance,
            "maintain_record",
            side_effect=[
                pipeline.provenance_maintenance.ProvenanceMaintenanceError(
                    "requires data regeneration"
                ),
                1,
            ],
        ) as maintain:
            result = pipeline.execute_metadata_plan(plan, lambda: plan)

        self.assertEqual(maintain.call_count, 2)
        self.assertEqual(result["completed"], 1)
        self.assertEqual(len(result["deferred"]), 1)

    def test_metadata_cli_uses_fast_plan_without_full_audit(self):
        plan = {
            "schema_version": 1,
            "target_elections": ["2028fed"],
            "profiles": ["metadata"],
            "requires_fresh_dependencies": False,
            "root_run_classes": ["metadata"],
            "selected_run_classes": ["metadata"],
            "blockers": [],
            "warnings": [],
            "accepted_stale_work_units": [],
            "tasks": [],
        }
        output = StringIO()
        with mock.patch.object(
            pipeline,
            "load_metadata_plan",
            return_value=plan,
        ) as load_plan, mock.patch.object(
            pipeline,
            "_load_audit",
            side_effect=AssertionError("full audit was called"),
        ), redirect_stdout(output):
            return_code = pipeline.main([
                "plan",
                "--election",
                "2028fed",
                "--profile",
                "metadata",
            ])

        self.assertEqual(return_code, 0)
        load_plan.assert_called_once()

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

    def test_cutoff_plan_refreshes_states_after_federal_cutoff(self):
        federal = work_unit(
            "cutoff_poll_outputs:2025fed",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "stale",
            "2025fed",
            target_match=False,
        )
        state = work_unit(
            "cutoff_poll_outputs:2026sa",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "current",
            "2026sa",
            target_match=False,
            dependencies=[federal["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([state, federal]),
            self.registry,
            {"cutoffs"},
        )

        self.assertEqual(
            [task["election"] for task in plan["tasks"]],
            ["2025fed", "2026sa"],
        )
        self.assertEqual(
            plan["tasks"][1]["status_counts"]["dependency-refresh"],
            1,
        )

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
            "bias_calibration_compatibility_inputs:1987fed:@TPP",
            "bias_calibration_compatibility_inputs",
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
            "bias_calibration_compatibility_inputs:2026vic:@TPP",
            "bias_calibration_compatibility_inputs",
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
                    "root_category": "bias_calibration_compatibility_inputs",
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

    def test_legacy_cutoff_bridge_skips_current_pure_trends(self):
        current_pollsters = work_unit(
            "pollster_parameters:2028fed",
            "pollster_parameters",
            "analyse_pollsters",
            "stale",
            "2028fed",
        )
        current_pure = work_unit(
            "pure_poll_outputs:2028fed:@TPP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "stale",
            "2028fed",
            "@TPP",
            dependencies=[current_pollsters["id"]],
        )
        historical_pure = work_unit(
            "pure_poll_outputs:1993sa:@TPP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "stale",
            "1993sa",
            "@TPP",
            target_match=False,
        )
        diagnostic = work_unit(
            "synthetic_tpp_outputs:sa",
            "synthetic_tpp_outputs",
            "generate_synthetic_tpp",
            "stale",
            "1993sa",
            target_match=False,
            dependencies=[current_pure["id"], historical_pure["id"]],
        )
        cutoff = work_unit(
            "cutoff_poll_outputs:1993sa",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "stale",
            "1993sa",
            target_match=False,
            dependencies=[diagnostic["id"]],
        )

        with mock.patch.object(
            pipeline.analysis_provenance.approvals_provenance,
            "current_elections",
            return_value={"2028fed"},
        ):
            plan = pipeline.build_plan(
                audit_result([
                    current_pollsters,
                    current_pure,
                    historical_pure,
                    diagnostic,
                    cutoff,
                ]),
                self.registry,
                {"cutoffs"},
            )

        self.assertEqual(
            [
                (task["stage"], task["election"])
                for task in plan["tasks"]
            ],
            [
                ("generate_pure_poll_trends", "1993sa"),
                ("generate_cutoff_poll_trends", "1993sa"),
            ],
        )

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

    def test_regular_profile_regenerates_only_target_pure_trend(self):
        target_pure = work_unit(
            "pure_poll_outputs:2026vic:@TPP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "stale",
            "2026vic",
            "@TPP",
        )
        historical_pure = work_unit(
            "pure_poll_outputs:1992vic:@TPP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "stale",
            "1992vic",
            "@TPP",
            target_match=False,
        )
        final = work_unit(
            "poll_trend_outputs:2026vic:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2026vic",
            "@TPP",
            dependencies=[target_pure["id"], historical_pure["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([target_pure, historical_pure, final]),
            self.registry,
            {"regular"},
        )

        self.assertEqual(
            [
                (task["stage"], task["election"])
                for task in plan["tasks"]
            ],
            [
                ("generate_pure_poll_trends", "2026vic"),
                ("generate_poll_trends", "2026vic"),
            ],
        )
        self.assertEqual(
            plan["target_only_run_classes"],
            ["regular_with_approvals"],
        )

    def test_regular_profiles_share_routine_cpp_roots(self):
        expected = {
            "adjustments",
            "analysis",
            "regional",
            "regular",
            "regular_with_approvals",
            "source",
        }

        regular = pipeline._root_run_classes({"regular"}, self.registry)
        approvals = pipeline._root_run_classes(
            {"regular-with-approvals"}, self.registry
        )

        self.assertEqual(regular, expected)
        self.assertEqual(approvals, expected)

    def test_regular_profile_orders_routine_cpp_input_generation(self):
        units = [
            work_unit(
                "election_result_exports:2026vic",
                "election_result_exports",
                "export_election_results",
                "stale",
                "2026vic",
            ),
            work_unit(
                "seat_statistics:all",
                "seat_statistics",
                "analyse_elections",
                "stale",
                "0none",
            ),
            work_unit(
                "pure_poll_outputs:2026vic:@TPP",
                "pure_poll_outputs",
                "generate_pure_poll_trends",
                "stale",
                "2026vic",
                "@TPP",
            ),
            work_unit(
                "poll_trend_outputs:2026vic:@TPP",
                "poll_trend_outputs",
                "generate_poll_trends",
                "stale",
                "2026vic",
                "@TPP",
            ),
            work_unit(
                "trend_adjustments:2026vic:TPP",
                "trend_adjustments",
                "generate_trend_adjustments",
                "stale",
                "2026vic",
                "TPP",
            ),
            work_unit(
                "regional_swing_deviations:2026vic:@TPP",
                "regional_swing_deviations",
                "generate_regional_swings",
                "stale",
                "2026vic",
                "@TPP",
            ),
        ]

        plan = pipeline.build_plan(
            audit_result(units), self.registry, {"regular"}
        )

        self.assertEqual(
            [task["stage"] for task in plan["tasks"]],
            [
                "export_election_results",
                "analyse_elections",
                "generate_pure_poll_trends",
                "generate_poll_trends",
                "generate_trend_adjustments",
                "generate_regional_swings",
            ],
        )

    def test_regular_profile_does_not_launch_result_acquisition(self):
        cache = work_unit(
            "election_result_cache:2026vic",
            "election_result_cache",
            "cache_election_results",
            "stale",
            "2026vic",
        )
        exported = work_unit(
            "election_result_exports:2026vic",
            "election_result_exports",
            "export_election_results",
            "stale",
            "2026vic",
            dependencies=[cache["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([cache, exported]), self.registry, {"regular"}
        )

        self.assertEqual(
            [task["stage"] for task in plan["tasks"]],
            ["export_election_results"],
        )

    def test_regular_profile_defers_whole_adjustment_on_stale_cutoff(self):
        cutoff = work_unit(
            "cutoff_poll_outputs:2023nsw",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "stale",
            "2023nsw",
            target_match=False,
        )
        adjustment = work_unit(
            "trend_adjustments:2027nsw:TPP",
            "trend_adjustments",
            "generate_trend_adjustments",
            "stale",
            "2027nsw",
            "TPP",
            dependencies=[cutoff["id"]],
        )
        adjustment["issues"] = [
            {
                "code": "stale_generated_dependency",
                "root_category": "cutoff_poll_outputs",
                "message": "stale historical cutoff",
            }
        ]
        fundamentals = work_unit(
            "trend_adjustments:2027nsw:fundamentals",
            "fundamentals",
            "generate_trend_adjustments",
            "stale",
            "2027nsw",
            "fundamentals",
        )

        plan = pipeline.build_plan(
            audit_result([cutoff, adjustment, fundamentals]),
            self.registry,
            {"regular"},
        )

        self.assertEqual(plan["tasks"], [])
        self.assertEqual(
            plan["deferred_tasks"],
            [
                {
                    "stage": "generate_trend_adjustments",
                    "election": "2027nsw",
                    "party": None,
                    "waiting_on": ["cutoff_poll_outputs"],
                }
            ],
        )

    def test_regular_profile_runs_adjustment_after_cutoff_is_replaced(self):
        cutoff = work_unit(
            "cutoff_poll_outputs:2023nsw",
            "cutoff_poll_outputs",
            "generate_cutoff_poll_trends",
            "current",
            "2023nsw",
            target_match=False,
        )
        adjustment = work_unit(
            "trend_adjustments:2027nsw:TPP",
            "trend_adjustments",
            "generate_trend_adjustments",
            "stale",
            "2027nsw",
            "TPP",
            dependencies=[cutoff["id"]],
        )
        adjustment["issues"] = [
            {
                "code": "changed_dependency",
                "root_category": "cutoff_poll_outputs",
                "message": "changed historical cutoff",
            }
        ]

        plan = pipeline.build_plan(
            audit_result([cutoff, adjustment]), self.registry, {"regular"}
        )

        self.assertEqual(plan["deferred_tasks"], [])
        self.assertEqual(
            [task["stage"] for task in plan["tasks"]],
            ["generate_trend_adjustments"],
        )

    def test_all_profile_uses_required_historical_and_active_graph(self):
        historical = work_unit(
            "poll_trend_outputs:2025fed:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2025fed",
            "@TPP",
            target_match=False,
        )
        active = work_unit(
            "poll_trend_outputs:2028fed:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2028fed",
            "@TPP",
            target_match=False,
        )
        inactive = work_unit(
            "poll_trend_outputs:2028qld:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2028qld",
            "@TPP",
            target_match=False,
        )
        unreferenced = work_unit(
            "pure_poll_outputs:1992vic:DEM FP",
            "pure_poll_outputs",
            "generate_pure_poll_trends",
            "legacy",
            "1992vic",
            "DEM FP",
            target_match=False,
        )
        audit = audit_result(
            [historical, active, inactive, unreferenced]
        )
        audit["target_elections"] = []
        audit["required_graph"] = {
            "required_work_unit_ids": [historical["id"], active["id"]]
        }

        plan = pipeline.build_plan(audit, self.registry, {"all"})

        self.assertEqual(
            [task["election"] for task in plan["tasks"]],
            ["2025fed", "2028fed"],
        )

    def test_approval_refresh_tolerates_inherited_staleness_with_metadata(self):
        """Metadata upgrades must not force historical calibration reducers."""

        pollsters = work_unit(
            "pollster_parameters:1987fed",
            "pollster_parameters",
            "analyse_pollsters",
            "stale",
            "1987fed",
            target_match=False,
        )
        pollsters["issues"] = [
            {
                "code": "stale_generated_dependency",
                "root_category": "poll_calibration_summaries",
                "message": "stale compact calibration summary",
            },
            {
                "code": "provenance_only_revision",
                "root_category": "pollster_analysis_script",
                "message": "metadata upgrade required",
            },
        ]
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
        pure["issues"] = [
            {
                "code": "stale_generated_dependency",
                "root_category": "pollster_parameters",
                "message": "stale pollster parameters",
            }
        ]
        final = work_unit(
            "poll_trend_outputs:2028fed:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2028fed",
            "@TPP",
            dependencies=[pure["id"]],
        )

        plan = pipeline.build_plan(
            audit_result([pollsters, pure, final]),
            self.registry,
            {"regular-with-approvals"},
        )

        self.assertEqual(
            [(task["stage"], task["election"]) for task in plan["tasks"]],
            [("generate_poll_trends", "2028fed")],
        )
        self.assertEqual(
            plan["accepted_stale_work_units"],
            [
                "pollster_parameters:1987fed",
                "pure_poll_outputs:1987fed:@TPP",
            ],
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

    def test_regular_profile_refreshes_target_regional_models(self):
        audit = audit_result(
            [
                work_unit(
                    "regional_swing_deviations:2028fed:@TPP",
                    "regional_swing_deviations",
                    "generate_regional_swings",
                    "stale",
                    "2028fed",
                    "@TPP",
                ),
                work_unit(
                    "regional_swing_deviations:2028fed:ONP FP",
                    "regional_swing_deviations",
                    "generate_regional_swings",
                    "stale",
                    "2028fed",
                    "ONP FP",
                ),
            ]
        )

        plan = pipeline.build_plan(audit, self.registry, {"regular"})

        self.assertEqual(
            [
                (task["stage"], task["election"], task["party"])
                for task in plan["tasks"]
            ],
            [
                ("generate_regional_swings", "2028fed", "@TPP"),
                ("generate_regional_swings", "2028fed", "ONP FP"),
            ],
        )

    def test_no_arguments_open_interactive_interface(self):
        with mock.patch.object(
            pipeline, "_interactive_select", return_value="exit"
        ):
            self.assertEqual(pipeline.main([]), 0)

    def test_plan_rejects_unknown_election_targets(self):
        stderr = StringIO()
        with redirect_stderr(stderr):
            return_code = pipeline.main([
                "plan",
                "--election",
                "2026fed",
                "--profile",
                "regular",
            ])

        self.assertEqual(return_code, 2)
        self.assertIn("unknown election", stderr.getvalue())

    def test_all_run_requires_exact_confirmation_phrase(self):
        required = work_unit(
            "poll_trend_outputs:2025fed:@TPP",
            "poll_trend_outputs",
            "generate_poll_trends",
            "stale",
            "2025fed",
            "@TPP",
            target_match=False,
        )
        audit = audit_result([required])
        audit["target_elections"] = []
        audit["required_graph"] = {
            "required_work_unit_ids": [required["id"]]
        }

        with mock.patch.object(
            pipeline, "_load_audit", return_value=(self.registry, audit)
        ), mock.patch.object(
            pipeline, "_interactive_typed_confirmation", return_value=False
        ) as confirm, mock.patch.object(
            pipeline, "_execute_plan_with_log"
        ) as execute:
            return_code = pipeline.main(["run", "--profile", "all"])

        self.assertEqual(return_code, 0)
        confirm.assert_called_once_with(
            "Run every required historical and active generation task now?",
            pipeline.ALL_GENERATION_CONFIRMATION,
        )
        execute.assert_not_called()

    def test_all_profile_rejects_election_target(self):
        audit = audit_result([])
        stderr = StringIO()

        with mock.patch.object(
            pipeline, "_load_audit", return_value=(self.registry, audit)
        ), redirect_stderr(stderr):
            return_code = pipeline.main([
                "plan",
                "--election",
                "2028fed",
                "--profile",
                "all",
            ])

        self.assertEqual(return_code, 2)
        self.assertIn("does not accept --election", stderr.getvalue())

    def test_typed_archive_confirmation_requires_the_exact_phrase(self):
        with mock.patch.object(
            pipeline,
            "_interactive_text",
            side_effect=["restore generated data", "RESTORE GENERATED DATA"],
        ):
            self.assertFalse(
                pipeline._interactive_typed_confirmation(
                    "Restore?", "RESTORE GENERATED DATA"
                )
            )
            self.assertTrue(
                pipeline._interactive_typed_confirmation(
                    "Restore?", "RESTORE GENERATED DATA"
                )
            )

    def test_interactive_archive_build_requires_typed_confirmation(self):
        preflight = {"work_units": 8, "managed_files": ["Outputs/a.csv"]}
        with mock.patch.object(
            pipeline,
            "_interactive_select",
            side_effect=["build-archive", "exit"],
        ), mock.patch.object(
            pipeline.generated_data_archive,
            "preflight_build",
            return_value=preflight,
        ), mock.patch.object(
            pipeline,
            "_interactive_typed_confirmation",
            return_value=False,
        ), mock.patch.object(
            pipeline.generated_data_archive,
            "build_archive",
        ) as build:
            self.assertEqual(pipeline.run_interactive(), 0)

        build.assert_not_called()

    def test_interactive_archive_restore_requires_typed_confirmation(self):
        manifest = {"files": ["one", "two"]}
        with mock.patch.object(
            pipeline,
            "_interactive_select",
            side_effect=["restore-archive", "exit"],
        ), mock.patch.object(
            pipeline.generated_data_archive,
            "validate_archive",
            return_value=manifest,
        ), mock.patch.object(
            pipeline,
            "_interactive_typed_confirmation",
            return_value=False,
        ), mock.patch.object(
            pipeline.generated_data_archive,
            "restore_archive",
        ) as restore:
            self.assertEqual(pipeline.run_interactive(), 0)

        restore.assert_not_called()

    def test_interactive_archive_actions_run_only_after_typed_confirmation(self):
        preflight = {"work_units": 8, "managed_files": ["Outputs/a.csv"]}
        archive_result = {
            "archive_directory": pipeline.ANALYSIS_DIRECTORY / "Archived",
            "files": 2,
            "roots": ["Outputs"],
        }
        with mock.patch.object(
            pipeline,
            "_interactive_select",
            side_effect=["build-archive", "restore-archive", "exit"],
        ), mock.patch.object(
            pipeline.generated_data_archive,
            "preflight_build",
            return_value=preflight,
        ), mock.patch.object(
            pipeline.generated_data_archive,
            "build_archive",
            return_value=archive_result,
        ) as build, mock.patch.object(
            pipeline.generated_data_archive,
            "validate_archive",
            return_value={"files": ["one", "two"]},
        ), mock.patch.object(
            pipeline.generated_data_archive,
            "restore_archive",
            return_value=archive_result,
        ) as restore, mock.patch.object(
            pipeline,
            "_interactive_typed_confirmation",
            return_value=True,
        ):
            self.assertEqual(pipeline.run_interactive(), 0)

        build.assert_called_once_with(pipeline.ANALYSIS_DIRECTORY)
        restore.assert_called_once_with(pipeline.ANALYSIS_DIRECTORY)

    def test_run_parser_accepts_each_generation_profile(self):
        parser = pipeline.build_parser()

        for profile in pipeline.EXECUTABLE_GENERATION_PROFILES:
            with self.subTest(profile=profile):
                args = parser.parse_args(
                    [
                        "run",
                        "--election",
                        "2026vic",
                        "--profile",
                        profile,
                    ]
                )
                self.assertEqual(args.profile, profile)

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

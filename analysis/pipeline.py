"""Inspect, plan and run regeneration of the Python analysis pipeline.

Status and planning cover the complete registered graph. Execution supports
the routine trend, approval-refresh, calibration and historical-cutoff
profiles by running existing generators directly and using their provenance
records for progress and completion.
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import threading
import time
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

import analysis_provenance
import generated_provenance
import pipeline_registry
import provenance_maintenance

try:
    from InquirerPy import inquirer
except ImportError:
    inquirer = None


ELECTION_PATTERN = re.compile(r"^(\d{4})([a-z]+)$")
PLAN_STATUSES = {"missing", "legacy", "stale"}
BLOCKING_STATUSES = {"altered", "blocked"}
STATUS_ORDER = (
    "current",
    "provenance-stale",
    "stale",
    "legacy",
    "missing",
    "altered",
    "blocked",
)
TASK_STATUS_PRIORITY = {
    "provenance-stale": 0,
    "missing": 0,
    "legacy": 1,
    "stale": 2,
    "dependency-refresh": 3,
}
PROFILE_RUN_CLASSES = {
    "metadata": set(),
    "regular": {
        "pollster_analysis",
        "regular",
        "regular_with_approvals",
    },
    "regular-with-approvals": {
        "pollster_analysis",
        "regular",
        "regular_with_approvals",
    },
    "calibration": {"calibration"},
    # Cutoffs are too expensive to knowingly generate from stale inputs.
    # Their plan therefore includes the calibration and pure-trend stages
    # needed to make every direct dependency current first.
    "cutoffs": {
        "calibration",
        "pollster_analysis",
        "regular_with_approvals",
        "cutoffs",
    },
    "all": None,
}
PROFILE_ROOT_RUN_CLASSES = {
    "metadata": set(),
    "regular": {"regular", "regular_with_approvals"},
    "regular-with-approvals": {"regular", "regular_with_approvals"},
    "calibration": {"calibration"},
    # Calibration and pure trends are prerequisites, not independent roots,
    # when planning historical cutoff work for a target adjustment.
    "cutoffs": {"cutoffs"},
    "all": None,
}
# A routine update regenerates the selected election's pure trend, but does not
# expand into every historical pure trend used to fit approval relationships.
PROFILE_TARGET_ONLY_RUN_CLASSES = {
    "regular": {"regular_with_approvals"},
}
EXECUTABLE_GENERATION_PROFILES = (
    "regular",
    "regular-with-approvals",
    "calibration",
    "cutoffs",
)
REGIONAL_PARTY_ARGUMENTS = {
    "@TPP": "",
    "ONP FP": "ON",
}
DISPLAY_EXAMPLE_LIMIT = 10
ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
PIPELINE_LOG_DIRECTORY = ANALYSIS_DIRECTORY / "Logs" / "Pipeline"


class PipelineError(ValueError):
    """Raised when a status or plan cannot be constructed safely."""


def _utc_timestamp():
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


class PipelineRunLog:
    """Tee task output and lifecycle events to one durable run log."""

    def __init__(
        self,
        profile,
        target_elections,
        log_directory=PIPELINE_LOG_DIRECTORY,
    ):
        timestamp = datetime.now(timezone.utc)
        target_label = "-".join(sorted(target_elections)) or "all"
        filename = "{}_{}_{}_{}.log".format(
            timestamp.strftime("%Y%m%dT%H%M%S%fZ"),
            profile,
            target_label,
            os.getpid(),
        )
        Path(log_directory).mkdir(parents=True, exist_ok=True)
        self.path = Path(log_directory) / filename
        self._file = self.path.open(
            "x", encoding="utf-8", buffering=1
        )
        self._write_lock = threading.Lock()
        self._closed = False
        self.event(
            "RUN START",
            profile=profile,
            targets=sorted(target_elections),
        )

    def event(self, name, **details):
        timestamp = _utc_timestamp()
        fields = " ".join(
            "{}={}".format(
                key,
                json.dumps(value, ensure_ascii=True, sort_keys=True),
            )
            for key, value in sorted(details.items())
        )
        line = "[{}] {}{}".format(
            timestamp, name, " " + fields if fields else ""
        )
        with self._write_lock:
            self._file.write(line + "\n")

    def _copy_stream(self, stream, terminal):
        try:
            for line in iter(stream.readline, ""):
                with self._write_lock:
                    terminal.write(line)
                    terminal.flush()
                    self._file.write(line)
        finally:
            stream.close()

    def run_command(self, command, working_directory):
        environment = os.environ.copy()
        environment["PYTHONUNBUFFERED"] = "1"
        process = subprocess.Popen(
            command,
            cwd=str(working_directory),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            env=environment,
        )
        output_threads = [
            threading.Thread(
                target=self._copy_stream,
                args=(process.stdout, sys.stdout),
                daemon=True,
            ),
            threading.Thread(
                target=self._copy_stream,
                args=(process.stderr, sys.stderr),
                daemon=True,
            ),
        ]
        for output_thread in output_threads:
            output_thread.start()
        try:
            return_code = process.wait()
        finally:
            for output_thread in output_threads:
                output_thread.join()
        return subprocess.CompletedProcess(command, return_code)

    def close(self, succeeded=True):
        if self._closed:
            return
        self.event("RUN COMPLETE" if succeeded else "RUN FAILED")
        self._file.close()
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exception_type, exception, traceback):
        self.close(succeeded=exception_type is None)
        return False


def _json_output(value):
    return json.dumps(value, indent=2, sort_keys=True)


def _normalize_elections(elections):
    return analysis_provenance._normalize_target_elections(elections)


def _election_cli(election):
    if election == "0none":
        return "none"
    match = ELECTION_PATTERN.fullmatch(election)
    if not match:
        raise PipelineError(
            "cannot convert election '{}' to a command argument".format(
                election
            )
        )
    return "{}-{}".format(match.group(1), match.group(2))


def _stage_indexes(registry):
    stage_by_id = {stage["id"]: stage for stage in registry["stages"]}
    producer_by_category = {
        category: stage
        for stage in registry["stages"]
        for category in stage["outputs"]
    }
    stage_positions = {
        stage_id: position
        for position, stage_id in enumerate(
            pipeline_registry.topological_stage_order(
                registry, core_only=False
            )
        )
    }
    return stage_by_id, producer_by_category, stage_positions


def _work_unit_stage(work_unit, registry, stage_by_id, producer_by_category):
    category = registry["categories"].get(work_unit["category"])
    if category and category["kind"] == "diagnostic":
        return None
    stage = stage_by_id.get(work_unit["stage"])
    if stage is None:
        stage = producer_by_category.get(work_unit["category"])
    return stage


def _run_class(stage):
    execution = stage.get("execution") if stage else None
    return execution["run_class"] if execution else None


def _selected_run_classes(profiles, registry):
    if "all" in profiles:
        return {
            execution["run_class"]
            for stage in registry["stages"]
            for execution in [stage.get("execution")]
            if execution is not None
        }
    selected = set()
    for profile in profiles:
        selected.update(PROFILE_RUN_CLASSES[profile])
    return selected


def _root_run_classes(profiles, registry):
    if "all" in profiles:
        return _selected_run_classes(profiles, registry)
    selected = set()
    for profile in profiles:
        selected.update(PROFILE_ROOT_RUN_CLASSES[profile])
    return selected


def _target_only_run_classes(profiles):
    """Return run classes restricted to explicitly selected elections."""

    if "all" in profiles:
        return set()
    target_only = set()
    for profile in profiles:
        target_only.update(PROFILE_TARGET_ONLY_RUN_CLASSES.get(profile, set()))
    # A broader profile removes any target-only restriction for its classes.
    for profile in profiles:
        if profile == "regular":
            continue
        target_only.difference_update(PROFILE_RUN_CLASSES[profile])
    return target_only


def _issue_summary(issue):
    return {
        "status": issue["status"],
        "code": issue["code"],
        "message": issue["message"],
    }


def build_status(audit, registry, include_details=False):
    """Return a concise status model suitable for text or JSON rendering."""

    stage_by_id, producer_by_category, _ = _stage_indexes(registry)
    by_run_class = defaultdict(Counter)
    unclassified = Counter()
    blockers = []

    for issue in audit["source_issues"]:
        if issue["status"] == "blocked":
            blockers.append(_issue_summary(issue))
    for issue in audit["manifest_issues"]:
        if issue["status"] == "blocked":
            blockers.append(_issue_summary(issue))

    for work_unit in audit["work_units"]:
        stage = _work_unit_stage(
            work_unit, registry, stage_by_id, producer_by_category
        )
        run_class = _run_class(stage)
        if run_class is None:
            unclassified[work_unit["status"]] += 1
        else:
            by_run_class[run_class][work_unit["status"]] += 1
        if work_unit["status"] in BLOCKING_STATUSES:
            blockers.append(
                {
                    "status": work_unit["status"],
                    "code": "work_unit_{}".format(work_unit["status"]),
                    "message": "{} ({})".format(
                        work_unit["record_key"],
                        "; ".join(
                            issue["message"]
                            for issue in work_unit["issues"]
                        ),
                    ),
                    "stage": stage["id"] if stage else None,
                    "run_class": run_class,
                }
            )

    for error in audit["internal_errors"]:
        blockers.append(
            {
                "status": "blocked",
                "code": "audit_error",
                "message": error,
            }
        )

    root_categories = {
        "immediate": sorted(audit["other_root_causes"]),
        "synthetic_tpp": sorted(audit["synthetic_tpp_root_causes"]),
        "calibration": sorted(audit["calibration_root_causes"]),
    }
    status = {
        "schema_version": 1,
        "target_elections": audit["target_elections"],
        "work_unit_status_counts": dict(
            audit["summary"]["work_unit_status_counts"]
        ),
        "by_run_class": {
            run_class: {
                state: counts.get(state, 0)
                for state in STATUS_ORDER
            }
            for run_class, counts in sorted(by_run_class.items())
        },
        "unclassified_status_counts": {
            state: unclassified.get(state, 0)
            for state in STATUS_ORDER
        },
        "root_categories": root_categories,
        "blockers": blockers,
        "warnings": [
            _issue_summary(issue)
            for issue in audit["manifest_issues"]
            if issue["status"] == "unknown"
        ],
        "cpp_impacts": analysis_provenance._json_compatible(
            audit["impacts"]
        ),
    }
    if include_details:
        status["work_units"] = audit["work_units"]
        status["source_issues"] = audit["source_issues"]
        status["manifest_issues"] = audit["manifest_issues"]
    return status


def _scope_values(work_unit, field):
    scope = work_unit.get("scope")
    if not scope:
        return []
    return list(scope.get(field, []))


def _party_cli(party):
    if party not in REGIONAL_PARTY_ARGUMENTS:
        raise PipelineError(
            "no command argument is defined for regional party '{}'".format(
                party
            )
        )
    return REGIONAL_PARTY_ARGUMENTS[party]


def _task_keys(work_unit, stage):
    task_scope = stage["execution"]["task_scope"]
    if task_scope == "global":
        return [(stage["id"], None, None)]

    elections = _scope_values(work_unit, "elections")
    if not elections:
        raise PipelineError(
            "{} does not identify an election for stage '{}'".format(
                work_unit["record_key"], stage["id"]
            )
        )
    if task_scope == "election":
        return [
            (stage["id"], election, None)
            for election in elections
        ]

    parties = _scope_values(work_unit, "parties")
    if not parties:
        raise PipelineError(
            "{} does not identify a party for stage '{}'".format(
                work_unit["record_key"], stage["id"]
            )
        )
    return [
        (stage["id"], election, party)
        for election in elections
        for party in parties
    ]


def _task_status(status_counts):
    for status in ("missing", "legacy", "stale", "dependency-refresh"):
        if status_counts.get(status):
            return status
    raise AssertionError("planned task has no plannable status")


def _is_inherited_only_staleness(work_unit):
    if work_unit["status"] != "stale" or not work_unit["issues"]:
        return False
    return all(
        issue["code"] == "stale_generated_dependency"
        for issue in work_unit["issues"]
    )


def _task_command(stage, election, party):
    variables = {}
    if election is not None:
        variables["election_cli"] = _election_cli(election)
    if party is not None:
        variables["party_cli"] = _party_cli(party)
    return pipeline_registry.stage_command(stage, variables)


def _election_sort_key(election):
    if election is None:
        return (0, 0, "")
    match = ELECTION_PATTERN.fullmatch(election)
    if not match:
        return (2, 0, election)
    region = match.group(2)
    return (
        0 if region == "fed" else 1,
        int(match.group(1)),
        region,
    )


def _stage_dependency_categories(stage):
    return {
        category
        for field in ("inputs", "optional_inputs", "feedback_inputs")
        for category in stage.get(field, [])
    }


def _reachable_work_unit_ids(
    work_units,
    registry,
    stage_by_id,
    producer_by_category,
    selected_run_classes,
    root_run_classes,
    target_only_run_classes,
):
    """Select profile roots and follow only current stage dependencies."""

    work_units_by_id = {
        work_unit.get("id", work_unit["record_key"]): work_unit
        for work_unit in work_units
    }
    reachable = set()
    queue = []

    def select(work_unit):
        work_unit_id = work_unit.get("id", work_unit["record_key"])
        if work_unit_id in reachable:
            return
        reachable.add(work_unit_id)
        queue.append(work_unit)

    for work_unit in work_units:
        stage = _work_unit_stage(
            work_unit, registry, stage_by_id, producer_by_category
        )
        run_class = _run_class(stage)
        if (
            run_class in root_run_classes
            and (
                work_unit.get("target_match", True)
                # Cutoff and calibration audits are already scoped to every
                # historical work unit needed by the selected target. Those
                # prerequisites, rather than their own election codes, are
                # the executable roots for these profiles.
                or run_class in {"cutoffs", "calibration"}
            )
        ):
            select(work_unit)

    while queue:
        work_unit = queue.pop()
        stage = _work_unit_stage(
            work_unit, registry, stage_by_id, producer_by_category
        )
        if stage is None:
            continue
        accepted_categories = _stage_dependency_categories(stage)
        pending_dependencies = list(work_unit.get("dependencies", []))
        seen_dependencies = set()
        while pending_dependencies:
            dependency_id = pending_dependencies.pop()
            if dependency_id in seen_dependencies:
                continue
            seen_dependencies.add(dependency_id)
            dependency = work_units_by_id.get(dependency_id)
            if dependency is None:
                continue

            category = registry["categories"].get(
                dependency["category"]
            )
            if category and category["kind"] == "diagnostic":
                # Old final-trend records point through the retained
                # synthetic-TPP snapshot. Treat it as a transparent bridge
                # to the pure trends that the current final stage consumes.
                pending_dependencies.extend(
                    dependency.get("dependencies", [])
                )
                continue
            if dependency["category"] not in accepted_categories:
                continue

            dependency_stage = _work_unit_stage(
                dependency,
                registry,
                stage_by_id,
                producer_by_category,
            )
            dependency_run_class = _run_class(dependency_stage)
            if (
                dependency_run_class in target_only_run_classes
                and not dependency.get("target_match", True)
            ):
                continue
            if dependency_run_class in selected_run_classes:
                select(dependency)

    return reachable


def _forward_invalidated_work_unit_ids(
    work_units,
    reachable_work_units,
    initially_planned,
    registry,
    stage_by_id,
    producer_by_category,
    selected_run_classes,
):
    """Return current downstream units invalidated by planned generation.

    Feedback inputs are intentionally excluded: following them forward would
    turn known incremental-model feedback into a cyclic execution plan.
    """

    work_units_by_id = {
        work_unit.get("id", work_unit["record_key"]): work_unit
        for work_unit in work_units
    }
    dependents = defaultdict(set)
    for work_unit_id in reachable_work_units:
        work_unit = work_units_by_id[work_unit_id]
        stage = _work_unit_stage(
            work_unit, registry, stage_by_id, producer_by_category
        )
        category = registry["categories"].get(work_unit["category"])
        is_diagnostic = category and category["kind"] == "diagnostic"
        if not is_diagnostic and (
            stage is None
            or _run_class(stage) not in selected_run_classes
        ):
            continue
        accepted_categories = (
            None
            if is_diagnostic
            else {
                dependency_category
                for field in ("inputs", "optional_inputs")
                for dependency_category in stage.get(field, [])
            }
        )
        for dependency_id in work_unit.get("dependencies", []):
            dependency = work_units_by_id.get(dependency_id)
            if dependency is None:
                continue
            if (
                accepted_categories is not None
                and dependency["category"] not in accepted_categories
            ):
                continue
            dependents[dependency_id].add(work_unit_id)

    affected = set(initially_planned)
    queue = list(initially_planned)
    while queue:
        dependency_id = queue.pop()
        for dependent_id in dependents.get(dependency_id, set()):
            if dependent_id in affected:
                continue
            affected.add(dependent_id)
            queue.append(dependent_id)
    return affected - set(initially_planned)


def build_plan(audit, registry, profiles):
    """Build ordered election-level tasks from one structured audit."""

    if profiles == {"metadata"}:
        return build_metadata_plan(audit)
    if "metadata" in profiles:
        raise PipelineError(
            "the metadata profile cannot be combined with generation profiles"
        )
    require_fresh_dependencies = bool(
        {"cutoffs", "all"} & set(profiles)
    )
    stage_by_id, producer_by_category, stage_positions = _stage_indexes(
        registry
    )
    selected_run_classes = _selected_run_classes(profiles, registry)
    root_run_classes = _root_run_classes(profiles, registry)
    target_only_run_classes = _target_only_run_classes(profiles)
    reachable_work_units = _reachable_work_unit_ids(
        audit["work_units"],
        registry,
        stage_by_id,
        producer_by_category,
        selected_run_classes,
        root_run_classes,
        target_only_run_classes,
    )
    initially_planned = {
        work_unit.get("id", work_unit["record_key"])
        for work_unit in audit["work_units"]
        if (
            work_unit.get("id", work_unit["record_key"])
            in reachable_work_units
            and work_unit["status"] in PLAN_STATUSES
            and (
                require_fresh_dependencies
                or not _is_inherited_only_staleness(work_unit)
            )
        )
    }
    dependency_refreshes = (
        _forward_invalidated_work_unit_ids(
            audit["work_units"],
            reachable_work_units,
            initially_planned,
            registry,
            stage_by_id,
            producer_by_category,
            selected_run_classes,
        )
        if initially_planned
        else set()
    )
    grouped = {}
    blockers = []
    warnings = []
    accepted_stale_work_units = []

    for issue in audit["source_issues"]:
        if issue["status"] == "blocked":
            blockers.append(_issue_summary(issue))
    for issue in audit["manifest_issues"]:
        if issue["status"] == "blocked":
            blockers.append(_issue_summary(issue))
        elif issue["status"] == "unknown":
            warnings.append(_issue_summary(issue))
    for error in audit["internal_errors"]:
        blockers.append(
            {
                "status": "blocked",
                "code": "audit_error",
                "message": error,
            }
        )

    for work_unit in audit["work_units"]:
        work_unit_id = work_unit.get("id", work_unit["record_key"])
        if work_unit_id not in reachable_work_units:
            continue
        stage = _work_unit_stage(
            work_unit, registry, stage_by_id, producer_by_category
        )
        if stage is None:
            continue
        run_class = _run_class(stage)
        if run_class not in selected_run_classes:
            continue
        if work_unit["status"] in BLOCKING_STATUSES:
            blockers.append(
                {
                    "status": work_unit["status"],
                    "code": "work_unit_{}".format(work_unit["status"]),
                    "message": "{} ({})".format(
                        work_unit["record_key"],
                        "; ".join(
                            issue["message"]
                            for issue in work_unit["issues"]
                        ),
                    ),
                    "stage": stage["id"],
                    "run_class": run_class,
                }
            )
            continue
        dependency_refresh = work_unit_id in dependency_refreshes
        if (
            work_unit["status"] not in PLAN_STATUSES
            and not dependency_refresh
        ):
            continue
        if (
            not require_fresh_dependencies
            and not dependency_refresh
            and _is_inherited_only_staleness(work_unit)
        ):
            accepted_stale_work_units.append(work_unit["record_key"])
            continue
        if stage.get("execution") is None:
            blockers.append(
                {
                    "status": "blocked",
                    "code": "stage_not_executable",
                    "message": "stage '{}' has no executable command".format(
                        stage["id"]
                    ),
                    "stage": stage["id"],
                    "run_class": run_class,
                }
            )
            continue
        try:
            task_keys = _task_keys(work_unit, stage)
        except PipelineError as error:
            blockers.append(
                {
                    "status": "blocked",
                    "code": "invalid_task_scope",
                    "message": str(error),
                    "stage": stage["id"],
                    "run_class": run_class,
                }
            )
            continue
        for task_key in task_keys:
            task = grouped.setdefault(
                task_key,
                {
                    "stage": stage,
                    "election": task_key[1],
                    "party": task_key[2],
                    "run_class": run_class,
                    "status_counts": Counter(),
                    "work_units": [],
                    "record_refs": [],
                    "issue_signatures": set(),
                },
            )
            planned_status = (
                "dependency-refresh"
                if dependency_refresh
                and work_unit["status"] not in PLAN_STATUSES
                else work_unit["status"]
            )
            task["status_counts"][planned_status] += 1
            task["work_units"].append(work_unit["record_key"])
            task["record_refs"].append(
                {
                    "manifest": work_unit["manifest"],
                    "record_key": work_unit["record_key"],
                }
            )
            task["issue_signatures"].update(
                "{}|{}|{}".format(
                    issue.get("code", ""),
                    issue.get("root_category", ""),
                    issue.get("message", ""),
                )
                for issue in work_unit["issues"]
            )

    tasks = []
    if not blockers:
        for task in grouped.values():
            stage = task["stage"]
            try:
                command = _task_command(
                    stage, task["election"], task["party"]
                )
            except (PipelineError, pipeline_registry.RegistryError) as error:
                blockers.append(
                    {
                        "status": "blocked",
                        "code": "invalid_command",
                        "message": str(error),
                        "stage": stage["id"],
                        "run_class": task["run_class"],
                    }
                )
                continue
            status_counts = {
                status: task["status_counts"].get(status, 0)
                for status in (
                    "missing",
                    "legacy",
                    "stale",
                    "dependency-refresh",
                )
            }
            tasks.append(
                {
                    "stage": stage["id"],
                    "run_class": task["run_class"],
                    "election": task["election"],
                    "party": task["party"],
                    "status": _task_status(status_counts),
                    "status_counts": status_counts,
                    "work_units": sorted(task["work_units"]),
                    "record_refs": sorted(
                        task["record_refs"],
                        key=lambda ref: (
                            ref["manifest"], ref["record_key"]
                        ),
                    ),
                    "issue_signatures": sorted(
                        task["issue_signatures"]
                    ),
                    "command": command,
                    "working_directory": stage["execution"][
                        "working_directory"
                    ],
                }
            )

    tasks.sort(
        key=lambda task: (
            stage_positions[task["stage"]],
            _election_sort_key(task["election"]),
            TASK_STATUS_PRIORITY[task["status"]],
            task["party"] or "",
        )
    )
    return {
        "schema_version": 1,
        "target_elections": audit["target_elections"],
        "profiles": sorted(profiles),
        "requires_fresh_dependencies": require_fresh_dependencies,
        "root_run_classes": sorted(root_run_classes),
        "selected_run_classes": sorted(selected_run_classes),
        "target_only_run_classes": sorted(target_only_run_classes),
        "blockers": blockers,
        "warnings": warnings,
        "accepted_stale_work_units": sorted(accepted_stale_work_units),
        "tasks": tasks if not blockers else [],
    }


def build_metadata_plan(audit):
    """Build metadata-only tasks without selecting data regeneration."""

    blockers = []
    for issue in audit["source_issues"]:
        if issue["status"] == "blocked":
            blockers.append(_issue_summary(issue))
    for issue in audit["manifest_issues"]:
        if issue["status"] == "blocked":
            blockers.append(_issue_summary(issue))
    for error in audit["internal_errors"]:
        blockers.append(
            {
                "status": "blocked",
                "code": "audit_error",
                "message": error,
            }
        )

    tasks = []
    if not blockers:
        for work_unit in audit["work_units"]:
            manifest_path = (
                ANALYSIS_DIRECTORY / work_unit["manifest"]
            ).resolve()
            maintainable = provenance_maintenance.can_maintain_record(
                manifest_path, work_unit["record_key"]
            )
            if (
                work_unit["status"] != "provenance-stale"
                and not maintainable
            ):
                continue
            tasks.append(
                {
                    "stage": "maintain_provenance",
                    "source_stage": work_unit["stage"],
                    "run_class": "metadata",
                    "election": (
                        work_unit["scope"]["elections"][0]
                        if len(work_unit["scope"]["elections"]) == 1
                        else None
                    ),
                    "party": (
                        work_unit["scope"]["parties"][0]
                        if len(work_unit["scope"]["parties"]) == 1
                        else None
                    ),
                    "status": "provenance-stale",
                    "status_counts": {"provenance-stale": 1},
                    "work_units": [work_unit["record_key"]],
                    "manifest": work_unit["manifest"],
                    "command": [
                        "metadata-maintenance",
                        work_unit["manifest"],
                        work_unit["record_key"],
                    ],
                    "working_directory": ".",
                }
            )
    tasks.sort(
        key=lambda task: (
            task["manifest"],
            task["work_units"][0],
        )
    )
    return {
        "schema_version": 1,
        "target_elections": audit["target_elections"],
        "profiles": ["metadata"],
        "requires_fresh_dependencies": False,
        "root_run_classes": ["metadata"],
        "selected_run_classes": ["metadata"],
        "blockers": blockers,
        "warnings": [],
        "accepted_stale_work_units": [],
        "tasks": tasks if not blockers else [],
    }


def _format_counts(counts):
    return ", ".join(
        "{} {}".format(counts.get(status, 0), status)
        for status in STATUS_ORDER
        if counts.get(status, 0)
    ) or "none"


def print_status(status):
    scope = (
        ", ".join(status["target_elections"])
        if status["target_elections"]
        else "all recorded work"
    )
    print("Pipeline status: {}".format(scope))
    print(
        "Work units: {}".format(
            _format_counts(status["work_unit_status_counts"])
        )
    )
    if status["by_run_class"]:
        print("By run class:")
        for run_class, counts in status["by_run_class"].items():
            if any(counts.values()):
                print("  {}: {}".format(run_class, _format_counts(counts)))

    roots = status["root_categories"]
    print(
        "Issue roots: {} immediate, {} approval-path-only, "
        "{} calibration-path-only".format(
            len(roots["immediate"]),
            len(roots["synthetic_tpp"]),
            len(roots["calibration"]),
        )
    )
    if status["blockers"]:
        print("Blockers ({}):".format(len(status["blockers"])))
        for blocker in status["blockers"][:DISPLAY_EXAMPLE_LIMIT]:
            print("  {}".format(blocker["message"]))
        if len(status["blockers"]) > DISPLAY_EXAMPLE_LIMIT:
            print(
                "  ... and {} more".format(
                    len(status["blockers"]) - DISPLAY_EXAMPLE_LIMIT
                )
            )
    if status["warnings"]:
        print("Warnings ({}):".format(len(status["warnings"])))
        for warning in status["warnings"][:DISPLAY_EXAMPLE_LIMIT]:
            print("  {}".format(warning["message"]))

    cpp_impacts = status["cpp_impacts"]
    for path_class, label in (
        ("immediate", "C++ inputs requiring prompt regeneration"),
        ("synthetic_tpp_only", "C++ inputs stale only through approvals"),
        ("calibration_only", "C++ inputs stale only through calibration"),
    ):
        impacts = cpp_impacts.get(path_class, {})
        if impacts:
            print("{}:".format(label))
            for consumer, categories in sorted(impacts.items()):
                print(
                    "  {}: {}".format(consumer, ", ".join(categories))
                )


def _task_target(task):
    target = task["election"] or "global"
    if task["party"]:
        target = "{} / {}".format(target, task["party"])
    return target


def print_plan(plan, include_details=False):
    scope = (
        ", ".join(plan["target_elections"])
        if plan["target_elections"]
        else "all recorded work"
    )
    print(
        "Pipeline plan: {} [{}]".format(
            scope, ", ".join(plan["profiles"])
        )
    )
    for warning in plan["warnings"]:
        print("WARNING: {}".format(warning["message"]))
    if plan["accepted_stale_work_units"]:
        print(
            "Accepted {} regenerated work unit(s) whose only issue is "
            "pre-existing upstream staleness; they do not require "
            "regeneration.".format(
                len(plan["accepted_stale_work_units"])
            )
        )
    if plan["blockers"]:
        print("Plan blocked by {} issue(s):".format(len(plan["blockers"])))
        for blocker in plan["blockers"][:DISPLAY_EXAMPLE_LIMIT]:
            print("  {}".format(blocker["message"]))
        if len(plan["blockers"]) > DISPLAY_EXAMPLE_LIMIT:
            print(
                "  ... and {} more".format(
                    len(plan["blockers"]) - DISPLAY_EXAMPLE_LIMIT
                )
            )
        return
    if not plan["tasks"]:
        if plan["profiles"] == ["metadata"]:
            print("No metadata maintenance tasks are required.")
        else:
            print("No regeneration tasks are required for this profile.")
        return

    print("{} regeneration task(s):".format(len(plan["tasks"])))
    if not include_details:
        tasks_by_stage = defaultdict(list)
        for task in plan["tasks"]:
            tasks_by_stage[task["stage"]].append(task)
        for stage, tasks in tasks_by_stage.items():
            statuses = Counter(task["status"] for task in tasks)
            targets = [_task_target(task) for task in tasks]
            shown = targets[:DISPLAY_EXAMPLE_LIMIT]
            suffix = ""
            if len(targets) > len(shown):
                suffix = ", ... (+{} more)".format(
                    len(targets) - len(shown)
                )
            print(
                "  {}: {} task(s); {}; targets: {}{}".format(
                    stage,
                    len(tasks),
                    ", ".join(
                        "{} {}".format(count, status)
                        for status, count in sorted(
                            statuses.items(),
                            key=lambda item: TASK_STATUS_PRIORITY[item[0]],
                        )
                    ),
                    ", ".join(shown),
                    suffix,
                )
            )
        print("Use --details to show every command.")
        return

    for index, task in enumerate(plan["tasks"], start=1):
        print(
            "  {}. {}: {} [{}; {} work unit(s)]".format(
                index,
                task["stage"],
                _task_target(task),
                task["status"],
                len(task["work_units"]),
            )
        )
        print("     {}".format(shlex.join(task["command"])))


def _task_identity(task):
    return (
        task["stage"],
        task.get("manifest"),
        tuple(task["work_units"]),
    )


def _generation_task_identity(task):
    return task["stage"], task["election"], task["party"]


def _task_record_markers(task):
    """Return run IDs proving which generated records existed before a task."""

    markers = {}
    manifests = {}
    for reference in task.get("record_refs", []):
        manifest_label = reference["manifest"]
        if manifest_label not in manifests:
            manifest_path = (
                ANALYSIS_DIRECTORY / manifest_label
            ).resolve()
            manifests[manifest_label] = generated_provenance.load_manifest(
                manifest_path
            )
        record_key = reference["record_key"]
        record = manifests[manifest_label]["records"].get(record_key)
        markers[(manifest_label, record_key)] = (
            record.get("run") if record else None
        )
    return markers


def _task_records_advanced(task, previous_markers):
    if not previous_markers:
        return False
    current_markers = _task_record_markers(task)
    return all(
        current_markers.get(key) is not None
        and current_markers.get(key) != previous_run
        for key, previous_run in previous_markers.items()
    )


def _task_has_new_staleness(snapshot_task, refreshed_task):
    return (
        set(refreshed_task["work_units"])
        != set(snapshot_task["work_units"])
        or set(refreshed_task.get("issue_signatures", []))
        != set(snapshot_task.get("issue_signatures", []))
    )


def _scheduled_task_failure(task):
    """Describe why a completed task is still present in a refreshed plan."""

    reasons = sorted({
        signature.split("|", 2)[-1]
        for signature in task.get("issue_signatures", [])
        if signature
    })
    suffix = ""
    if reasons:
        suffix = ": {}".format("; ".join(reasons))
    return "{} {} remains scheduled after completion{}".format(
        task["stage"], _task_target(task), suffix
    )


def _refresh_after_task(
    task,
    refresh_plan,
    input_func=input,
    run_log=None,
):
    """Wait until post-task provenance is valid and the task has cleared."""

    while True:
        failure = None
        try:
            refreshed = refresh_plan()
            if refreshed["blockers"]:
                failure = refreshed["blockers"][0]["message"]
            else:
                matching_task = next(
                    (
                        current_task
                        for current_task in refreshed["tasks"]
                        if _task_identity(current_task) == _task_identity(task)
                    ),
                    None,
                )
                if matching_task is None:
                    return refreshed
                failure = _scheduled_task_failure(matching_task)
        except (
            analysis_provenance.AnalysisProvenanceError,
            pipeline_registry.RegistryError,
            provenance_maintenance.ProvenanceMaintenanceError,
            OSError,
            ValueError,
        ) as error:
            failure = str(error)

        if run_log is not None:
            run_log.event(
                "TASK PROVENANCE ACTION REQUIRED",
                stage=task["stage"],
                target=_task_target(task),
                problem=failure,
            )
        print(
            "\nACTION REQUIRED: post-task provenance check failed:\n"
            "{}\n"
            "Resolve the issue, then press Enter to retry. "
            "Press Ctrl-C to stop the pipeline.".format(failure),
            file=sys.stderr,
            flush=True,
        )
        input_func()


def _refresh_generation_task(
    task,
    previous_markers,
    refresh_plan,
    input_func=input,
    run_log=None,
):
    """Validate snapshot completion, deferring only newer external work."""

    while True:
        failure = None
        try:
            refreshed = refresh_plan()
            if refreshed["blockers"]:
                failure = refreshed["blockers"][0]["message"]
            else:
                matching_task = next(
                    (
                        current_task
                        for current_task in refreshed["tasks"]
                        if _generation_task_identity(current_task)
                        == _generation_task_identity(task)
                    ),
                    None,
                )
                if matching_task is None:
                    return refreshed
                if (
                    _task_has_new_staleness(task, matching_task)
                    and _task_records_advanced(task, previous_markers)
                ):
                    return refreshed
                failure = _scheduled_task_failure(matching_task)
        except (
            analysis_provenance.AnalysisProvenanceError,
            generated_provenance.GeneratedProvenanceError,
            pipeline_registry.RegistryError,
            provenance_maintenance.ProvenanceMaintenanceError,
            OSError,
            ValueError,
        ) as error:
            failure = str(error)

        if run_log is not None:
            run_log.event(
                "TASK PROVENANCE ACTION REQUIRED",
                stage=task["stage"],
                target=_task_target(task),
                problem=failure,
            )
        print(
            "\nACTION REQUIRED: post-task provenance check failed:\n"
            "{}\n"
            "Resolve the issue, then press Enter to retry. "
            "Press Ctrl-C to stop the pipeline.".format(failure),
            file=sys.stderr,
            flush=True,
        )
        input_func()


def _execute_generation_snapshot(
    snapshot_tasks,
    current_plan,
    refresh_plan,
    phase,
    input_func,
    run_log=None,
):
    """Execute one fixed task snapshot and return the refreshed plan."""

    completed = 0
    for index, task in enumerate(snapshot_tasks, start=1):
        matching_task = next(
            (
                current_task
                for current_task in current_plan["tasks"]
                if _generation_task_identity(current_task)
                == _generation_task_identity(task)
            ),
            None,
        )
        if matching_task is None:
            continue
        previous_markers = _task_record_markers(task)

        print(
            "\n"
            "============================================================\n"
            "PIPELINE {} TASK {}/{}: {} {}\n"
            "============================================================".format(
                phase,
                index,
                len(snapshot_tasks),
                task["stage"],
                _task_target(task),
            ),
            flush=True,
        )
        print("Command: {}".format(shlex.join(task["command"])), flush=True)
        working_directory = (
            ANALYSIS_DIRECTORY / task["working_directory"]
        ).resolve()
        started = time.monotonic()
        if run_log is not None:
            run_log.event(
                "TASK START",
                phase=phase,
                position=index,
                task_count=len(snapshot_tasks),
                stage=task["stage"],
                target=_task_target(task),
                command=shlex.join(task["command"]),
                working_directory=str(working_directory),
            )
        try:
            result = (
                run_log.run_command(task["command"], working_directory)
                if run_log is not None
                else subprocess.run(
                    task["command"],
                    cwd=str(working_directory),
                )
            )
        except OSError as error:
            if run_log is not None:
                run_log.event(
                    "TASK START FAILED",
                    stage=task["stage"],
                    target=_task_target(task),
                    problem=str(error),
                )
            raise PipelineError(
                "could not start {} {}: {}".format(
                    task["stage"], _task_target(task), error
                )
            ) from error
        if result.returncode:
            if run_log is not None:
                run_log.event(
                    "TASK FAILED",
                    stage=task["stage"],
                    target=_task_target(task),
                    exit_status=result.returncode,
                    duration_seconds=round(
                        time.monotonic() - started, 3
                    ),
                )
            raise PipelineError(
                "{} {} failed with exit code {}".format(
                    task["stage"],
                    _task_target(task),
                    result.returncode,
                )
            )

        current_plan = _refresh_generation_task(
            task,
            previous_markers,
            refresh_plan,
            input_func=input_func,
            run_log=run_log,
        )
        completed += 1
        if run_log is not None:
            run_log.event(
                "TASK VERIFIED",
                stage=task["stage"],
                target=_task_target(task),
                exit_status=result.returncode,
                duration_seconds=round(time.monotonic() - started, 3),
            )
        print(
            "============================================================\n"
            "PIPELINE {} TASK {}/{} RECORDED AS COMPLETE\n"
            "============================================================"
            .format(phase, index, len(snapshot_tasks)),
            flush=True,
        )
    return current_plan, completed


def _confirm_additional_follow_up(tasks, input_func):
    print(
        "\n{} task(s) became actionable during the follow-up pass."
        .format(len(tasks)),
        flush=True,
    )
    for task in tasks[:DISPLAY_EXAMPLE_LIMIT]:
        print("  {} {}".format(task["stage"], _task_target(task)))
    if len(tasks) > DISPLAY_EXAMPLE_LIMIT:
        print(
            "  ... and {} more".format(
                len(tasks) - DISPLAY_EXAMPLE_LIMIT
            )
        )
    print(
        "Run another refreshed follow-up pass? [y/N]",
        flush=True,
    )
    try:
        response = input_func().strip().casefold()
    except EOFError:
        return False
    return response in {"y", "yes"}


def _refresh_follow_up_plan(refresh_plan, input_func):
    while True:
        try:
            refreshed = refresh_plan()
            if not refreshed["blockers"]:
                return refreshed
            failure = refreshed["blockers"][0]["message"]
        except (
            analysis_provenance.AnalysisProvenanceError,
            generated_provenance.GeneratedProvenanceError,
            pipeline_registry.RegistryError,
            provenance_maintenance.ProvenanceMaintenanceError,
            OSError,
            ValueError,
        ) as error:
            failure = str(error)
        print(
            "\nACTION REQUIRED: could not prepare the follow-up pass:\n"
            "{}\n"
            "Resolve the issue, then press Enter to retry. "
            "Press Ctrl-C to stop the pipeline.".format(failure),
            file=sys.stderr,
            flush=True,
        )
        input_func()


def execute_generation_plan(
    plan,
    refresh_plan,
    input_func=input,
    run_log=None,
):
    """Execute a primary snapshot plus user-controlled follow-up passes."""

    if (
        len(plan["profiles"]) != 1
        or plan["profiles"][0] not in EXECUTABLE_GENERATION_PROFILES
    ):
        raise PipelineError(
            "expected one executable generation profile: {}".format(
                ", ".join(EXECUTABLE_GENERATION_PROFILES)
            )
        )
    if plan["blockers"]:
        raise PipelineError("cannot execute a blocked pipeline plan")

    profile = plan["profiles"][0]
    if not plan["tasks"]:
        print("No {} tasks are required.".format(profile))
        return {
            "completed": 0,
            "follow_up_passes": 0,
            "deferred_tasks": [],
        }

    current_plan, completed = _execute_generation_snapshot(
        list(plan["tasks"]),
        plan,
        refresh_plan,
        "PRIMARY",
        input_func,
        run_log,
    )
    follow_up_passes = 0
    if current_plan["tasks"]:
        follow_up_passes = 1
        follow_up_tasks = list(current_plan["tasks"])
        print(
            "\nPrimary snapshot complete. Running {} follow-up task(s)."
            .format(len(follow_up_tasks)),
            flush=True,
        )
        current_plan, pass_completed = _execute_generation_snapshot(
            follow_up_tasks,
            current_plan,
            refresh_plan,
            "FOLLOW-UP {}".format(follow_up_passes),
            input_func,
            run_log,
        )
        completed += pass_completed

    while (
        current_plan["tasks"]
        and _confirm_additional_follow_up(
            current_plan["tasks"], input_func
        )
    ):
        current_plan = _refresh_follow_up_plan(
            refresh_plan, input_func
        )
        if not current_plan["tasks"]:
            break
        follow_up_passes += 1
        follow_up_tasks = list(current_plan["tasks"])
        current_plan, pass_completed = _execute_generation_snapshot(
            follow_up_tasks,
            current_plan,
            refresh_plan,
            "FOLLOW-UP {}".format(follow_up_passes),
            input_func,
            run_log,
        )
        completed += pass_completed

    deferred_tasks = list(current_plan["tasks"])
    print(
        "\n{} run completed successfully: {} command(s) run.".format(
            profile, completed
        )
    )
    if deferred_tasks:
        print(
            "{} task(s) remain deferred to the next run:".format(
                len(deferred_tasks)
            )
        )
        for task in deferred_tasks[:DISPLAY_EXAMPLE_LIMIT]:
            print(
                "  {} {}".format(task["stage"], _task_target(task))
            )
        if len(deferred_tasks) > DISPLAY_EXAMPLE_LIMIT:
            print(
                "  ... and {} more".format(
                    len(deferred_tasks) - DISPLAY_EXAMPLE_LIMIT
                )
            )
    if run_log is not None:
        run_log.event(
            "PLAN COMPLETE",
            commands_run=completed,
            follow_up_passes=follow_up_passes,
            deferred_tasks=len(deferred_tasks),
        )
    return {
        "completed": completed,
        "follow_up_passes": follow_up_passes,
        "deferred_tasks": deferred_tasks,
    }


def execute_calibration_plan(
    plan,
    refresh_plan,
    input_func=input,
    run_log=None,
):
    """Backward-compatible calibration-specific entry point."""

    if plan["profiles"] != ["calibration"]:
        raise PipelineError("expected a calibration plan")
    return execute_generation_plan(
        plan,
        refresh_plan,
        input_func=input_func,
        run_log=run_log,
    )


def execute_metadata_plan(
    plan,
    refresh_plan,
    input_func=input,
    run_log=None,
):
    """Apply ordered metadata upgrades without running data generators."""

    if plan["profiles"] != ["metadata"]:
        raise PipelineError("expected a metadata-maintenance plan")
    if plan["blockers"]:
        raise PipelineError("cannot execute a blocked pipeline plan")
    if not plan["tasks"]:
        print("No metadata maintenance is required.")
        return

    current_plan = plan
    for index, task in enumerate(plan["tasks"], start=1):
        if _task_identity(task) not in {
            _task_identity(item) for item in current_plan["tasks"]
        }:
            continue
        print(
            "\n"
            "============================================================\n"
            "METADATA TASK {}/{}: {}\n"
            "============================================================".format(
                index, len(plan["tasks"]), task["work_units"][0]
            ),
            flush=True,
        )
        started = time.monotonic()
        if run_log is not None:
            run_log.event(
                "TASK START",
                phase="METADATA",
                position=index,
                task_count=len(plan["tasks"]),
                stage=task["stage"],
                target=_task_target(task),
                command="metadata-maintenance",
                working_directory=str(ANALYSIS_DIRECTORY),
            )
        manifest_path = (
            ANALYSIS_DIRECTORY / task["manifest"]
        ).resolve()
        count = provenance_maintenance.maintain_record(
            manifest_path, task["work_units"][0]
        )
        current_plan = _refresh_after_task(
            task,
            refresh_plan,
            input_func=input_func,
            run_log=run_log,
        )
        if run_log is not None:
            run_log.event(
                "TASK VERIFIED",
                stage=task["stage"],
                target=_task_target(task),
                upgrades_applied=count,
                duration_seconds=round(time.monotonic() - started, 3),
            )
        print(
            "Applied {} ordered provenance upgrade(s).".format(count),
            flush=True,
        )
    if run_log is not None:
        run_log.event(
            "PLAN COMPLETE",
            commands_run=0,
            metadata_tasks=len(plan["tasks"]),
        )


def _execute_plan_with_log(
    plan,
    refresh_plan,
    target_elections,
    input_func=input,
):
    profile = plan["profiles"][0]
    try:
        run_log = PipelineRunLog(profile, target_elections or [])
    except OSError as error:
        raise PipelineError(
            "could not create pipeline log: {}".format(error)
        ) from error
    print(
        "Pipeline log: {}".format(
            run_log.path.relative_to(ANALYSIS_DIRECTORY)
        ),
        flush=True,
    )
    run_log.event(
        "PLAN READY",
        tasks=len(plan["tasks"]),
        blockers=len(plan["blockers"]),
    )
    with run_log:
        if profile == "metadata":
            return execute_metadata_plan(
                plan,
                refresh_plan,
                input_func=input_func,
                run_log=run_log,
            )
        return execute_generation_plan(
            plan,
            refresh_plan,
            input_func=input_func,
            run_log=run_log,
        )
    print("\nMetadata maintenance completed successfully.")


def _interactive_select(message, choices):
    if inquirer is not None:
        return inquirer.select(
            message=message,
            choices=[
                {"name": label, "value": value}
                for label, value in choices
            ],
            cycle=False,
        ).execute()
    print(message)
    for index, (label, _) in enumerate(choices, start=1):
        print("{}. {}".format(index, label))
    while True:
        try:
            response = input("> ").strip()
        except EOFError:
            return None
        try:
            selected = int(response) - 1
        except ValueError:
            continue
        if 0 <= selected < len(choices):
            return choices[selected][1]


def _interactive_text(message):
    if inquirer is not None:
        return inquirer.text(message=message).execute().strip()
    try:
        return input("{}\n> ".format(message)).strip()
    except EOFError:
        return None


def _interactive_confirm(message):
    if inquirer is not None:
        return inquirer.confirm(message=message, default=False).execute()
    try:
        return input("{} [y/N]\n> ".format(message)).strip().lower() in {
            "y",
            "yes",
        }
    except EOFError:
        return False


def _interactive_elections(required):
    while True:
        response = _interactive_text(
            "Election code(s), separated by commas"
            + (" (required)" if required else " (blank means all)")
        )
        if response is None:
            return None
        elections = [
            value.strip()
            for value in response.split(",")
            if value.strip()
        ]
        if elections or not required:
            return elections
        print("At least one election is required for a plan.")


def _load_audit(target_elections):
    registry = pipeline_registry.load_registry()
    pipeline_registry.validate_registry(registry)
    audit = analysis_provenance.audit_repository(
        registry=registry,
        target_elections=target_elections,
    )
    return registry, audit


def _load_plan_fresh(target_elections, profiles):
    """Build a refreshed plan with the current on-disk Python modules."""

    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "plan",
        "--format",
        "json",
    ]
    for election in sorted(target_elections):
        command.extend(("--election", election))
    for profile in sorted(profiles):
        command.extend(("--profile", profile))
    result = subprocess.run(
        command,
        cwd=str(ANALYSIS_DIRECTORY),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        plan = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        problem = result.stderr.strip() or result.stdout.strip()
        raise PipelineError(
            "fresh pipeline plan failed{}: {}".format(
                " with exit code {}".format(result.returncode)
                if result.returncode
                else "",
                problem or "no machine-readable output",
            )
        ) from error
    if result.returncode not in {0, 2}:
        raise PipelineError(
            "fresh pipeline plan failed with exit code {}: {}".format(
                result.returncode,
                result.stderr.strip() or "no error output",
            )
        )
    return plan


def run_interactive():
    """Open a menu-driven status and planning interface."""

    print("Python analysis pipeline")
    while True:
        action = _interactive_select(
            "Choose an action",
            (
                ("Show status", "status"),
                ("Build regeneration plan", "plan"),
                ("Run generation plan", "run-generation"),
                ("Run metadata maintenance", "run-metadata"),
                ("Exit", "exit"),
            ),
        )
        if action in {None, "exit"}:
            return 0
        try:
            elections = (
                []
                if action == "run-metadata"
                else _interactive_elections(
                    required=action in {"plan", "run-generation"}
                )
            )
            if elections is None:
                return 0
            target_elections = _normalize_elections(elections)
            registry, audit = _load_audit(target_elections)
            if action == "status":
                print_status(build_status(audit, registry))
            elif action == "run-generation":
                profile = _interactive_select(
                    "Choose a run profile",
                    (
                        ("Regular", "regular"),
                        (
                            "Regular with approval refresh",
                            "regular-with-approvals",
                        ),
                        ("Calibration only", "calibration"),
                        (
                            "Historical cutoffs needed by target adjustments",
                            "cutoffs",
                        ),
                    ),
                )
                if profile is None:
                    return 0
                plan = build_plan(audit, registry, {profile})
                print_plan(plan)
                if (
                    not plan["blockers"]
                    and plan["tasks"]
                    and _interactive_confirm(
                        "Run these {} commands now?".format(profile)
                    )
                ):
                    _execute_plan_with_log(
                        plan,
                        lambda: _load_plan_fresh(
                            target_elections, {profile}
                        ),
                        target_elections,
                    )
            elif action == "run-metadata":
                plan = build_plan(audit, registry, {"metadata"})
                print_plan(plan)
                if (
                    not plan["blockers"]
                    and plan["tasks"]
                    and _interactive_confirm(
                        "Apply these metadata upgrades now?"
                    )
                ):
                    _execute_plan_with_log(
                        plan,
                        lambda: _load_plan_fresh(
                            target_elections, {"metadata"}
                        ),
                        target_elections,
                    )
            else:
                profile = _interactive_select(
                    "Choose a run profile",
                    (
                        ("Regular", "regular"),
                        (
                            "Regular with approval refresh",
                            "regular-with-approvals",
                        ),
                        ("Calibration only", "calibration"),
                        ("Metadata maintenance", "metadata"),
                        (
                            "Historical cutoffs needed by target adjustments",
                            "cutoffs",
                        ),
                        ("All executable stages", "all"),
                    ),
                )
                if profile is None:
                    return 0
                print_plan(build_plan(audit, registry, {profile}))
        except (
            PipelineError,
            analysis_provenance.AnalysisProvenanceError,
            pipeline_registry.RegistryError,
        ) as error:
            print("Error: {}".format(error), file=sys.stderr)
        print()


def _add_election_arguments(parser, required=False):
    parser.add_argument(
        "--election",
        action="append",
        default=[],
        required=required,
        help=(
            "Target an election such as 2028fed. Repeat for a custom group."
        ),
    )


def build_parser():
    parser = argparse.ArgumentParser(
        description="Inspect and plan Python analysis regeneration."
    )
    subparsers = parser.add_subparsers(dest="command")
    subparsers.add_parser(
        "interactive", help="Open the menu-driven status and planning tool."
    )

    status_parser = subparsers.add_parser(
        "status", help="Show concise provenance and freshness status."
    )
    _add_election_arguments(status_parser)
    status_parser.add_argument(
        "--format", choices=("text", "json"), default="text"
    )
    status_parser.add_argument(
        "--details",
        action="store_true",
        help="Include individual work units in JSON output.",
    )

    plan_parser = subparsers.add_parser(
        "plan", help="Build an ordered read-only regeneration plan."
    )
    _add_election_arguments(plan_parser)
    plan_parser.add_argument(
        "--profile",
        action="append",
        choices=tuple(PROFILE_RUN_CLASSES),
        default=[],
        help=(
            "Select work to plan. Repeat to combine profiles; defaults to "
            "regular."
        ),
    )
    plan_parser.add_argument(
        "--format", choices=("text", "json"), default="text"
    )
    plan_parser.add_argument(
        "--details",
        action="store_true",
        help="Show every planned command in text output.",
    )

    run_parser = subparsers.add_parser(
        "run", help="Execute a prevalidated regeneration plan."
    )
    _add_election_arguments(run_parser)
    run_parser.add_argument(
        "--profile",
        choices=EXECUTABLE_GENERATION_PROFILES + ("metadata",),
        default="calibration",
        help="Select a generation profile or metadata-only maintenance.",
    )
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        if args.command in {None, "interactive"}:
            return run_interactive()
        target_elections = _normalize_elections(args.election)
        registry, audit = _load_audit(target_elections)
        if args.command == "status":
            status = build_status(
                audit, registry, include_details=args.details
            )
            if args.format == "json":
                print(_json_output(status))
            else:
                print_status(status)
            return 2 if status["blockers"] else 0

        if args.command == "run":
            profiles = {args.profile}
            if args.profile != "metadata" and not target_elections:
                raise PipelineError(
                    "generation profiles require at least one --election"
                )
            plan = build_plan(audit, registry, profiles)
            print_plan(plan)
            if plan["blockers"]:
                return 2
            refresh = lambda: _load_plan_fresh(
                target_elections,
                profiles,
            )
            _execute_plan_with_log(
                plan, refresh, target_elections
            )
            return 0

        profiles = set(args.profile or ["regular"])
        if profiles != {"metadata"} and not target_elections:
            raise PipelineError(
                "generation plans require at least one --election"
            )
        plan = build_plan(audit, registry, profiles)
        if args.format == "json":
            print(_json_output(plan))
        else:
            print_plan(plan, include_details=args.details)
        return 2 if plan["blockers"] else 0
    except (
        PipelineError,
        analysis_provenance.AnalysisProvenanceError,
        pipeline_registry.RegistryError,
    ) as error:
        print("Error: {}".format(error), file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nPipeline run interrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())

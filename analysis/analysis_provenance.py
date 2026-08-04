"""Audit and register source changes across the Python analysis pipeline.

This is the repository-level interface for provenance operations. The lower
level source_provenance module remains useful for manifest maintenance, while
this module applies the project's manifest locations and impact policy.

Main functions:
* ``audit_repository`` walks registered source and generated manifests,
  classifies work-unit freshness and reports terminal C++ input impacts.
* ``register_changes`` validates and records a scoped source-data or code
  change against the appropriate source manifest.
* ``audit_json`` exposes the same audit result to pipeline.py without parsing
  human-readable terminal output.
* ``run_interactive`` provides the routine audit and change-registration menu.
"""

import argparse
import fnmatch
import json
import re
import sys
import time
from collections import Counter, defaultdict, deque
from pathlib import Path

import generated_provenance
import approvals_provenance
import calibration_provenance
import pipeline_registry
import pollster_analysis_provenance
import provenance_maintenance
import region_model_provenance
import source_provenance
import trend_adjust_provenance

try:
    from InquirerPy import inquirer
except ImportError:
    inquirer = None


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
SOURCE_MANIFEST_PATHS = (
    ANALYSIS_DIRECTORY / "provenance.json",
    ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    ANALYSIS_DIRECTORY / "Regional" / "provenance.json",
    ANALYSIS_DIRECTORY / "Models" / "provenance.json",
    ANALYSIS_DIRECTORY / "seats" / "provenance.json",
    ANALYSIS_DIRECTORY / "Federal-State" / "provenance.json",
)
GENERATED_MANIFEST_PATHS = (
    ANALYSIS_DIRECTORY / "elections" / "generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Seat Statistics"
    / "generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Outputs"
    / "Calibration"
    / "generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Outputs"
    / "Calibration"
    / "pollster-generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Outputs"
    / "pure-generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Outputs"
    / "poll-trend-generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Outputs"
    / "cutoff-generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Adjustments"
    / "generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Synthetic TPPs"
    / "generated-provenance.json",
    ANALYSIS_DIRECTORY
    / "Regional"
    / "generated-provenance.json",
)
IMPACT_LEVELS = (
    "negligible",
    "provenance-only",
    "minor",
    "material",
    "major",
)
CALIBRATION_STAGES = {
    "calibrate_pollsters",
    "calibrate_pollster_bias",
    "compact_calibration_summaries",
}
CUTOFF_STAGES = {"generate_cutoff_poll_trends"}
SYNTHETIC_TPP_STAGES = {
    "generate_pure_poll_trends",
    # Retained for pre-registry-v2 generated manifests.
    "generate_synthetic_tpp",
}
PATH_IMMEDIATE = "immediate"
PATH_SYNTHETIC_TPP = "synthetic_tpp"
PATH_CUTOFF = "cutoff"
PATH_CALIBRATION = "calibration"
DEPENDENCY_FIELDS = ("inputs", "optional_inputs", "feedback_inputs")
ELECTION_CODE_PATTERN = re.compile(r"^\d{4}[a-z]+$")
WORK_UNIT_EXAMPLE_LIMIT = 15
WORK_UNIT_STATUS_PRECEDENCE = {
    "current": 0,
    "provenance-stale": 1,
    "stale": 2,
    "legacy": 3,
    "missing": 4,
    "altered": 5,
    "blocked": 6,
}


# Source and generated-record selection helpers

class AnalysisProvenanceError(ValueError):
    """Raised when a repository-level provenance operation is invalid."""


def _category_for_path(path, manifest_paths=SOURCE_MANIFEST_PATHS):
    path = Path(path).resolve()
    matches = []
    for manifest_path in manifest_paths:
        manifest = source_provenance.load_manifest(manifest_path)
        source_folder = (
            Path(manifest_path).resolve().parent / manifest["folder"]
        ).resolve()
        try:
            relative_path = path.relative_to(source_folder).as_posix()
        except ValueError:
            continue
        for category_id, category in manifest["categories"].items():
            included = any(
                fnmatch.fnmatch(relative_path, pattern)
                for pattern in category["file_patterns"]
            )
            excluded = any(
                fnmatch.fnmatch(relative_path, pattern)
                for pattern in category["exclude_patterns"]
            )
            if included and not excluded:
                matches.append(
                    (Path(manifest_path), category_id, relative_path)
                )
    if not matches:
        raise AnalysisProvenanceError(
            "{} is not covered by a tracked source category".format(path)
        )
    if len(matches) > 1:
        raise AnalysisProvenanceError(
            "{} matches multiple source categories: {}".format(
                path,
                ", ".join(
                    "{}:{}".format(manifest, category)
                    for manifest, category, _ in matches
                ),
            )
        )
    return matches[0]


def _source_category_index(manifest_paths):
    index = {}
    for manifest_path in manifest_paths:
        manifest = source_provenance.load_manifest(manifest_path)
        for category_id, category in manifest["categories"].items():
            index[category_id] = {
                "manifest": Path(manifest_path),
                "category": category,
            }
    return index


def _generated_issue_root(issue, record):
    if (
        ("changed output " in issue or "missing output " in issue)
        and issue.endswith("_results.pkl")
    ):
        return "election_result_cache"
    dependency_match = re.search(
        r"(?:dependency revision|source change|dependency) "
        r"([A-Za-z0-9_]+)",
        issue,
    )
    if dependency_match:
        return dependency_match.group(1)
    return record["category"]


def _generated_issue_code(issue):
    if issue == "legacy provenance baseline; generation inputs unknown":
        return "legacy"
    prefixes = (
        ("missing output ", "missing_output"),
        ("changed output ", "altered_output"),
        ("changed dependency ", "changed_dependency"),
        ("stale generated dependency ", "stale_generated_dependency"),
        ("invalid dependency ", "invalid_dependency"),
        ("unrecorded source change ", "unregistered_source_change"),
        ("new semantic dependency revision ", "new_semantic_revision"),
        (
            "obsolete calibration-party dependency ",
            "obsolete_calibration_party_dependency",
        ),
        (
            "provenance-only dependency revision ",
            "provenance_only_revision",
        ),
    )
    for prefix, code in prefixes:
        if issue.startswith(prefix):
            return code
    return "unknown"


def _work_unit_status(issues):
    statuses = {"current"}
    for issue in issues:
        code = _generated_issue_code(issue)
        if code == "legacy":
            statuses.add("legacy")
        elif code == "missing_output":
            statuses.add("missing")
        elif code == "altered_output":
            statuses.add("altered")
        elif code == "provenance_only_revision":
            statuses.add("provenance-stale")
        elif code in {
            "invalid_dependency",
            "unregistered_source_change",
            "unknown",
        }:
            statuses.add("blocked")
        else:
            statuses.add("stale")
    return max(statuses, key=WORK_UNIT_STATUS_PRECEDENCE.get)


def _manifest_label(path):
    resolved = Path(path).resolve()
    try:
        return resolved.relative_to(ANALYSIS_DIRECTORY).as_posix()
    except ValueError:
        return str(resolved)


def _generated_path_class(
    record,
    category_id,
    calibration_outputs,
    cutoff_outputs,
    synthetic_tpp_outputs,
):
    if record["stage"] in CUTOFF_STAGES or category_id in cutoff_outputs:
        return PATH_CUTOFF
    if (
        record["stage"] in CALIBRATION_STAGES
        or category_id in calibration_outputs
    ):
        return PATH_CALIBRATION
    if (
        record["stage"] in SYNTHETIC_TPP_STAGES
        or category_id in synthetic_tpp_outputs
    ):
        return PATH_SYNTHETIC_TPP
    return PATH_IMMEDIATE


def _is_audited_transitive_issue(
    issue,
    record,
    manifest_path,
    manifest,
    audited_manifest_paths,
    resolve_path=None,
):
    """Avoid reporting an upstream generated issue again downstream."""

    match = re.fullmatch(
        r"stale generated dependency ([A-Za-z0-9_]+) \(.*\)",
        issue,
    )
    if not match:
        return False
    dependency = record["dependencies"].get(match.group(1))
    if not dependency or dependency["kind"] != "generated_manifest":
        return False
    resolve_path = resolve_path or (lambda path: Path(path).resolve())
    resolved_manifest = resolve_path(manifest_path)
    base_directory = resolve_path(
        resolved_manifest.parent / manifest["path_base"]
    )
    dependency_manifest = resolve_path(
        base_directory / dependency["manifest"]
    )
    return dependency_manifest in audited_manifest_paths


def _latest_relevant_event(category, recorded_revision, scope):
    target_scope = source_provenance._build_scope(
        all_scopes=scope["all"],
        elections=scope["elections"],
        parties=scope["parties"],
        stages=[] if scope["all"] else [scope["stage"]],
    )
    try:
        events = source_provenance.semantic_events_affecting(
            category, recorded_revision, target_scope
        )
    except source_provenance.ProvenanceError:
        # Generated-data validation reports a malformed or future source
        # revision as a blocking dependency issue.  Root-cause formatting
        # must preserve that result rather than turning the audit into a
        # traceback while trying to name a latest event.
        return None
    return events[-1] if events else None


def _work_unit_examples(records, limit=WORK_UNIT_EXAMPLE_LIMIT):
    """Return a compact, deterministic sample of affected record keys."""

    labels = []
    for record_key, record, _ in records:
        prefix = "{}:".format(record["category"])
        label = (
            record_key[len(prefix):]
            if record_key.startswith(prefix)
            else record_key
        )
        labels.append(label.replace(":", "/"))
    labels = sorted(set(labels))
    shown = labels[:limit]
    suffix = ""
    if len(labels) > limit:
        suffix = ", ... (+{} more)".format(len(labels) - limit)
    return "; work units: {}{}".format(", ".join(shown), suffix)


def _generated_root_description(
    category_id,
    records,
    source_categories,
):
    relevant_events = []
    affected_records = 0
    legacy_only_records = 0
    legacy_issue = (
        "legacy provenance baseline; generation inputs unknown"
    )
    for _, record, record_issues in records:
        matching_issues = [
            issue
            for issue in record_issues
            if _generated_issue_root(issue, record) == category_id
        ]
        if not matching_issues:
            continue
        affected_records += 1
        if matching_issues == [legacy_issue]:
            legacy_only_records += 1
        dependency = record["dependencies"].get(category_id)
        source_entry = source_categories.get(category_id)
        if (
            dependency is None
            or dependency["kind"] != "source_manifest"
            or source_entry is None
        ):
            continue
        event = _latest_relevant_event(
            source_entry["category"],
            dependency["semantic_revision"],
            {
                "all": record["scope"]["all"],
                "elections": record["scope"]["elections"],
                "parties": record["scope"]["parties"],
                "stage": record["stage"],
            },
        )
        if event is not None:
            relevant_events.append(event)

    if relevant_events:
        latest = max(
            relevant_events,
            key=lambda event: (
                event["semantic_revision"],
                event["recorded_at_utc"],
            ),
        )
        return "{} [{}; {}]".format(
            latest["summary"], latest["magnitude"], latest["change_type"]
        )
    if affected_records and legacy_only_records == affected_records:
        if category_id in {
            "pure_poll_outputs",
            "poll_trend_outputs",
            "cutoff_poll_outputs",
        }:
            return (
                "{} pre-provenance work unit(s) were fingerprinted after "
                "generation; their exact source versions and Stan seeds "
                "were not recorded{}".format(
                    affected_records,
                    _work_unit_examples(records),
                )
            )
        return (
            "{} pre-provenance work unit(s) were fingerprinted after "
            "generation; their exact source versions were not recorded{}"
            .format(affected_records, _work_unit_examples(records))
        )
    if affected_records:
        return "{} generated work unit(s) are stale or altered{}".format(
            affected_records,
            _work_unit_examples(records),
        )
    return "generated data are stale or altered"


def _terminal_impacts(root_categories, registry, path_seeds=None):
    """Map stale roots to C++ inputs, preserving deferred path classes."""

    edges = defaultdict(list)
    calibration_outputs = _calibration_output_categories(registry)
    cutoff_outputs = _cutoff_output_categories(registry)
    synthetic_tpp_outputs = _synthetic_tpp_output_categories(registry)
    for stage in registry["stages"]:
        stage_inputs = set()
        for field in DEPENDENCY_FIELDS:
            stage_inputs.update(stage.get(field, []))
        synthetic_inputs = set(
            stage.get("dependency_path_classes", {}).get(
                PATH_SYNTHETIC_TPP, []
            )
        )
        for input_category in stage_inputs:
            edge_path = (
                PATH_CUTOFF
                if stage["id"] in CUTOFF_STAGES
                else PATH_CALIBRATION
                if stage["id"] in CALIBRATION_STAGES
                else PATH_SYNTHETIC_TPP
                if (
                    stage["id"] in SYNTHETIC_TPP_STAGES
                    or input_category in synthetic_inputs
                )
                else PATH_IMMEDIATE
            )
            for output_category in stage["outputs"]:
                edges[input_category].append(
                    (output_category, edge_path)
                )

    direct_consumers = defaultdict(set)
    for consumer in registry["consumers"]:
        if not consumer["id"].startswith("cpp_"):
            continue
        for category_id in consumer["inputs"]:
            direct_consumers[category_id].add(consumer["id"])

    impacts = {
        PATH_IMMEDIATE: defaultdict(set),
        PATH_SYNTHETIC_TPP: defaultdict(set),
        PATH_CUTOFF: defaultdict(set),
        PATH_CALIBRATION: defaultdict(set),
    }
    seeds = path_seeds
    if seeds is None:
        seeds = {
            (
                root_category,
                PATH_CUTOFF
                if root_category in cutoff_outputs
                else PATH_CALIBRATION
                if root_category in calibration_outputs
                else PATH_SYNTHETIC_TPP
                if root_category in synthetic_tpp_outputs
                else PATH_IMMEDIATE,
            )
            for root_category in root_categories
        }
    for root_category, root_path in seeds:
        queue = deque([(root_category, root_path)])
        visited = set()
        while queue:
            category_id, path_class = queue.popleft()
            state = (category_id, path_class)
            if state in visited:
                continue
            visited.add(state)
            for consumer_id in direct_consumers.get(category_id, set()):
                impacts[path_class][consumer_id].add(category_id)
            for output_category, edge_path in edges.get(
                category_id, []
            ):
                next_path = (
                    edge_path
                    if path_class == PATH_IMMEDIATE
                    else path_class
                )
                queue.append(
                    (output_category, next_path)
                )
    for path_class in (
        PATH_SYNTHETIC_TPP,
        PATH_CUTOFF,
        PATH_CALIBRATION,
    ):
        only_key = "{}_only".format(path_class)
        impacts[only_key] = defaultdict(set)
        other_path_classes = {
            PATH_IMMEDIATE,
            PATH_SYNTHETIC_TPP,
            PATH_CUTOFF,
            PATH_CALIBRATION,
        } - {path_class}
        for consumer_id, categories in impacts[path_class].items():
            only_categories = set(categories)
            for other_path_class in other_path_classes:
                only_categories -= impacts[other_path_class].get(
                    consumer_id, set()
                )
            if only_categories:
                impacts[only_key][consumer_id].update(only_categories)
    return impacts


def _calibration_output_categories(registry):
    """Return categories whose records are produced by calibration runs."""

    return {
        category_id
        for stage in registry["stages"]
        if stage["id"] in CALIBRATION_STAGES
        for category_id in stage["outputs"]
    }


def _cutoff_output_categories(registry):
    """Return categories whose records are produced by historical cutoffs."""

    return {
        category_id
        for stage in registry["stages"]
        if stage["id"] in CUTOFF_STAGES
        for category_id in stage["outputs"]
    }


def _synthetic_tpp_output_categories(registry):
    """Return categories generated within the synthetic-TPP path."""

    return {
        category_id
        for stage in registry["stages"]
        if stage["id"] in SYNTHETIC_TPP_STAGES
        for category_id in stage["outputs"]
    }


def _normalize_target_elections(target_elections):
    if not target_elections:
        return None
    normalized = {
        str(election).strip().casefold()
        for election in target_elections
        if str(election).strip()
    }
    invalid = sorted(
        election
        for election in normalized
        if not ELECTION_CODE_PATTERN.fullmatch(election)
    )
    if invalid:
        raise AnalysisProvenanceError(
            "invalid election code(s): {}; expected forms such as "
            "2028fed".format(", ".join(invalid))
        )
    configured = {
        str(election).casefold()
        for election in approvals_provenance._configured_elections()
    }
    unknown = sorted(normalized - configured)
    if unknown:
        raise AnalysisProvenanceError(
            "unknown election code(s): {}; expected a code listed in "
            "Data/polled-elections.csv or Data/future-elections.csv"
            .format(", ".join(unknown))
        )
    return normalized


def _record_matches_elections(record, target_elections):
    scope = record["scope"]
    record_elections = set(scope["elections"])
    return (
        scope["all"]
        or not record_elections
        or bool(record_elections & target_elections)
    )


def _generated_work_unit_id(manifest_path, record_key):
    return "{}::{}".format(_manifest_label(manifest_path), record_key)


def _selected_generated_records(
    manifest_paths,
    target_elections,
    check_context=None,
    include_dependencies=False,
):
    """Select target records and all generated work units they reference."""

    if target_elections is None:
        return (None, {}, {}) if include_dependencies else None

    manifests = {}
    manifest_labels = {}
    output_owners = {}
    record_locations = {}
    resolve_path = (
        check_context.resolve_path
        if check_context is not None
        else (lambda path: Path(path).resolve())
    )
    for manifest_path in manifest_paths:
        resolved_path = resolve_path(manifest_path)
        if not Path(resolved_path).is_file():
            continue
        manifest = (
            check_context.load_manifest(resolved_path)
            if check_context is not None
            else generated_provenance.load_manifest(resolved_path)
        )
        manifests[resolved_path] = manifest
        manifest_labels[resolved_path] = _manifest_label(resolved_path)
        base_directory = resolve_path(
            resolved_path.parent / manifest["path_base"]
        )
        for record_key, record in manifest["records"].items():
            record_locations[record_key] = (
                resolved_path,
                record_key,
            )
            for output_path in record["outputs"]:
                # The manifest schema guarantees portable relative paths and
                # base_directory is already resolved. Resolving every output
                # separately causes thousands of avoidable filesystem lookups
                # on Windows-mounted workspaces.
                output_owners[base_directory / output_path] = (
                    resolved_path,
                    record_key,
                )

    selected = {path: set() for path in manifests}
    audit_only = {path: set() for path in manifests}
    dependencies = defaultdict(set)
    queue = deque()
    completed_calibration_elections = {
        election
        for manifest in manifests.values()
        for record in manifest["records"].values()
        if (
            record["category"] == "poll_calibration_summaries"
            and record["status"] == "generated"
            and any(
                output.startswith("Outputs/Calibration/Summaries/")
                for output in record["outputs"]
            )
        )
        for election in record["scope"]["elections"]
    }
    modern_bias_elections = {
        election
        for manifest in manifests.values()
        for record in manifest["records"].values()
        if (
            record["status"] == "generated"
            and (
                (
                    record["category"]
                    == "bias_calibration_compatibility_inputs"
                    and any(
                        output.startswith(
                            "Outputs/Calibration/Staging/"
                        )
                        and output.endswith("-bias.csv")
                        for output in record["outputs"]
                    )
                )
                or record["category"] == "bias_calibration_stan_seeds"
            )
        )
        for election in record["scope"]["elections"]
    }
    configured_calibration_parties = (
        calibration_provenance.configured_parties_by_election()
    )

    def is_superseded_calibration_record(record):
        if record["category"] not in {
            "poll_calibration_compatibility_inputs",
            "bias_calibration_compatibility_inputs",
            "poll_calibration_traces",
            "bias_calibration_outputs",
        }:
            return False
        elections = record["scope"]["elections"]
        parties = record["scope"]["parties"]
        if len(elections) != 1 or len(parties) != 1:
            return False
        configured = configured_calibration_parties.get(elections[0])
        return configured is not None and parties[0] not in configured

    def is_orphaned_precompact_summary(record):
        # Older leave-one-out runs recorded wide calib_*.csv bundles under
        # poll_calibration_summaries with stage calibrate_pollsters. Modern
        # calibrate no longer refreshes those records; Summaries/*.csv is the
        # active summary artifact. Keep the old metadata, but do not schedule
        # Stan calibration forever after a compact sibling exists.
        if record["category"] != "poll_calibration_summaries":
            return False
        if any(
            output.startswith("Outputs/Calibration/Summaries/")
            for output in record["outputs"]
        ):
            return False
        return bool(
            completed_calibration_elections.intersection(
                record["scope"]["elections"]
            )
        )

    def is_superseded_detailed_bias_output(record):
        # Modern --bias writes Staging/*-bias.csv and Seeds/*-bias.csv. It no
        # longer refreshes party-level fp_*_biascal.csv records. Once modern
        # bias evidence exists, keep those detailed outputs for archives but
        # do not schedule calibrate_pollster_bias forever via compact-summary
        # dependency edges.
        if record["category"] != "bias_calibration_outputs":
            return False
        return bool(
            modern_bias_elections.intersection(
                record["scope"]["elections"]
            )
        )

    def is_completed_calibration_detail(record):
        if record["category"] not in {
            "poll_calibration_compatibility_inputs",
            "bias_calibration_compatibility_inputs",
            "poll_calibration_traces",
        }:
            return False
        return bool(
            completed_calibration_elections.intersection(
                record["scope"]["elections"]
            )
        )

    def work_unit_id(manifest_path, record_key):
        return "{}::{}".format(
            manifest_labels[manifest_path], record_key
        )

    def is_nonschedulable_retained_record(record):
        """Retained metadata that must not create regeneration tasks."""

        return (
            is_superseded_calibration_record(record)
            or is_orphaned_precompact_summary(record)
            or is_superseded_detailed_bias_output(record)
            or is_completed_calibration_detail(record)
        )

    def select(manifest_path, record_key, *, require_schedulable=True):
        if (
            manifest_path not in manifests
            or record_key not in manifests[manifest_path]["records"]
            or record_key in selected[manifest_path]
        ):
            return
        record = manifests[manifest_path]["records"][record_key]
        nonschedulable = is_nonschedulable_retained_record(record)
        if require_schedulable and nonschedulable:
            return
        selected[manifest_path].add(record_key)
        if nonschedulable:
            # Checked for consumer freshness / short-circuit, never planned.
            audit_only[manifest_path].add(record_key)
        queue.append((manifest_path, record_key))

    for manifest_path, manifest in manifests.items():
        for record_key, record in manifest["records"].items():
            if not _record_matches_elections(record, target_elections):
                continue
            if is_nonschedulable_retained_record(record):
                # Keep old outputs and metadata for traceability, but do not
                # schedule commands that no longer produce those artifacts.
                continue
            select(manifest_path, record_key)

    while queue:
        manifest_path, record_key = queue.popleft()
        manifest = manifests[manifest_path]
        record = manifest["records"][record_key]
        base_directory = resolve_path(
            manifest_path.parent / manifest["path_base"]
        )
        for dependency in record["dependencies"].values():
            if dependency["kind"] == "generated_manifest":
                dependency_manifest = resolve_path(
                    base_directory / dependency["manifest"]
                )
                non_invalidating_records = set(
                    dependency.get("non_invalidating_records", [])
                )
                for dependency_record in dependency["records"]:
                    if dependency_record in non_invalidating_records:
                        continue
                    if (
                        dependency_manifest in manifests
                        and dependency_record
                        in manifests[dependency_manifest]["records"]
                    ):
                        dependencies[
                            work_unit_id(manifest_path, record_key)
                        ].add(
                            work_unit_id(
                                dependency_manifest, dependency_record
                            )
                        )
                    # Always audit consumer-referenced keys that still exist,
                    # even when superseded/orphaned filters exclude them from
                    # regeneration scheduling.
                    select(
                        dependency_manifest,
                        dependency_record,
                        require_schedulable=False,
                    )
            elif dependency["kind"] == "files":
                for dependency_file in dependency["files"]:
                    owner = output_owners.get(
                        base_directory / dependency_file
                    )
                    if owner:
                        owner_record = manifests[owner[0]]["records"][
                            owner[1]
                        ]
                        if is_nonschedulable_retained_record(owner_record):
                            continue
                        dependencies[
                            work_unit_id(manifest_path, record_key)
                        ].add(
                            work_unit_id(*owner)
                        )
                        select(*owner)
        if record["stage"] in {
            "generate_pure_poll_trends",
            "generate_poll_trends",
            "generate_cutoff_poll_trends",
        }:
            elections = record["scope"]["elections"]
            if len(elections) == 1:
                pollster_key = "pollster_parameters:{}".format(
                    elections[0]
                )
                owner = record_locations.get(pollster_key)
                if owner is not None:
                    dependencies[
                        work_unit_id(manifest_path, record_key)
                    ].add(work_unit_id(*owner))
                    select(*owner)
    if include_dependencies:
        return (
            selected,
            {
                work_unit_id: sorted(dependency_ids)
                for work_unit_id, dependency_ids in dependencies.items()
            },
            audit_only,
        )
    return selected


def _missing_regional_work_units(target_elections):
    required = region_model_provenance.required_work_units(
        target_elections
    )
    manifest_path = region_model_provenance.MANIFEST_PATH
    if manifest_path.is_file():
        manifest = generated_provenance.load_manifest(manifest_path)
        recorded = set(manifest["records"])
    else:
        recorded = set()
    return sorted(set(required) - recorded)


def _missing_cutoff_work_units(target_elections):
    required = trend_adjust_provenance.required_cutoff_work_units(
        target_elections
    )
    manifest_path = trend_adjust_provenance.CUTOFF_MANIFEST_PATH
    if manifest_path.is_file():
        manifest = generated_provenance.load_manifest(manifest_path)
        recorded = set(manifest["records"])
    else:
        recorded = set()
    return sorted(set(required) - recorded)


def _missing_cutoff_dependencies(election, available_work_unit_ids):
    """Return already-audited prerequisites for a missing cutoff record.

    A newly required consolidated cutoff has no manifest record from which to
    discover its parents.  Mirror the recorder's preflight dependencies here
    so the pipeline refreshes them before starting expensive Stan work.  Pure
    trends for active future elections are intentionally omitted: cutoff
    provenance records them as non-invalidating because routine new polling
    should not continually restart historical cutoff generation.
    """

    dependencies = [
        _generated_work_unit_id(
            pollster_analysis_provenance.MANIFEST_PATH,
            "pollster_parameters:{}".format(election),
        )
    ]
    if election in approvals_provenance.approval_elections():
        for pure_election in sorted(
            approvals_provenance.synthetic_dependency_elections([election])
            - approvals_provenance.current_elections()
        ):
            dependencies.append(
                _generated_work_unit_id(
                    approvals_provenance.PURE_MANIFEST_PATH,
                    "pure_poll_outputs:{}:@TPP".format(pure_election),
                )
            )
    return [
        dependency
        for dependency in dependencies
        if dependency in available_work_unit_ids
    ]


def _missing_calibration_summary_work_units(target_elections):
    """Return compact summaries missing from otherwise usable bias evidence.

    A summary is an optional consumer optimisation, not a prerequisite for
    pollster analysis.  Reporting it as a non-blocking work unit lets the
    calibration and full profiles compact retained legacy files without
    rerunning their still-current Stan work.
    """

    manifest_path = calibration_provenance.MANIFEST_PATH
    if not manifest_path.is_file():
        return []
    manifest = generated_provenance.load_manifest(manifest_path)
    elections_with_bias = {
        record["scope"]["elections"][0]
        for record in manifest["records"].values()
        if (
            record["category"] in {
                "bias_calibration_compatibility_inputs",
                "bias_calibration_outputs",
            }
            and len(record["scope"]["elections"]) == 1
            and (
                target_elections is None
                or record["scope"]["elections"][0] in target_elections
            )
        )
    }
    elections_with_summary = {
        record["scope"]["elections"][0]
        for record in manifest["records"].values()
        if (
            record["category"] == "poll_calibration_summaries"
            and len(record["scope"]["elections"]) == 1
            and any(
                output.startswith("Outputs/Calibration/Summaries/")
                for output in record["outputs"]
            )
        )
    }
    return sorted(elections_with_bias - elections_with_summary)


def _missing_federal_calibration_prior_work_units(target_elections):
    """Return federal prior records required by state calibration but absent."""

    required = calibration_provenance.required_federal_prior_work_units(
        target_elections
    )
    manifest_path = calibration_provenance.MANIFEST_PATH
    if manifest_path.is_file():
        manifest = generated_provenance.load_manifest(manifest_path)
        recorded = set(manifest["records"])
    else:
        recorded = set()
    return sorted(set(required) - recorded)


def _federal_prior_consumer_elections(work_units, target_elections):
    """Elections whose calibrate/bias work implies needed federal priors.

    Calibration plans often include historical state bias units pulled in as
    dependency roots even when the explicit target is a later federal. Those
    consumers must be included when synthesizing missing priors.
    """

    consumers = set(target_elections or ())
    for work_unit in work_units:
        if work_unit["stage"] not in {
            "calibrate_pollsters",
            "calibrate_pollster_bias",
        }:
            continue
        if work_unit["category"] == "federal_calibration_priors":
            continue
        consumers.update(work_unit["scope"]["elections"])
    return consumers or None


def _attach_federal_prior_dependencies(work_units):
    """Link state calibrate/bias units to overlapping federal prior units."""

    prior_ids_by_election = {
        work_unit["scope"]["elections"][0]: work_unit["id"]
        for work_unit in work_units
        if (
            work_unit["category"] == "federal_calibration_priors"
            and len(work_unit["scope"]["elections"]) == 1
        )
    }
    if not prior_ids_by_election:
        return

    import fp_model_data

    try:
        election_cycles = fp_model_data.load_election_cycles(
            ANALYSIS_DIRECTORY / "Data" / "election-cycles.csv"
        )
    except fp_model_data.ConfigError:
        return

    for work_unit in work_units:
        if work_unit["stage"] not in {
            "calibrate_pollsters",
            "calibrate_pollster_bias",
        }:
            continue
        if work_unit["category"] == "federal_calibration_priors":
            continue
        if len(work_unit["scope"]["elections"]) != 1:
            continue
        election = work_unit["scope"]["elections"][0]
        code = calibration_provenance._election_code_from_short(election)
        if code.region() == "fed":
            continue
        for federal in fp_model_data.overlapping_federal_elections(
            code, election_cycles
        ):
            prior_id = prior_ids_by_election.get(federal.short())
            if prior_id and prior_id not in work_unit["dependencies"]:
                work_unit["dependencies"].append(prior_id)


# Core freshness audit and impact propagation

def audit_repository(
    source_manifest_paths=SOURCE_MANIFEST_PATHS,
    generated_manifest_paths=GENERATED_MANIFEST_PATHS,
    registry=None,
    target_elections=None,
    progress=None,
):
    """Return root provenance issues and their terminal C++ impacts."""

    def report(message):
        if progress is not None:
            progress(message)

    started = time.monotonic()
    report("Starting provenance audit...")
    registry = registry or pipeline_registry.load_registry()
    target_elections = _normalize_target_elections(target_elections)
    root_causes = defaultdict(set)
    root_path_modes = defaultdict(set)
    impact_seeds = set()
    internal_errors = []
    source_issues = []
    work_units = []
    manifest_issues = []
    diagnostic_work_units = defaultdict(list)
    touched = []
    generated_check_context = (
        generated_provenance.ManifestCheckContext()
    )
    report("Checking authored source provenance...")
    for manifest_path in source_manifest_paths:
        try:
            source_manifest = source_provenance.load_manifest(manifest_path)
            comparisons = source_provenance.check_manifest(manifest_path)
        except source_provenance.ProvenanceError as error:
            internal_errors.append("{}: {}".format(manifest_path, error))
            continue
        for category_id, category in source_manifest["categories"].items():
            for event in category["events"]:
                if event["magnitude"] != "provenance-only":
                    continue
                try:
                    provenance_maintenance.validate_upgrade_id(
                        event["provenance_upgrade"]
                    )
                except (
                    provenance_maintenance.ProvenanceMaintenanceError
                ) as error:
                    message = "{}: {}".format(category_id, error)
                    source_issues.append(
                        {
                            "category": category_id,
                            "status": "blocked",
                            "code": "unknown_provenance_upgrade",
                            "change_kind": "metadata",
                            "path": None,
                            "message": message,
                        }
                    )
                    internal_errors.append(message)
        for category_id, comparison in comparisons.items():
            generated_check_context.source_cache[
                (
                    str(
                        generated_check_context.resolve_path(
                            manifest_path
                        )
                    ),
                    category_id,
                )
            ] = (
                source_manifest["categories"][category_id],
                comparison,
            )
            for change_kind in ("added", "removed", "modified"):
                for path in comparison[change_kind]:
                    message = "unregistered {} file {}".format(
                        change_kind, path
                    )
                    root_causes[category_id].add(
                        message
                    )
                    source_issues.append(
                        {
                            "category": category_id,
                            "status": "blocked",
                            "code": "unregistered_source_change",
                            "change_kind": change_kind,
                            "path": path,
                            "message": message,
                        }
                    )
                    root_path_modes[category_id].add(PATH_IMMEDIATE)
                    impact_seeds.add((category_id, PATH_IMMEDIATE))
            for path in comparison["touched"]:
                touched.append(
                    "{}:{} has a timestamp-only change in {}".format(
                        manifest_path, category_id, path
                    )
                )

    unknown_generated = []
    source_categories = {}
    try:
        source_categories = _source_category_index(source_manifest_paths)
    except source_provenance.ProvenanceError as error:
        internal_errors.append(str(error))

    calibration_outputs = _calibration_output_categories(registry)
    cutoff_outputs = _cutoff_output_categories(registry)
    synthetic_tpp_outputs = _synthetic_tpp_output_categories(registry)
    audits_cutoff_outputs = (
        trend_adjust_provenance.CUTOFF_MANIFEST_PATH.resolve()
        in {
            Path(path).resolve()
            for path in generated_manifest_paths
        }
    )
    missing_cutoff_work_units = []
    if audits_cutoff_outputs:
        try:
            missing_cutoff_work_units = _missing_cutoff_work_units(
                target_elections
            )
        except (
            generated_provenance.GeneratedProvenanceError,
            OSError,
        ) as error:
            internal_errors.append(
                "could not determine required cutoff work: {}".format(
                    error
                )
            )

    # Missing consolidated cutoff records have no stored lineage. Include
    # their prerequisites in this audit so the synthetic records below can
    # make them explicit pipeline dependencies.
    selected_elections = target_elections
    if target_elections is not None and missing_cutoff_work_units:
        selected_elections = set(target_elections)
        cutoff_elections = {
            work_unit.split(":", 1)[1]
            for work_unit in missing_cutoff_work_units
        }
        selected_elections.update(cutoff_elections)
        for election in cutoff_elections:
            if election in approvals_provenance.approval_elections():
                selected_elections.update(
                    approvals_provenance.synthetic_dependency_elections(
                        [election]
                    )
                    - approvals_provenance.current_elections()
                )
    # State calibrate/bias needs overlapping federal priors. Expand selection
    # so those federal records are audited even when the prior already exists.
    if target_elections is not None:
        try:
            prior_elections = (
                calibration_provenance.required_federal_prior_elections(
                    target_elections
                )
            )
        except (
            generated_provenance.GeneratedProvenanceError,
            OSError,
        ) as error:
            prior_elections = set()
            internal_errors.append(
                "could not determine required federal priors: {}".format(
                    error
                )
            )
        if prior_elections:
            if not isinstance(selected_elections, set):
                selected_elections = set(target_elections)
            selected_elections.update(prior_elections)
    selected_generated_records = None
    generated_dependencies = {}
    audit_only_generated_records = {}
    report("Selecting generated records...")
    try:
        (
            selected_generated_records,
            generated_dependencies,
            audit_only_generated_records,
        ) = _selected_generated_records(
            generated_manifest_paths,
            selected_elections,
            check_context=generated_check_context,
            include_dependencies=True,
        )
    except generated_provenance.GeneratedProvenanceError as error:
        internal_errors.append(str(error))
        selected_generated_records = {
            Path(path).resolve(): set()
            for path in generated_manifest_paths
        }
        audit_only_generated_records = {
            Path(path).resolve(): set()
            for path in generated_manifest_paths
        }
    audited_generated_paths = {
        generated_check_context.resolve_path(path)
        for path in generated_manifest_paths
        if Path(path).is_file()
    }
    # File dependencies often point to outputs recorded in another manifest.
    # Preload those manifests so selected-record fingerprint priming can see
    # owner outputs without hashing every recorded path.
    report(
        "Loading generated manifests ({})...".format(
            len(audited_generated_paths)
        )
    )
    for manifest_path in audited_generated_paths:
        try:
            generated_check_context.load_manifest(manifest_path)
        except generated_provenance.GeneratedProvenanceError:
            # The normal loop below reports invalid manifests in context.
            pass
    checkable_manifests = [
        Path(manifest_path)
        for manifest_path in generated_manifest_paths
        if Path(manifest_path).is_file()
    ]
    if checkable_manifests:
        report("Statting selected outputs...")
    for manifest_index, manifest_path in enumerate(
        generated_manifest_paths, start=1
    ):
        if not Path(manifest_path).is_file():
            message = "{} has no generated provenance manifest".format(
                manifest_path
            )
            unknown_generated.append(message)
            manifest_issues.append(
                {
                    "manifest": _manifest_label(manifest_path),
                    "status": "unknown",
                    "code": "missing_generated_manifest",
                    "message": message,
                }
            )
            continue
        try:
            manifest = generated_check_context.load_manifest(manifest_path)
            selected_keys = None
            if selected_generated_records is not None:
                selected_keys = selected_generated_records.get(
                    Path(manifest_path).resolve(), set()
                )
            if (
                Path(manifest_path).resolve()
                == region_model_provenance.MANIFEST_PATH.resolve()
            ):
                required_regional_keys = set(
                    region_model_provenance.required_work_units(
                        target_elections
                    )
                )
                selected_keys = (
                    required_regional_keys
                    if selected_keys is None
                    else selected_keys & required_regional_keys
                )
            if selected_keys is None:
                selected_count = len(manifest["records"])
            else:
                selected_count = len(selected_keys)
            report(
                "Checking generated freshness (manifest {}/{}: {}, {} "
                "records)...".format(
                    manifest_index,
                    len(generated_manifest_paths),
                    _manifest_label(manifest_path),
                    selected_count,
                )
            )
            checked_records = generated_provenance.check_manifest(
                manifest_path,
                record_keys=selected_keys,
                _context=generated_check_context,
            )
        except generated_provenance.GeneratedProvenanceError as error:
            message = "{}: {}".format(manifest_path, error)
            internal_errors.append(message)
            manifest_issues.append(
                {
                    "manifest": _manifest_label(manifest_path),
                    "status": "blocked",
                    "code": "invalid_generated_manifest",
                    "message": message,
                }
            )
            continue
        records_by_root = defaultdict(list)
        manifest_label = _manifest_label(manifest_path)
        audit_only_keys = set()
        if audit_only_generated_records:
            audit_only_keys = audit_only_generated_records.get(
                Path(manifest_path).resolve(), set()
            )
        for record_key, record_issues in checked_records.items():
            record = manifest["records"][record_key]
            # Consumer-referenced superseded/orphaned records are checked so
            # nested freshness can short-circuit, but they must not create
            # regeneration tasks the current generators no longer refresh.
            if record_key in audit_only_keys:
                continue
            record_issues = list(record_issues)
            if record["category"] == "pollster_parameters":
                record_issues.extend(
                    pollster_analysis_provenance
                    .obsolete_calibration_dependency_issues(
                        record,
                        generated_check_context.resolve_path(
                            Path(manifest_path).parent
                            / manifest["path_base"]
                        ),
                        check_context=generated_check_context,
                    )
                )
            direct_issues = [
                issue
                for issue in record_issues
                if not _is_audited_transitive_issue(
                    issue,
                    record,
                    manifest_path,
                    manifest,
                    audited_generated_paths,
                    generated_check_context.resolve_path,
                )
            ]
            data_direct_issues = [
                issue
                for issue in direct_issues
                if _generated_issue_code(issue)
                != "provenance_only_revision"
            ]
            roots = {
                _generated_issue_root(issue, record)
                for issue in data_direct_issues
            }
            path_classes = {
                _generated_path_class(
                    record,
                    _generated_issue_root(issue, record),
                    calibration_outputs,
                    cutoff_outputs,
                    synthetic_tpp_outputs,
                )
                for issue in record_issues
                if _generated_issue_code(issue)
                != "provenance_only_revision"
            }
            if not path_classes:
                path_classes.add(
                    _generated_path_class(
                        record,
                        record["category"],
                        calibration_outputs,
                        cutoff_outputs,
                        synthetic_tpp_outputs,
                    )
                )
            work_unit_status = _work_unit_status(record_issues)
            work_units.append(
                {
                    "id": "{}::{}".format(
                        manifest_label, record_key
                    ),
                    "record_key": record_key,
                    "category": record["category"],
                    "stage": record["stage"],
                    "scope": record["scope"],
                    "manifest": manifest_label,
                    "target_match": (
                        target_elections is None
                        or _record_matches_elections(
                            record, target_elections
                        )
                    ),
                    "dependencies": generated_dependencies.get(
                        "{}::{}".format(manifest_label, record_key),
                        [],
                    ),
                    "status": work_unit_status,
                    "blocking": work_unit_status
                    in {"altered", "blocked"},
                    "path_classes": sorted(path_classes),
                    "issues": [
                        {
                            "code": _generated_issue_code(issue),
                            "root_category": _generated_issue_root(
                                issue, record
                            ),
                            "message": issue,
                        }
                        for issue in record_issues
                    ],
                }
            )
            category = registry.get("categories", {}).get(
                record["category"], {}
            )
            if category.get("kind") == "diagnostic":
                if data_direct_issues:
                    diagnostic_work_units[record["category"]].append(
                        (record_key, work_unit_status)
                    )
                # Retained detailed files are useful for investigations, but
                # they are not part of a current executable data path.
                continue
            for category_id in roots:
                records_by_root[category_id].append(
                    (record_key, record, data_direct_issues)
                )
                path_class = _generated_path_class(
                    record,
                    category_id,
                    calibration_outputs,
                    cutoff_outputs,
                    synthetic_tpp_outputs,
                )
                root_path_modes[category_id].add(path_class)
                impact_seeds.add(
                    (record["category"], path_class)
                )
        for category_id, records in records_by_root.items():
            if category_id in root_causes:
                continue
            root_causes[category_id].add(
                _generated_root_description(
                    category_id, records, source_categories
                )
            )

    audits_regional_outputs = (
        region_model_provenance.MANIFEST_PATH.resolve()
        in {
            Path(path).resolve()
            for path in generated_manifest_paths
        }
    )
    if audits_regional_outputs:
        missing_regional_work_units = []
        try:
            missing_regional_work_units = _missing_regional_work_units(
                target_elections
            )
        except (
            generated_provenance.GeneratedProvenanceError,
            OSError,
        ) as error:
            internal_errors.append(
                "could not determine required regional model work: {}"
                .format(error)
            )
        if missing_regional_work_units:
            for work_unit in missing_regional_work_units:
                _, election, party = work_unit.split(":", 2)
                work_units.append(
                    {
                        "id": _generated_work_unit_id(
                            region_model_provenance.MANIFEST_PATH,
                            work_unit,
                        ),
                        "record_key": work_unit,
                        "category": "regional_swing_deviations",
                        "stage": "generate_regional_swings",
                        "scope": generated_provenance.generation_scope(
                            elections=[election],
                            parties=[party],
                        ),
                        "manifest": _manifest_label(
                            region_model_provenance.MANIFEST_PATH
                        ),
                        "target_match": True,
                        "dependencies": [],
                        "status": "missing",
                        "blocking": False,
                        "path_classes": [PATH_IMMEDIATE],
                        "issues": [
                            {
                                "code": "missing_record",
                                "root_category":
                                    "regional_swing_deviations",
                                "message":
                                    "required work unit has no generated record",
                            }
                        ],
                    }
                )
            shown = missing_regional_work_units[
                :WORK_UNIT_EXAMPLE_LIMIT
            ]
            suffix = ""
            if len(missing_regional_work_units) > len(shown):
                suffix = ", ... (+{} more)".format(
                    len(missing_regional_work_units) - len(shown)
                )
            root_causes["regional_swing_deviations"].add(
                "{} required work unit(s) have no generated record; "
                "work units: {}{}".format(
                    len(missing_regional_work_units),
                    ", ".join(
                        work_unit.replace(
                            "regional_swing_deviations:", ""
                        ).replace(":", "/")
                        for work_unit in shown
                    ),
                    suffix,
                )
            )
            root_path_modes["regional_swing_deviations"].add(
                PATH_IMMEDIATE
            )
            impact_seeds.add(
                ("regional_swing_deviations", PATH_IMMEDIATE)
            )

    if audits_cutoff_outputs:
        if missing_cutoff_work_units:
            available_work_unit_ids = {
                work_unit["id"] for work_unit in work_units
            }
            for work_unit in missing_cutoff_work_units:
                _, election = work_unit.split(":", 1)
                work_units.append(
                    {
                        "id": _generated_work_unit_id(
                            trend_adjust_provenance.CUTOFF_MANIFEST_PATH,
                            work_unit,
                        ),
                        "record_key": work_unit,
                        "category": "cutoff_poll_outputs",
                        "stage": "generate_cutoff_poll_trends",
                        "scope": generated_provenance.generation_scope(
                            elections=[election],
                        ),
                        "manifest": _manifest_label(
                            trend_adjust_provenance.CUTOFF_MANIFEST_PATH
                        ),
                        "target_match": True,
                        "dependencies": _missing_cutoff_dependencies(
                            election, available_work_unit_ids
                        ),
                        "status": "missing",
                        "blocking": False,
                        "path_classes": [PATH_CUTOFF],
                        "issues": [
                            {
                                "code": "missing_record",
                                "root_category": "cutoff_poll_outputs",
                                "message":
                                    "required work unit has no generated record",
                            }
                        ],
                    }
                )
            shown = missing_cutoff_work_units[
                :WORK_UNIT_EXAMPLE_LIMIT
            ]
            suffix = ""
            if len(missing_cutoff_work_units) > len(shown):
                suffix = ", ... (+{} more)".format(
                    len(missing_cutoff_work_units) - len(shown)
                )
            root_causes["cutoff_poll_outputs"].add(
                "{} required work unit(s) have no generated record; "
                "work units: {}{}".format(
                    len(missing_cutoff_work_units),
                    ", ".join(
                        work_unit.replace(
                            "cutoff_poll_outputs:", ""
                        )
                        for work_unit in shown
                    ),
                    suffix,
                )
            )
            root_path_modes["cutoff_poll_outputs"].add(
                PATH_CUTOFF
            )
            impact_seeds.add(
                ("cutoff_poll_outputs", PATH_CUTOFF)
            )

    missing_calibration_summaries = []
    try:
        missing_calibration_summaries = (
            _missing_calibration_summary_work_units(target_elections)
        )
    except (generated_provenance.GeneratedProvenanceError, OSError) as error:
        internal_errors.append(
            "could not determine missing calibration summaries: {}".format(
                error
            )
        )
    for election in missing_calibration_summaries:
        work_units.append(
            {
                "id": _generated_work_unit_id(
                    calibration_provenance.MANIFEST_PATH,
                    "poll_calibration_summaries:{}:compact".format(election),
                ),
                "record_key": "poll_calibration_summaries:{}:compact".format(
                    election
                ),
                "category": "poll_calibration_summaries",
                "stage": "compact_calibration_summaries",
                "scope": generated_provenance.generation_scope(
                    elections=[election]
                ),
                "manifest": _manifest_label(
                    calibration_provenance.MANIFEST_PATH
                ),
                "target_match": True,
                "dependencies": [],
                "status": "missing",
                "blocking": False,
                "path_classes": [PATH_CALIBRATION],
                "issues": [
                    {
                        "code": "missing_record",
                        "root_category": "poll_calibration_summaries",
                        "message": (
                            "current detailed calibration evidence has no "
                            "compact summary"
                        ),
                    }
                ],
            }
        )

    missing_federal_priors = []
    try:
        missing_federal_priors = (
            _missing_federal_calibration_prior_work_units(
                _federal_prior_consumer_elections(
                    work_units, target_elections
                )
            )
        )
    except (generated_provenance.GeneratedProvenanceError, OSError) as error:
        internal_errors.append(
            "could not determine missing federal calibration priors: {}"
            .format(error)
        )
    if missing_federal_priors:
        shown = missing_federal_priors[:WORK_UNIT_EXAMPLE_LIMIT]
        suffix = ""
        if len(missing_federal_priors) > len(shown):
            suffix = ", ... (+{} more)".format(
                len(missing_federal_priors) - len(shown)
            )
        root_causes["federal_calibration_priors"].add(
            "{} required federal prior(s) have no generated record; "
            "work units: {}{}".format(
                len(missing_federal_priors),
                ", ".join(
                    work_unit.replace("federal_calibration_priors:", "")
                    for work_unit in shown
                ),
                suffix,
            )
        )
        root_path_modes["federal_calibration_priors"].add(PATH_CALIBRATION)
        impact_seeds.add(("federal_calibration_priors", PATH_CALIBRATION))
    for work_unit in missing_federal_priors:
        _, election = work_unit.split(":", 1)
        work_units.append(
            {
                "id": _generated_work_unit_id(
                    calibration_provenance.MANIFEST_PATH,
                    work_unit,
                ),
                "record_key": work_unit,
                "category": "federal_calibration_priors",
                "stage": "calibrate_pollsters",
                "scope": generated_provenance.generation_scope(
                    elections=[election],
                ),
                "manifest": _manifest_label(
                    calibration_provenance.MANIFEST_PATH
                ),
                "target_match": True,
                "dependencies": [],
                "status": "missing",
                "blocking": False,
                "path_classes": [PATH_CALIBRATION],
                "issues": [
                    {
                        "code": "missing_record",
                        "root_category": "federal_calibration_priors",
                        "message": (
                            "required federal calibration prior has no "
                            "generated record"
                        ),
                    }
                ],
            }
        )
    _attach_federal_prior_dependencies(work_units)

    report("Building audit impacts...")
    impacts = _terminal_impacts(
        root_causes, registry, path_seeds=impact_seeds
    )
    synthetic_tpp_root_causes = {
        category_id: sorted(details)
        for category_id, details in sorted(root_causes.items())
        if root_path_modes[category_id] == {PATH_SYNTHETIC_TPP}
    }
    calibration_root_causes = {
        category_id: sorted(details)
        for category_id, details in sorted(root_causes.items())
        if root_path_modes[category_id] == {PATH_CALIBRATION}
    }
    cutoff_root_causes = {
        category_id: sorted(details)
        for category_id, details in sorted(root_causes.items())
        if root_path_modes[category_id] == {PATH_CUTOFF}
    }
    other_root_causes = {
        category_id: sorted(details)
        for category_id, details in sorted(root_causes.items())
        if root_path_modes[category_id]
        not in (
            {PATH_SYNTHETIC_TPP},
            {PATH_CUTOFF},
            {PATH_CALIBRATION},
        )
    }
    issues = [
        "{}: {}".format(category_id, detail)
        for category_id in sorted(root_causes)
        for detail in sorted(root_causes[category_id])
    ]
    issues.extend(internal_errors)
    work_units.sort(
        key=lambda item: (
            item["category"],
            item["record_key"],
            item["manifest"],
        )
    )
    status_counts = Counter(
        work_unit["status"] for work_unit in work_units
    )
    has_blockers = bool(
        internal_errors
        or any(issue["status"] == "blocked" for issue in source_issues)
        or any(issue["status"] == "blocked" for issue in manifest_issues)
        or any(work_unit["blocking"] for work_unit in work_units)
    )
    diagnostic_notices = {
        category: (
            "{} non-current detailed work unit(s) are retained for "
            "diagnosis only and do not affect regeneration planning."
            .format(len(records))
        )
        for category, records in sorted(diagnostic_work_units.items())
    }
    report(
        "Provenance audit complete ({:.1f}s).".format(
            time.monotonic() - started
        )
    )
    return {
        "issues": issues,
        "root_causes": {
            category_id: sorted(details)
            for category_id, details in sorted(root_causes.items())
        },
        "synthetic_tpp_root_causes": synthetic_tpp_root_causes,
        "cutoff_root_causes": cutoff_root_causes,
        "calibration_root_causes": calibration_root_causes,
        "other_root_causes": other_root_causes,
        "internal_errors": internal_errors,
        "impacts": impacts,
        "touched": touched,
        "unknown_generated": unknown_generated,
        "source_issues": source_issues,
        "work_units": work_units,
        "provenance_maintenance": [
            {
                "record_key": work_unit["record_key"],
                "manifest": work_unit["manifest"],
                "stage": work_unit["stage"],
                "scope": work_unit["scope"],
                "issues": [
                    issue
                    for issue in work_unit["issues"]
                    if issue["code"] == "provenance_only_revision"
                ],
            }
            for work_unit in work_units
            if work_unit["status"] == "provenance-stale"
        ],
        "manifest_issues": manifest_issues,
        "diagnostic_notices": diagnostic_notices,
        "summary": {
            "work_unit_status_counts": {
                status: status_counts.get(status, 0)
                for status in WORK_UNIT_STATUS_PRECEDENCE
            },
            "has_blockers": has_blockers,
        },
        "target_elections": (
            sorted(target_elections) if target_elections else []
        ),
    }


# Source-change registration and interactive interface

def register_changes(
    paths,
    summary,
    impact,
    change_type=None,
    scope=None,
    provenance_upgrade=None,
    source_manifest_paths=SOURCE_MANIFEST_PATHS,
):
    """Register assessed changes to explicitly named tracked files.

    Negligible changes require no downstream action. Provenance-only changes
    schedule explicit metadata upgrades without invalidating generated data.
    Minor and higher changes require data regeneration.
    """

    if impact not in IMPACT_LEVELS:
        raise AnalysisProvenanceError(
            "impact must be one of: {}".format(", ".join(IMPACT_LEVELS))
        )
    if not summary or not summary.strip():
        raise AnalysisProvenanceError("summary must not be empty")
    if impact == "provenance-only":
        try:
            provenance_maintenance.validate_upgrade_id(
                provenance_upgrade
            )
        except provenance_maintenance.ProvenanceMaintenanceError as error:
            raise AnalysisProvenanceError(str(error)) from error
    elif provenance_upgrade is not None:
        raise AnalysisProvenanceError(
            "only provenance-only changes accept a provenance upgrade"
        )
    if change_type is None:
        change_type = "formatting" if impact == "negligible" else "methodology"
    if change_type not in source_provenance.RECORD_CHANGE_TYPES:
        raise AnalysisProvenanceError(
            "change type must be one of: {}".format(
                ", ".join(sorted(source_provenance.RECORD_CHANGE_TYPES))
            )
        )
    scope = scope or source_provenance._build_scope(all_scopes=True)

    grouped_paths = defaultdict(set)
    for path in paths:
        absolute_path = Path(path)
        if not absolute_path.is_absolute():
            absolute_path = ANALYSIS_DIRECTORY / absolute_path
        manifest_path, category_id, relative_path = _category_for_path(
            absolute_path, source_manifest_paths
        )
        grouped_paths[(manifest_path, category_id)].add(relative_path)

    if not grouped_paths:
        raise AnalysisProvenanceError("at least one changed file is required")

    # Preflight every category so one registration cannot silently absorb
    # another edited file from the same category.
    for (manifest_path, category_id), requested_paths in grouped_paths.items():
        comparison = source_provenance.check_manifest(manifest_path)[
            category_id
        ]
        changed_paths = set(source_provenance._changed_paths(comparison))
        if not changed_paths:
            raise AnalysisProvenanceError(
                "{} has no unregistered physical changes".format(category_id)
            )
        omitted = sorted(changed_paths - requested_paths)
        unchanged = sorted(requested_paths - changed_paths)
        if omitted:
            raise AnalysisProvenanceError(
                "{} also has unregistered changes; include: {}".format(
                    category_id, ", ".join(omitted)
                )
            )
        if unchanged:
            raise AnalysisProvenanceError(
                "{} did not change: {}".format(
                    category_id, ", ".join(unchanged)
                )
            )

    events = []
    affects_outputs = impact in {"minor", "material", "major"}
    for manifest_path, category_id in sorted(grouped_paths):
        event, _ = source_provenance.record_change(
            manifest_path,
            category_id,
            summary,
            change_type,
            impact,
            affects_outputs,
            scope,
            provenance_upgrade=provenance_upgrade,
        )
        events.append((manifest_path, category_id, event))
    return events


def _add_scope_arguments(parser):
    parser.add_argument("--all-scopes", action="store_true")
    parser.add_argument("--election", action="append", default=[])
    parser.add_argument("--party", action="append", default=[])
    parser.add_argument("--stage", action="append", default=[])


def _json_compatible(value):
    if isinstance(value, dict):
        return {
            str(key): _json_compatible(item)
            for key, item in value.items()
        }
    if isinstance(value, (set, frozenset)):
        return sorted(_json_compatible(item) for item in value)
    if isinstance(value, (list, tuple)):
        return [_json_compatible(item) for item in value]
    if isinstance(value, Path):
        return str(value)
    return value


def audit_json(result):
    """Return a stable machine-readable representation of an audit result."""

    return json.dumps(
        _json_compatible(result),
        indent=2,
        sort_keys=True,
    )


def _print_audit(result):
    if result.get("target_elections"):
        print(
            "Audit scope: {}".format(
                ", ".join(result["target_elections"])
            )
        )
    if result["other_root_causes"]:
        print("Source and generated-data issues:")
        for category_id, details in result["other_root_causes"].items():
            print("  {}:".format(category_id))
            for detail in details:
                print("    {}".format(detail))
    if result["synthetic_tpp_root_causes"]:
        print("Synthetic-TPP-path-only provenance issues:")
        print(
            "  Pure-trend and synthetic-TPP regeneration may be deferred "
            "temporarily."
        )
        for category_id, details in (
            result["synthetic_tpp_root_causes"].items()
        ):
            print("  {}:".format(category_id))
            for detail in details:
                print("    {}".format(detail))
    if result["cutoff_root_causes"]:
        print("Historical-cutoff-path-only provenance issues:")
        print(
            "  Historical cutoff regeneration may be deferred temporarily."
        )
        for category_id, details in result["cutoff_root_causes"].items():
            print("  {}:".format(category_id))
            for detail in details:
                print("    {}".format(detail))
    if result["calibration_root_causes"]:
        print("Calibration-path-only provenance issues:")
        print("  Slow calibration regeneration may be tolerated temporarily.")
        for category_id, details in (
            result["calibration_root_causes"].items()
        ):
            print("  {}:".format(category_id))
            for detail in details:
                print("    {}".format(detail))
        print(
            "  Missing historical seed metadata is informational only; "
            "unknown input lineage determines legacy status."
        )
    if result["diagnostic_notices"]:
        print("Diagnostic provenance notices:")
        for category_id, detail in result["diagnostic_notices"].items():
            print("  {}: {}".format(category_id, detail))
    if result["provenance_maintenance"]:
        print(
            "Metadata maintenance required for {} generated work unit(s)."
            .format(len(result["provenance_maintenance"]))
        )
        shown = result["provenance_maintenance"][
            :WORK_UNIT_EXAMPLE_LIMIT
        ]
        print(
            "  {}".format(
                ", ".join(item["record_key"] for item in shown)
            )
        )
    for issue in result["internal_errors"]:
        print("ERROR: {}".format(issue))

    immediate = result["impacts"]["immediate"]
    synthetic_tpp = result["impacts"]["synthetic_tpp_only"]
    calibration = result["impacts"]["calibration_only"]
    if immediate:
        print("C++ direct inputs requiring prompt regeneration:")
        for consumer_id in sorted(immediate):
            print(
                "  {}: {}".format(
                    consumer_id, ", ".join(sorted(immediate[consumer_id]))
                )
            )
    if synthetic_tpp:
        print(
            "C++ direct inputs stale only through synthetic-TPP paths:"
        )
        print(
            "  Refresh these before calibration-only issues where practical."
        )
        for consumer_id in sorted(synthetic_tpp):
            print(
                "  {}: {}".format(
                    consumer_id,
                    ", ".join(sorted(synthetic_tpp[consumer_id])),
                )
            )
    if calibration:
        print("C++ direct inputs stale only through calibration paths:")
        print("  Slow calibration updates may be tolerated temporarily.")
        for consumer_id in sorted(calibration):
            print(
                "  {}: {}".format(
                    consumer_id,
                    ", ".join(sorted(calibration[consumer_id])),
                )
            )
    if (
        result["root_causes"]
        and not immediate
        and not synthetic_tpp
        and not calibration
    ):
        print("No tracked C++ direct input is downstream of these changes.")

    for message in result["touched"]:
        print("NOTICE: {}".format(message))
    for message in result["unknown_generated"]:
        print("UNKNOWN: {}".format(message))
    if not result["issues"]:
        print("Provenance audit passed: no stale or unregistered content.")


def _menu_select(message, choices, default=None):
    if inquirer is not None:
        return inquirer.select(
            message=message,
            choices=choices,
            default=default,
            cycle=False,
        ).execute()
    print(message)
    for index, choice in enumerate(choices, start=1):
        label = choice["name"]
        if choice["value"] == default:
            label += " (default)"
        print("{}. {}".format(index, label))
    while True:
        response = input("> ").strip()
        if not response and default is not None:
            return default
        try:
            selected = int(response) - 1
            if 0 <= selected < len(choices):
                return choices[selected]["value"]
        except ValueError:
            pass
        print("Please choose one of the listed numbers.")


def _menu_checkbox(message, choices):
    if inquirer is not None:
        return inquirer.checkbox(
            message=message,
            choices=choices,
            cycle=False,
        ).execute()
    print(message)
    for index, choice in enumerate(choices, start=1):
        print("{}. {}".format(index, choice["name"]))
    print(
        "Enter one or more numbers separated by commas, "
        "or press Enter to cancel."
    )
    while True:
        response = input("> ").strip()
        if not response:
            return []
        try:
            indexes = {
                int(value.strip()) - 1
                for value in response.split(",")
                if value.strip()
            }
        except ValueError:
            indexes = set()
        if indexes and all(0 <= index < len(choices) for index in indexes):
            return [choices[index]["value"] for index in sorted(indexes)]
        print("Please select at least one listed number.")


def _menu_text(message, default=""):
    if inquirer is not None:
        return inquirer.text(message=message, default=default).execute().strip()
    suffix = " [{}]".format(default) if default else ""
    response = input("{}{}: ".format(message, suffix)).strip()
    return response or default


def _menu_confirm(message, default=True):
    if inquirer is not None:
        return inquirer.confirm(message=message, default=default).execute()
    suffix = "[Y/n]" if default else "[y/N]"
    response = input("{} {} ".format(message, suffix)).strip().casefold()
    if not response:
        return default
    return response in {"y", "yes"}


def _unregistered_files(manifest_paths=SOURCE_MANIFEST_PATHS):
    files = []
    for manifest_path in manifest_paths:
        manifest = source_provenance.load_manifest(manifest_path)
        source_folder = (
            Path(manifest_path).resolve().parent / manifest["folder"]
        ).resolve()
        comparisons = source_provenance.check_manifest(manifest_path)
        for category_id, comparison in comparisons.items():
            for change_kind in ("added", "removed", "modified", "touched"):
                for relative_path in comparison[change_kind]:
                    files.append(
                        {
                            "path": source_folder / relative_path,
                            "relative_path": relative_path,
                            "category": category_id,
                            "change_kind": change_kind,
                        }
                    )
    return files


def _interactive_scope():
    scope_type = _menu_select(
        "Change scope",
        [
            {
                "name": "Specific elections, parties or stages (recommended)",
                "value": "specific",
            },
            {"name": "All downstream work", "value": "all"},
        ],
        default="specific",
    )
    if scope_type == "all":
        return source_provenance._build_scope(all_scopes=True)

    def values(prompt):
        response = _menu_text(prompt)
        return [
            value.strip()
            for value in response.split(",")
            if value.strip()
        ]

    elections = values("Elections, comma-separated (optional)")
    parties = values("Parties, comma-separated (optional)")
    stages = values("Stages, comma-separated (optional)")
    return source_provenance._build_scope(
        elections=elections,
        parties=parties,
        stages=stages,
    )


def _confirm_all_election_scope(scope):
    if not scope["all"] and scope["elections"]:
        return True
    return _menu_confirm(
        "This scope can affect every election. Continue with "
        "all-election impact?",
        default=False,
    )


def _interactive_register():
    changes = _unregistered_files()
    if not changes:
        print("No unregistered source or script changes were found.")
        return
    choices = [
        {
            "name": "{}: {} ({})".format(
                change["category"],
                change["relative_path"],
                change["change_kind"],
            ),
            "value": str(change["path"]),
        }
        for change in changes
    ]
    selected_files = _menu_checkbox(
        "Select changes to register "
        "(Space to select, Enter to continue)",
        choices,
    )
    if not selected_files:
        print("No changes selected; registration cancelled.")
        return
    impact = _menu_select(
        "Impact level",
        [
            {"name": value.title(), "value": value}
            for value in IMPACT_LEVELS
        ],
        default="negligible",
    )
    default_change_type = (
        "formatting" if impact == "negligible" else "methodology"
    )
    change_type = _menu_select(
        "Change type",
        [
            {"name": value.replace("_", " ").title(), "value": value}
            for value in sorted(source_provenance.RECORD_CHANGE_TYPES)
        ],
        default=default_change_type,
    )
    summary = _menu_text("Brief change summary")
    if not summary:
        raise AnalysisProvenanceError("summary must not be empty")
    provenance_upgrade = None
    if impact == "provenance-only":
        provenance_upgrade = _menu_select(
            "Metadata upgrade path",
            [
                {"name": value, "value": value}
                for value in provenance_maintenance.upgrade_ids()
            ],
        )
    scope = _interactive_scope()
    if not _confirm_all_election_scope(scope):
        print("Registration cancelled.")
        return
    if not _menu_confirm("Register this assessment?", default=True):
        print("Registration cancelled.")
        return
    events = register_changes(
        selected_files,
        summary,
        impact,
        change_type=change_type,
        scope=scope,
        provenance_upgrade=provenance_upgrade,
    )
    for manifest_path, category_id, event in events:
        print(
            "Registered {} in {} at semantic revision {} and provenance "
            "revision {}.".format(
                category_id,
                manifest_path,
                event["semantic_revision"],
                event["provenance_revision"],
            )
        )


def run_interactive():
    if inquirer is None:
        print(
            "InquirerPy is not installed; using plain terminal prompts."
        )
    while True:
        action = _menu_select(
            "Analysis provenance",
            [
                {"name": "Audit all provenance", "value": "audit"},
                {
                    "name": "Audit selected elections",
                    "value": "audit_selected",
                },
                {
                    "name": "Register detected changes",
                    "value": "register",
                },
                {"name": "Exit", "value": "exit"},
            ],
            default="audit",
        )
        if action == "exit":
            return 0
        try:
            if action == "register":
                _interactive_register()
            target_elections = None
            if action == "audit_selected":
                response = _menu_text(
                    "Election codes, comma-separated"
                )
                target_elections = [
                    value.strip()
                    for value in response.split(",")
                    if value.strip()
                ]
                if not target_elections:
                    print("No elections selected; audit cancelled.")
                    print()
                    continue
            _print_audit(
                audit_repository(
                    target_elections=target_elections,
                    progress=print,
                )
            )
        except (
            AnalysisProvenanceError,
            generated_provenance.GeneratedProvenanceError,
            source_provenance.ProvenanceError,
        ) as error:
            print("Error: {}".format(error), file=sys.stderr)
        print()


def build_parser():
    parser = argparse.ArgumentParser(
        description="Audit and register analysis provenance."
    )
    subparsers = parser.add_subparsers(dest="command")
    subparsers.add_parser(
        "interactive",
        help="Open the optional menu-driven audit and registration interface.",
    )
    audit_parser = subparsers.add_parser(
        "audit",
        help="Audit tracked source, code and generated manifests.",
    )
    audit_parser.add_argument(
        "--election",
        action="append",
        default=[],
        help=(
            "Limit generated work units to this election and their recorded "
            "upstream dependencies. Repeat for a custom election group."
        ),
    )
    audit_parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Choose human-readable output or structured JSON.",
    )
    register_parser = subparsers.add_parser(
        "register-change",
        help="Assess changed tracked files and update their manifests.",
    )
    register_parser.add_argument("files", nargs="+")
    register_parser.add_argument("--summary", required=True)
    register_parser.add_argument(
        "--impact", required=True, choices=IMPACT_LEVELS
    )
    register_parser.add_argument(
        "--provenance-upgrade",
        choices=provenance_maintenance.upgrade_ids(),
    )
    register_parser.add_argument(
        "--change-type",
        choices=sorted(source_provenance.RECORD_CHANGE_TYPES),
    )
    _add_scope_arguments(register_parser)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        if args.command in {None, "interactive"}:
            return run_interactive()
        if args.command == "audit":
            progress = (
                (lambda message: print(message, file=sys.stderr, flush=True))
                if args.format == "json"
                else print
            )
            result = audit_repository(
                target_elections=args.election,
                progress=progress,
            )
            if args.format == "json":
                print(audit_json(result))
            else:
                _print_audit(result)
            return 2 if result["issues"] else 0

        if args.command == "register-change":
            has_specific_scope = (
                args.election or args.party or args.stage
            )
            scope = source_provenance._build_scope(
                all_scopes=args.all_scopes or not has_specific_scope,
                elections=args.election,
                parties=args.party,
                stages=args.stage,
            )
            events = register_changes(
                args.files,
                args.summary,
                args.impact,
                change_type=args.change_type,
                scope=scope,
                provenance_upgrade=args.provenance_upgrade,
            )
            for manifest_path, category_id, event in events:
                print(
                    "Registered {} in {} at semantic revision {} and "
                    "provenance revision {}.".format(
                        category_id,
                        manifest_path,
                        event["semantic_revision"],
                        event["provenance_revision"],
                    )
                )
            result = audit_repository(
                progress=lambda message: print(
                    message, file=sys.stderr, flush=True
                ),
            )
            _print_audit(result)
            return 2 if result["issues"] else 0
    except (
        AnalysisProvenanceError,
        generated_provenance.GeneratedProvenanceError,
        source_provenance.ProvenanceError,
    ) as error:
        print("Error: {}".format(error), file=sys.stderr)
        return 2
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())

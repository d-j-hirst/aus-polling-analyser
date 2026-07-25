"""Audit and register source changes across the Python analysis pipeline.

This is the repository-level interface for provenance operations. The lower
level source_provenance module remains useful for manifest maintenance, while
this module applies the project's manifest locations and impact policy.
"""

import argparse
import fnmatch
import re
import sys
from collections import defaultdict, deque
from pathlib import Path

import generated_provenance
import pipeline_registry
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
IMPACT_LEVELS = ("negligible", "minor", "material", "major")
CALIBRATION_STAGES = {
    "calibrate_pollsters",
    "calibrate_pollster_bias",
    "generate_cutoff_poll_trends",
}
SYNTHETIC_TPP_STAGES = {
    "generate_pure_poll_trends",
    "generate_synthetic_tpp",
}
PATH_IMMEDIATE = "immediate"
PATH_SYNTHETIC_TPP = "synthetic_tpp"
PATH_CALIBRATION = "calibration"
DEPENDENCY_FIELDS = ("inputs", "optional_inputs", "feedback_inputs")
ELECTION_CODE_PATTERN = re.compile(r"^\d{4}[a-z]+$")
WORK_UNIT_EXAMPLE_LIMIT = 15


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


def _is_audited_transitive_issue(
    issue,
    record,
    manifest_path,
    manifest,
    audited_manifest_paths,
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
    base_directory = (
        Path(manifest_path).resolve().parent / manifest["path_base"]
    ).resolve()
    dependency_manifest = (
        base_directory / dependency["manifest"]
    ).resolve()
    return dependency_manifest in audited_manifest_paths


def _latest_relevant_event(category, recorded_revision, scope):
    target_scope = source_provenance._build_scope(
        all_scopes=scope["all"],
        elections=scope["elections"],
        parties=scope["parties"],
        stages=[] if scope["all"] else [scope["stage"]],
    )
    events = source_provenance.semantic_events_affecting(
        category, recorded_revision, target_scope
    )
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
    synthetic_tpp_outputs = _synthetic_tpp_output_categories(registry)
    for stage in registry["stages"]:
        stage_inputs = set()
        for field in DEPENDENCY_FIELDS:
            stage_inputs.update(stage.get(field, []))
        edge_path = (
            PATH_CALIBRATION
            if stage["id"] in CALIBRATION_STAGES
            else PATH_SYNTHETIC_TPP
            if stage["id"] in SYNTHETIC_TPP_STAGES
            else PATH_IMMEDIATE
        )
        for input_category in stage_inputs:
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
        PATH_CALIBRATION: defaultdict(set),
    }
    seeds = path_seeds
    if seeds is None:
        seeds = {
            (
                root_category,
                PATH_CALIBRATION
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
    impacts["synthetic_tpp_only"] = defaultdict(set)
    for consumer_id, categories in impacts[PATH_SYNTHETIC_TPP].items():
        synthetic_only_categories = (
            categories - impacts[PATH_IMMEDIATE].get(consumer_id, set())
        )
        if synthetic_only_categories:
            impacts["synthetic_tpp_only"][consumer_id].update(
                synthetic_only_categories
            )
    impacts["calibration_only"] = defaultdict(set)
    for consumer_id, categories in impacts[PATH_CALIBRATION].items():
        calibration_only_categories = (
            categories
            - impacts[PATH_IMMEDIATE].get(consumer_id, set())
            - impacts[PATH_SYNTHETIC_TPP].get(consumer_id, set())
        )
        if calibration_only_categories:
            impacts["calibration_only"][consumer_id].update(
                calibration_only_categories
            )
    return impacts


def _calibration_output_categories(registry):
    """Return categories whose records are produced by calibration runs."""

    return {
        category_id
        for stage in registry["stages"]
        if stage["id"] in CALIBRATION_STAGES
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
    return normalized


def _record_matches_elections(record, target_elections):
    scope = record["scope"]
    record_elections = set(scope["elections"])
    return (
        scope["all"]
        or not record_elections
        or bool(record_elections & target_elections)
    )


def _selected_generated_records(
    manifest_paths, target_elections, check_context=None
):
    """Select target records and all generated work units they reference."""

    if target_elections is None:
        return None

    manifests = {}
    output_owners = {}
    for manifest_path in manifest_paths:
        resolved_path = Path(manifest_path).resolve()
        if not resolved_path.is_file():
            continue
        manifest = (
            check_context.load_manifest(resolved_path)
            if check_context is not None
            else generated_provenance.load_manifest(resolved_path)
        )
        manifests[resolved_path] = manifest
        base_directory = (
            resolved_path.parent / manifest["path_base"]
        ).resolve()
        for record_key, record in manifest["records"].items():
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
    queue = deque()

    def select(manifest_path, record_key):
        if (
            manifest_path not in manifests
            or record_key not in manifests[manifest_path]["records"]
            or record_key in selected[manifest_path]
        ):
            return
        selected[manifest_path].add(record_key)
        queue.append((manifest_path, record_key))

    for manifest_path, manifest in manifests.items():
        for record_key, record in manifest["records"].items():
            if _record_matches_elections(record, target_elections):
                select(manifest_path, record_key)

    while queue:
        manifest_path, record_key = queue.popleft()
        manifest = manifests[manifest_path]
        record = manifest["records"][record_key]
        base_directory = (
            manifest_path.parent / manifest["path_base"]
        ).resolve()
        for dependency in record["dependencies"].values():
            if dependency["kind"] == "generated_manifest":
                dependency_manifest = (
                    base_directory / dependency["manifest"]
                ).resolve()
                for dependency_record in dependency["records"]:
                    select(dependency_manifest, dependency_record)
            elif dependency["kind"] == "files":
                for dependency_file in dependency["files"]:
                    owner = output_owners.get(
                        base_directory / dependency_file
                    )
                    if owner:
                        select(*owner)
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


def audit_repository(
    source_manifest_paths=SOURCE_MANIFEST_PATHS,
    generated_manifest_paths=GENERATED_MANIFEST_PATHS,
    registry=None,
    target_elections=None,
):
    """Return root provenance issues and their terminal C++ impacts."""

    registry = registry or pipeline_registry.load_registry()
    target_elections = _normalize_target_elections(target_elections)
    root_causes = defaultdict(set)
    root_path_modes = defaultdict(set)
    impact_seeds = set()
    internal_errors = []
    touched = []
    generated_check_context = (
        generated_provenance.ManifestCheckContext()
    )
    for manifest_path in source_manifest_paths:
        try:
            source_manifest = source_provenance.load_manifest(manifest_path)
            comparisons = source_provenance.check_manifest(manifest_path)
        except source_provenance.ProvenanceError as error:
            internal_errors.append("{}: {}".format(manifest_path, error))
            continue
        for category_id, comparison in comparisons.items():
            generated_check_context.source_cache[
                (str(Path(manifest_path).resolve()), category_id)
            ] = (
                source_manifest["categories"][category_id],
                comparison,
            )
            for change_kind in ("added", "removed", "modified"):
                for path in comparison[change_kind]:
                    root_causes[category_id].add(
                        "unregistered {} file {}".format(
                            change_kind, path
                        )
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
    synthetic_tpp_outputs = _synthetic_tpp_output_categories(registry)
    selected_generated_records = None
    try:
        selected_generated_records = _selected_generated_records(
            generated_manifest_paths,
            target_elections,
            check_context=generated_check_context,
        )
    except generated_provenance.GeneratedProvenanceError as error:
        internal_errors.append(str(error))
        selected_generated_records = {
            Path(path).resolve(): set()
            for path in generated_manifest_paths
        }
    audited_generated_paths = {
        Path(path).resolve()
        for path in generated_manifest_paths
        if Path(path).is_file()
    }
    for manifest_path in generated_manifest_paths:
        if not Path(manifest_path).is_file():
            unknown_generated.append(
                "{} has no generated provenance manifest".format(
                    manifest_path
                )
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
            checked_records = generated_provenance.check_manifest(
                manifest_path,
                record_keys=selected_keys,
                _context=generated_check_context,
            )
        except generated_provenance.GeneratedProvenanceError as error:
            internal_errors.append("{}: {}".format(manifest_path, error))
            continue
        records_by_root = defaultdict(list)
        for record_key, record_issues in checked_records.items():
            record = manifest["records"][record_key]
            direct_issues = [
                issue
                for issue in record_issues
                if not _is_audited_transitive_issue(
                    issue,
                    record,
                    manifest_path,
                    manifest,
                    audited_generated_paths,
                )
            ]
            roots = {
                _generated_issue_root(issue, record)
                for issue in direct_issues
            }
            for category_id in roots:
                records_by_root[category_id].append(
                    (record_key, record, direct_issues)
                )
                path_class = (
                    PATH_CALIBRATION
                    if (
                        record["stage"] in CALIBRATION_STAGES
                        or category_id in calibration_outputs
                    )
                    else PATH_SYNTHETIC_TPP
                    if (
                        record["stage"] in SYNTHETIC_TPP_STAGES
                        or category_id in synthetic_tpp_outputs
                    )
                    else PATH_IMMEDIATE
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

    audits_cutoff_outputs = (
        trend_adjust_provenance.CUTOFF_MANIFEST_PATH.resolve()
        in {
            Path(path).resolve()
            for path in generated_manifest_paths
        }
    )
    if audits_cutoff_outputs:
        missing_cutoff_work_units = []
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
        if missing_cutoff_work_units:
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
                PATH_CALIBRATION
            )
            impact_seeds.add(
                ("cutoff_poll_outputs", PATH_CALIBRATION)
            )

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
    other_root_causes = {
        category_id: sorted(details)
        for category_id, details in sorted(root_causes.items())
        if root_path_modes[category_id]
        not in ({PATH_SYNTHETIC_TPP}, {PATH_CALIBRATION})
    }
    issues = [
        "{}: {}".format(category_id, detail)
        for category_id in sorted(root_causes)
        for detail in sorted(root_causes[category_id])
    ]
    issues.extend(internal_errors)
    return {
        "issues": issues,
        "root_causes": {
            category_id: sorted(details)
            for category_id, details in sorted(root_causes.items())
        },
        "synthetic_tpp_root_causes": synthetic_tpp_root_causes,
        "calibration_root_causes": calibration_root_causes,
        "other_root_causes": other_root_causes,
        "internal_errors": internal_errors,
        "impacts": impacts,
        "touched": touched,
        "unknown_generated": unknown_generated,
        "target_elections": (
            sorted(target_elections) if target_elections else []
        ),
    }


def register_changes(
    paths,
    summary,
    impact,
    change_type=None,
    scope=None,
    source_manifest_paths=SOURCE_MANIFEST_PATHS,
):
    """Register assessed changes to explicitly named tracked files.

    A negligible change does not increment the semantic revision and therefore
    permits downstream generated data to remain in use. Every higher impact
    invalidates matching generated work units until they are regenerated.
    """

    if impact not in IMPACT_LEVELS:
        raise AnalysisProvenanceError(
            "impact must be one of: {}".format(", ".join(IMPACT_LEVELS))
        )
    if not summary or not summary.strip():
        raise AnalysisProvenanceError("summary must not be empty")
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
    affects_outputs = impact != "negligible"
    for manifest_path, category_id in sorted(grouped_paths):
        event, _ = source_provenance.record_change(
            manifest_path,
            category_id,
            summary,
            change_type,
            impact,
            affects_outputs,
            scope,
        )
        events.append((manifest_path, category_id, event))
    return events


def _add_scope_arguments(parser):
    parser.add_argument("--all-scopes", action="store_true")
    parser.add_argument("--election", action="append", default=[])
    parser.add_argument("--party", action="append", default=[])
    parser.add_argument("--stage", action="append", default=[])


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
            {"name": "All downstream work", "value": "all"},
            {
                "name": "Specific elections, parties or stages",
                "value": "specific",
            },
        ],
        default="all",
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
    scope = _interactive_scope()
    if not _menu_confirm("Register this assessment?", default=True):
        print("Registration cancelled.")
        return
    events = register_changes(
        selected_files,
        summary,
        impact,
        change_type=change_type,
        scope=scope,
    )
    for manifest_path, category_id, event in events:
        print(
            "Registered {} in {} at semantic revision {}.".format(
                category_id,
                manifest_path,
                event["semantic_revision"],
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
                audit_repository(target_elections=target_elections)
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
            result = audit_repository(target_elections=args.election)
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
            )
            for manifest_path, category_id, event in events:
                print(
                    "Registered {} in {} at semantic revision {}.".format(
                        category_id,
                        manifest_path,
                        event["semantic_revision"],
                    )
                )
            result = audit_repository()
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

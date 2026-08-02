"""Apply explicit metadata-only upgrades to generated provenance records.

This module never regenerates analysis data. It applies versioned repair
functions only when a source event explicitly declares a compatible
``provenance-only`` upgrade path; material changes remain regeneration work.

Main functions:
* ``pending_upgrades`` selects ordered applicable upgrades for one record.
* ``can_maintain_record`` checks whether all current issues are repairable
  without regenerating the work unit.
* ``maintain_record`` applies the upgrades sequentially and republishes the
  manifest through generated_provenance.
"""

import copy
from pathlib import Path

import generated_provenance
import source_provenance


# Registered metadata-upgrade implementations

class ProvenanceMaintenanceError(ValueError):
    """Raised when metadata cannot be upgraded without regeneration."""


def _no_generated_metadata_change(record, context):
    """Record an orchestration change that has no generated-record payload."""


def _refresh_source_dependency(record, context):
    """Refresh a source dependency after metadata-only bookkeeping changes."""


def _refresh_pollster_calibration_dependencies(record, context):
    """Prune calibration parties the pollster reducers cannot consume."""

    import pollster_analysis_provenance

    pollster_analysis_provenance.refresh_calibration_dependencies(
        record, context["base_directory"]
    )


def _refresh_direct_generated_dependencies(record, context):
    """Re-fingerprint direct generated inputs after a metadata-only repair."""

    base_directory = context["base_directory"]
    for category_id, dependency in list(record["dependencies"].items()):
        if dependency["kind"] != "generated_manifest":
            continue
        manifest_path = (
            Path(base_directory) / dependency["manifest"]
        ).resolve()
        record["dependencies"][category_id] = (
            generated_provenance.generated_manifest_dependency(
                category_id,
                manifest_path,
                dependency["records"],
                base_directory,
                allow_stale=False,
                non_invalidating_records=dependency.get(
                    "non_invalidating_records", ()
                ),
            )
        )


UPGRADES = {
    "no-generated-metadata-change-v1": _no_generated_metadata_change,
    "refresh-source-dependency-v1": _refresh_source_dependency,
    "refresh-pollster-calibration-dependencies-v1":
        _refresh_pollster_calibration_dependencies,
    "refresh-direct-generated-dependencies-v1":
        _refresh_direct_generated_dependencies,
}


# Upgrade selection, validation and publication

def upgrade_ids():
    return tuple(sorted(UPGRADES))


def validate_upgrade_id(upgrade_id):
    if upgrade_id not in UPGRADES:
        raise ProvenanceMaintenanceError(
            "unknown provenance upgrade '{}'; available upgrades: {}"
            .format(upgrade_id, ", ".join(upgrade_ids()))
        )


def _target_scope(record):
    return source_provenance._build_scope(
        all_scopes=record["scope"]["all"],
        elections=record["scope"]["elections"],
        parties=record["scope"]["parties"],
        stages=[] if record["scope"]["all"] else [record["stage"]],
    )


def pending_upgrades(record, base_directory, source_manifest_cache=None):
    """Return applicable provenance-only source events in execution order."""

    pending = []
    source_manifest_cache = (
        source_manifest_cache
        if source_manifest_cache is not None
        else {}
    )
    target_scope = _target_scope(record)
    for category_id, dependency in record["dependencies"].items():
        if dependency["kind"] != "source_manifest":
            continue
        manifest_path = (
            Path(base_directory) / dependency["manifest"]
        ).resolve()
        if manifest_path not in source_manifest_cache:
            source_manifest_cache[manifest_path] = (
                source_provenance.load_manifest(manifest_path)
            )
        source_manifest = source_manifest_cache[manifest_path]
        try:
            category = source_manifest["categories"][category_id]
        except KeyError as error:
            raise ProvenanceMaintenanceError(
                "{} has no source category '{}'".format(
                    manifest_path, category_id
                )
            ) from error
        semantic_events = source_provenance.semantic_events_affecting(
            category,
            dependency["semantic_revision"],
            target_scope,
        )
        if semantic_events:
            raise ProvenanceMaintenanceError(
                "{} requires data regeneration before metadata maintenance"
                .format(category_id)
            )
        events = source_provenance.provenance_events_affecting(
            category,
            dependency.get(
                "provenance_revision",
                dependency["semantic_revision"],
            ),
            target_scope,
        )
        for event_index, event in enumerate(category["events"]):
            if event not in events:
                continue
            validate_upgrade_id(event["provenance_upgrade"])
            pending.append(
                {
                    "category": category_id,
                    "event": event,
                    "event_index": event_index,
                    "manifest": manifest_path,
                }
            )
    return sorted(
        pending,
        key=lambda item: (
            item["event"]["recorded_at_utc"],
            str(item["manifest"]),
            item["category"],
            item["event_index"],
        ),
    )


def _repairable_issue(issue, upgrades):
    """Return whether an explicit pending upgrade can repair ``issue``."""

    if issue.startswith("provenance-only dependency revision "):
        return True
    upgrade_ids = {
        item["event"]["provenance_upgrade"] for item in upgrades
    }
    if "refresh-pollster-calibration-dependencies-v1" in upgrade_ids and (
        issue.startswith(
            "stale generated dependency poll_calibration_summaries "
        )
        or issue.startswith(
            "stale generated dependency bias_calibration_outputs "
        )
    ):
        return True
    return (
        "refresh-direct-generated-dependencies-v1" in upgrade_ids
        and (
            issue.startswith("stale generated dependency ")
            or issue.startswith("changed dependency ")
        )
    )


def can_maintain_record(
    manifest_path,
    record_key,
    source_manifest_cache=None,
    check_context=None,
):
    """Whether an explicit metadata upgrade can safely repair one record."""

    manifest_path = Path(manifest_path).resolve()
    try:
        manifest = generated_provenance.load_manifest(manifest_path)
    except generated_provenance.GeneratedProvenanceError:
        return False
    record = manifest["records"].get(record_key)
    if record is None:
        return False
    base_directory = (manifest_path.parent / manifest["path_base"]).resolve()
    try:
        upgrades = pending_upgrades(
            record,
            base_directory,
            source_manifest_cache=source_manifest_cache,
        )
    except (
        ProvenanceMaintenanceError,
        generated_provenance.GeneratedProvenanceError,
    ):
        return False
    if not upgrades:
        return False
    issues = generated_provenance.check_record(
        record,
        base_directory,
        check_context=check_context,
    )
    return all(_repairable_issue(issue, upgrades) for issue in issues)


def _maintain_record_once(manifest_path, record_key):
    """Apply every pending metadata upgrade to one generated work unit."""

    manifest_path = Path(manifest_path).resolve()
    manifest = generated_provenance.load_manifest(manifest_path)
    try:
        original_record = manifest["records"][record_key]
    except KeyError as error:
        raise ProvenanceMaintenanceError(
            "{} has no record '{}'".format(manifest_path, record_key)
        ) from error
    base_directory = (
        manifest_path.parent / manifest["path_base"]
    ).resolve()
    try:
        upgrades = pending_upgrades(original_record, base_directory)
    except ProvenanceMaintenanceError as error:
        issues = generated_provenance.check_record(
            original_record, base_directory
        )
        if issues:
            raise ProvenanceMaintenanceError(
                "{} cannot receive metadata maintenance: {}".format(
                    record_key, "; ".join(issues)
                )
            ) from error
        raise
    issues = generated_provenance.check_record(original_record, base_directory)
    data_issues = [
        issue
        for issue in issues
        if not _repairable_issue(issue, upgrades)
    ]
    if data_issues:
        raise ProvenanceMaintenanceError(
            "{} cannot receive metadata maintenance: {}".format(
                record_key, "; ".join(data_issues)
            )
        )

    record = copy.deepcopy(original_record)
    if not upgrades:
        return 0

    applied = record.setdefault("provenance_maintenance", [])
    for item in upgrades:
        event = item["event"]
        UPGRADES[event["provenance_upgrade"]](
            record,
            {
                "record_key": record_key,
                "source_category": item["category"],
                "source_manifest": item["manifest"],
                "event": event,
                "base_directory": base_directory,
            },
        )
        applied.append(
            {
                "event_id": event["id"],
                "source_category": item["category"],
                "upgrade": event["provenance_upgrade"],
                "applied_at_utc": generated_provenance.utc_now(),
            }
        )

    for category_id in sorted(
        {item["category"] for item in upgrades}
    ):
        dependency = record["dependencies"][category_id]
        source_manifest_path = (
            base_directory / dependency["manifest"]
        ).resolve()
        record["dependencies"][category_id] = (
            generated_provenance.source_manifest_dependency(
                category_id,
                source_manifest_path,
                base_directory,
            )
        )

    generated_provenance.update_manifest(
        manifest_path,
        {record_key: record},
        {},
        path_base=manifest["path_base"],
        expected_records={record_key: original_record},
    )
    return len(upgrades)


def maintain_record(manifest_path, record_key):
    """Apply upgrades, retrying if a generator publishes at the same time."""

    while True:
        try:
            return _maintain_record_once(manifest_path, record_key)
        except generated_provenance.ConcurrentManifestUpdate:
            # Re-read and reapply upgrades instead of replacing a newer record
            # with metadata derived from the previous one.
            continue

"""Apply explicit metadata-only upgrades to generated provenance records."""

import copy
from pathlib import Path

import generated_provenance
import source_provenance


class ProvenanceMaintenanceError(ValueError):
    """Raised when metadata cannot be upgraded without regeneration."""


def _no_generated_metadata_change(record, context):
    """Record an orchestration change that has no generated-record payload."""


def _refresh_source_dependency(record, context):
    """Refresh a source dependency after metadata-only bookkeeping changes."""


UPGRADES = {
    "no-generated-metadata-change-v1": _no_generated_metadata_change,
    "refresh-source-dependency-v1": _refresh_source_dependency,
}


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


def pending_upgrades(record, base_directory):
    """Return applicable provenance-only source events in execution order."""

    pending = []
    target_scope = _target_scope(record)
    for category_id, dependency in record["dependencies"].items():
        if dependency["kind"] != "source_manifest":
            continue
        manifest_path = (
            Path(base_directory) / dependency["manifest"]
        ).resolve()
        source_manifest = source_provenance.load_manifest(manifest_path)
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
    issues = generated_provenance.check_record(
        original_record,
        base_directory,
    )
    data_issues = [
        issue
        for issue in issues
        if not issue.startswith("provenance-only dependency revision ")
    ]
    if data_issues:
        raise ProvenanceMaintenanceError(
            "{} cannot receive metadata maintenance: {}".format(
                record_key, "; ".join(data_issues)
            )
        )

    record = copy.deepcopy(original_record)
    upgrades = pending_upgrades(record, base_directory)
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

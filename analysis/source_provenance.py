"""Record and validate provenance for maintained analysis inputs and code.

Source manifests are intentionally separate from generated-data manifests.
They are designed to be committed to Git and normally maintained as one file
per source folder. This utility uses only the Python standard library.
"""

import argparse
import fnmatch
import hashlib
import json
import os
import re
import sys
import tempfile
import uuid
from datetime import date, datetime, timezone
from pathlib import Path, PurePosixPath


SCHEMA_VERSION = 1
SCHEMA_PATH = Path(__file__).with_name("source_provenance.schema.json")
CHANGE_TYPES = {
    "baseline",
    "correction",
    "coverage_extension",
    "source_refresh",
    "methodology",
    "schema",
    "formatting",
    "other",
}
RECORD_CHANGE_TYPES = CHANGE_TYPES - {"baseline"}
MAGNITUDES = {"unknown", "negligible", "minor", "material", "major"}
RECORD_MAGNITUDES = MAGNITUDES - {"unknown"}
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


class ProvenanceError(ValueError):
    """Raised when a manifest or requested provenance operation is invalid."""


def utc_now():
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def _mtime_utc(path):
    return (
        datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
        .isoformat()
        .replace("+00:00", "Z")
    )


def _parse_datetime(value, context):
    if not isinstance(value, str):
        raise ProvenanceError("{} must be an ISO date-time string".format(context))
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise ProvenanceError(
            "{} is not a valid ISO date-time: {}".format(context, value)
        ) from error


def _parse_date(value, context):
    if not isinstance(value, str):
        raise ProvenanceError("{} must be an ISO date string".format(context))
    try:
        date.fromisoformat(value)
    except ValueError as error:
        raise ProvenanceError(
            "{} is not a valid ISO date: {}".format(context, value)
        ) from error


def _require_non_empty_string(value, context):
    if not isinstance(value, str) or not value.strip():
        raise ProvenanceError("{} must be a non-empty string".format(context))


def _require_exact_keys(item, required, optional, context):
    if not isinstance(item, dict):
        raise ProvenanceError("{} must be an object".format(context))
    missing = sorted(set(required) - set(item))
    unexpected = sorted(set(item) - set(required) - set(optional))
    if missing:
        raise ProvenanceError(
            "{} is missing field(s): {}".format(context, ", ".join(missing))
        )
    if unexpected:
        raise ProvenanceError(
            "{} has unexpected field(s): {}".format(
                context, ", ".join(unexpected)
            )
        )


def _validate_relative_path(value, context, allow_patterns=False):
    _require_non_empty_string(value, context)
    if "\\" in value:
        raise ProvenanceError(
            "{} must use forward slashes for portability".format(context)
        )
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts:
        raise ProvenanceError(
            "{} must be relative and remain inside its source folder".format(
                context
            )
        )
    if not allow_patterns and any(
        character in value for character in ("*", "?", "[", "]")
    ):
        raise ProvenanceError(
            "{} must be a concrete relative path".format(context)
        )


def _validate_string_list(
    value, context, allow_empty=True, relative_paths=False, allow_patterns=False
):
    if not isinstance(value, list):
        raise ProvenanceError("{} must be a list".format(context))
    if not allow_empty and not value:
        raise ProvenanceError("{} must not be empty".format(context))
    for index, item in enumerate(value):
        _require_non_empty_string(item, "{}[{}]".format(context, index))
        if relative_paths:
            _validate_relative_path(
                item,
                "{}[{}]".format(context, index),
                allow_patterns=allow_patterns,
            )
    if len(value) != len(set(value)):
        raise ProvenanceError("{} contains duplicate values".format(context))


def _validate_source(source, context):
    _require_exact_keys(
        source,
        required={"description"},
        optional={"uri", "retrieved_at_utc", "coverage_through"},
        context=context,
    )
    _require_non_empty_string(source["description"], "{}.description".format(context))
    if "uri" in source:
        _require_non_empty_string(source["uri"], "{}.uri".format(context))
    if "retrieved_at_utc" in source:
        _parse_datetime(
            source["retrieved_at_utc"],
            "{}.retrieved_at_utc".format(context),
        )
    if "coverage_through" in source:
        _parse_date(
            source["coverage_through"],
            "{}.coverage_through".format(context),
        )


def _validate_scope(scope, context):
    _require_exact_keys(
        scope,
        required={"all", "elections", "parties", "stages"},
        optional=set(),
        context=context,
    )
    if not isinstance(scope["all"], bool):
        raise ProvenanceError("{}.all must be a boolean".format(context))
    for field in ("elections", "parties", "stages"):
        _validate_string_list(scope[field], "{}.{}".format(context, field))
    has_selectors = any(
        scope[field] for field in ("elections", "parties", "stages")
    )
    if scope["all"] and has_selectors:
        raise ProvenanceError(
            "{} cannot combine all=true with scope selectors".format(context)
        )
    if not scope["all"] and not has_selectors:
        raise ProvenanceError(
            "{} must select at least one election, party or stage".format(
                context
            )
        )


def _validate_file_fingerprint(fingerprint, context):
    _require_exact_keys(
        fingerprint,
        required={"sha256", "size_bytes", "recorded_mtime_utc"},
        optional=set(),
        context=context,
    )
    if (
        not isinstance(fingerprint["sha256"], str)
        or not SHA256_PATTERN.fullmatch(fingerprint["sha256"])
    ):
        raise ProvenanceError("{}.sha256 must be 64 lowercase hex digits".format(context))
    if (
        not isinstance(fingerprint["size_bytes"], int)
        or isinstance(fingerprint["size_bytes"], bool)
        or fingerprint["size_bytes"] < 0
    ):
        raise ProvenanceError(
            "{}.size_bytes must be a non-negative integer".format(context)
        )
    _parse_datetime(
        fingerprint["recorded_mtime_utc"],
        "{}.recorded_mtime_utc".format(context),
    )


def _validate_event(event, context, expected_previous_revision, is_first):
    _require_exact_keys(
        event,
        required={
            "id",
            "recorded_at_utc",
            "change_type",
            "magnitude",
            "affects_outputs",
            "semantic_revision",
            "summary",
            "files",
            "scope",
        },
        optional={"source"},
        context=context,
    )
    _require_non_empty_string(event["id"], "{}.id".format(context))
    _parse_datetime(event["recorded_at_utc"], "{}.recorded_at_utc".format(context))
    if event["change_type"] not in CHANGE_TYPES:
        raise ProvenanceError(
            "{}.change_type is unsupported: {}".format(
                context, event["change_type"]
            )
        )
    if event["magnitude"] not in MAGNITUDES:
        raise ProvenanceError(
            "{}.magnitude is unsupported: {}".format(
                context, event["magnitude"]
            )
        )
    if not isinstance(event["affects_outputs"], bool):
        raise ProvenanceError(
            "{}.affects_outputs must be a boolean".format(context)
        )
    if (
        not isinstance(event["semantic_revision"], int)
        or isinstance(event["semantic_revision"], bool)
        or event["semantic_revision"] < 1
    ):
        raise ProvenanceError(
            "{}.semantic_revision must be a positive integer".format(context)
        )
    _require_non_empty_string(event["summary"], "{}.summary".format(context))
    _validate_string_list(
        event["files"],
        "{}.files".format(context),
        relative_paths=True,
    )
    _validate_scope(event["scope"], "{}.scope".format(context))
    if "source" in event:
        _validate_source(event["source"], "{}.source".format(context))

    if is_first:
        if event["change_type"] != "baseline":
            raise ProvenanceError("{} must be a baseline event".format(context))
        if event["magnitude"] != "unknown":
            raise ProvenanceError(
                "{} baseline magnitude must be unknown".format(context)
            )
        if not event["affects_outputs"]:
            raise ProvenanceError(
                "{} baseline must establish an output-affecting revision".format(
                    context
                )
            )
        expected_revision = 1
    else:
        if event["change_type"] == "baseline":
            raise ProvenanceError(
                "{} cannot contain a second baseline event".format(context)
            )
        if event["magnitude"] == "unknown":
            raise ProvenanceError(
                "{} non-baseline magnitude must be assessed".format(context)
            )
        if not event["affects_outputs"] and event["magnitude"] != "negligible":
            raise ProvenanceError(
                "{} non-output-affecting changes must have negligible magnitude".format(
                    context
                )
            )
        expected_revision = (
            expected_previous_revision + 1
            if event["affects_outputs"]
            else expected_previous_revision
        )

    if event["semantic_revision"] != expected_revision:
        raise ProvenanceError(
            "{} semantic revision is {}, expected {}".format(
                context, event["semantic_revision"], expected_revision
            )
        )
    return expected_revision


def _validate_category(category_id, category):
    context = "category '{}'".format(category_id)
    _require_non_empty_string(category_id, "category id")
    _require_exact_keys(
        category,
        required={
            "description",
            "file_patterns",
            "exclude_patterns",
            "semantic_revision",
            "files",
            "events",
        },
        optional=set(),
        context=context,
    )
    _require_non_empty_string(
        category["description"], "{}.description".format(context)
    )
    _validate_string_list(
        category["file_patterns"],
        "{}.file_patterns".format(context),
        allow_empty=False,
        relative_paths=True,
        allow_patterns=True,
    )
    _validate_string_list(
        category["exclude_patterns"],
        "{}.exclude_patterns".format(context),
        relative_paths=True,
        allow_patterns=True,
    )
    if (
        not isinstance(category["semantic_revision"], int)
        or isinstance(category["semantic_revision"], bool)
        or category["semantic_revision"] < 1
    ):
        raise ProvenanceError(
            "{}.semantic_revision must be a positive integer".format(context)
        )
    if not isinstance(category["files"], dict):
        raise ProvenanceError("{}.files must be an object".format(context))
    for path, fingerprint in category["files"].items():
        _validate_relative_path(path, "{}.files path".format(context))
        _validate_file_fingerprint(
            fingerprint, "{}.files['{}']".format(context, path)
        )
    if not isinstance(category["events"], list) or not category["events"]:
        raise ProvenanceError("{}.events must be a non-empty list".format(context))

    event_ids = set()
    revision = 0
    for index, event in enumerate(category["events"]):
        event_context = "{}.events[{}]".format(context, index)
        revision = _validate_event(
            event,
            event_context,
            expected_previous_revision=revision,
            is_first=(index == 0),
        )
        if event["id"] in event_ids:
            raise ProvenanceError(
                "{} has duplicate event id '{}'".format(context, event["id"])
            )
        event_ids.add(event["id"])
    if category["semantic_revision"] != revision:
        raise ProvenanceError(
            "{} semantic revision is {}, but its events end at {}".format(
                context, category["semantic_revision"], revision
            )
        )


def validate_manifest(manifest):
    _require_exact_keys(
        manifest,
        required={
            "$schema",
            "schema_version",
            "folder",
            "description",
            "created_at_utc",
            "updated_at_utc",
            "categories",
        },
        optional=set(),
        context="manifest",
    )
    _require_non_empty_string(manifest["$schema"], "manifest.$schema")
    if manifest["schema_version"] != SCHEMA_VERSION:
        raise ProvenanceError(
            "unsupported schema_version {}".format(manifest["schema_version"])
        )
    _validate_relative_path(manifest["folder"], "manifest.folder")
    _require_non_empty_string(manifest["description"], "manifest.description")
    _parse_datetime(manifest["created_at_utc"], "manifest.created_at_utc")
    _parse_datetime(manifest["updated_at_utc"], "manifest.updated_at_utc")
    if not isinstance(manifest["categories"], dict):
        raise ProvenanceError("manifest.categories must be an object")

    recorded_paths = {}
    for category_id, category in manifest["categories"].items():
        _validate_category(category_id, category)
        for path in category["files"]:
            if path in recorded_paths:
                raise ProvenanceError(
                    "file '{}' is recorded in both '{}' and '{}'".format(
                        path, recorded_paths[path], category_id
                    )
                )
            recorded_paths[path] = category_id


def load_manifest(manifest_path):
    path = Path(manifest_path)
    try:
        with path.open("r", encoding="utf-8") as manifest_file:
            manifest = json.load(manifest_file)
    except FileNotFoundError as error:
        raise ProvenanceError(
            "provenance manifest does not exist: {}".format(path)
        ) from error
    except json.JSONDecodeError as error:
        raise ProvenanceError(
            "could not parse {}: {}".format(path, error)
        ) from error
    validate_manifest(manifest)
    return manifest


def _atomic_write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            newline="\n",
            dir=str(path.parent),
            prefix=".{}.".format(path.name),
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            json.dump(value, temporary_file, indent=2, sort_keys=False)
            temporary_file.write("\n")
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(str(temporary_path), str(path))
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()


def _schema_reference(manifest_path):
    resolved_schema = SCHEMA_PATH.resolve()
    manifest_folder = Path(manifest_path).resolve().parent
    try:
        relative_schema = os.path.relpath(
            str(resolved_schema), str(manifest_folder)
        )
    except ValueError:
        # Windows cannot express a relative path between different drives.
        return resolved_schema.as_uri()
    return Path(relative_schema).as_posix()


def initialize_manifest(manifest_path, description, folder="."):
    manifest_path = Path(manifest_path)
    if manifest_path.exists():
        raise ProvenanceError(
            "refusing to overwrite existing manifest: {}".format(manifest_path)
        )
    _require_non_empty_string(description, "description")
    _validate_relative_path(folder, "folder")
    now = utc_now()
    manifest = {
        "$schema": _schema_reference(manifest_path),
        "schema_version": SCHEMA_VERSION,
        "folder": folder,
        "description": description,
        "created_at_utc": now,
        "updated_at_utc": now,
        "categories": {},
    }
    validate_manifest(manifest)
    _atomic_write_json(manifest_path, manifest)
    return manifest


def _source_folder(manifest_path, manifest):
    folder = (Path(manifest_path).resolve().parent / manifest["folder"]).resolve()
    if not folder.is_dir():
        raise ProvenanceError(
            "manifest source folder does not exist: {}".format(folder)
        )
    return folder


def _canonical_source_bytes(path):
    # Git normalizes text files to LF, while Windows worktrees commonly use
    # CRLF. Source provenance tracks logical text rather than checkout format.
    return path.read_bytes().replace(b"\r\n", b"\n")


def _hash_bytes(value):
    digest = hashlib.sha256()
    digest.update(value)
    return digest.hexdigest()


def _fingerprint_file(path):
    canonical_bytes = _canonical_source_bytes(path)
    return {
        "sha256": _hash_bytes(canonical_bytes),
        "size_bytes": len(canonical_bytes),
        "recorded_mtime_utc": _mtime_utc(path),
    }


def _matches_any(path, patterns):
    return any(fnmatch.fnmatch(path, pattern) for pattern in patterns)


def snapshot_category(manifest_path, manifest, category):
    folder = _source_folder(manifest_path, manifest)
    matched_paths = set()
    for pattern in category["file_patterns"]:
        for path in folder.glob(pattern):
            if not path.is_file():
                continue
            relative_path = path.relative_to(folder).as_posix()
            if _matches_any(relative_path, category["exclude_patterns"]):
                continue
            if path.resolve() == Path(manifest_path).resolve():
                continue
            matched_paths.add(relative_path)

    return {
        relative_path: _fingerprint_file(folder / relative_path)
        for relative_path in sorted(matched_paths)
    }


def compare_snapshots(recorded, current):
    recorded_paths = set(recorded)
    current_paths = set(current)
    added = sorted(current_paths - recorded_paths)
    removed = sorted(recorded_paths - current_paths)
    modified = sorted(
        path
        for path in recorded_paths & current_paths
        if recorded[path]["sha256"] != current[path]["sha256"]
        or recorded[path]["size_bytes"] != current[path]["size_bytes"]
    )
    touched = sorted(
        path
        for path in recorded_paths & current_paths
        if path not in modified
        and recorded[path]["recorded_mtime_utc"]
        != current[path]["recorded_mtime_utc"]
    )
    return {
        "added": added,
        "removed": removed,
        "modified": modified,
        "touched": touched,
    }


def _changed_paths(comparison):
    return sorted(
        set(
            comparison["added"]
            + comparison["removed"]
            + comparison["modified"]
            + comparison["touched"]
        )
    )


def _event_id(change_type, category_id):
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    suffix = uuid.uuid4().hex[:8]
    safe_category = re.sub(r"[^A-Za-z0-9]+", "-", category_id).strip("-")
    return "{}-{}-{}-{}".format(timestamp, change_type, safe_category, suffix)


def _build_source(
    source_description=None,
    source_uri=None,
    retrieved_at_utc=None,
    coverage_through=None,
):
    if not any(
        (source_description, source_uri, retrieved_at_utc, coverage_through)
    ):
        return None
    if not source_description:
        raise ProvenanceError(
            "source_description is required when recording source details"
        )
    source = {"description": source_description}
    if source_uri:
        source["uri"] = source_uri
    if retrieved_at_utc:
        source["retrieved_at_utc"] = retrieved_at_utc
    if coverage_through:
        source["coverage_through"] = coverage_through
    _validate_source(source, "source")
    return source


def _build_scope(all_scopes=False, elections=None, parties=None, stages=None):
    scope = {
        "all": bool(all_scopes),
        "elections": sorted(set(elections or [])),
        "parties": sorted(set(parties or [])),
        "stages": sorted(set(stages or [])),
    }
    _validate_scope(scope, "scope")
    return scope


def scope_affects_target(change_scope, target_scope):
    """Return whether a semantic change can affect a target work unit.

    Values are alternatives within a dimension, while populated dimensions
    jointly constrain the change. A missing target dimension is treated
    conservatively as unknown rather than as proof that the change is
    irrelevant.
    """

    _validate_scope(change_scope, "change_scope")
    _validate_scope(target_scope, "target_scope")
    if change_scope["all"] or target_scope["all"]:
        return True

    for field in ("elections", "parties", "stages"):
        change_values = {
            value.casefold() for value in change_scope[field]
        }
        target_values = {
            value.casefold() for value in target_scope[field]
        }
        if (
            change_values
            and target_values
            and change_values.isdisjoint(target_values)
        ):
            return False
    return True


def semantic_events_affecting(category, after_revision, target_scope):
    """Return relevant output-affecting events newer than a recorded revision."""

    if (
        not isinstance(after_revision, int)
        or isinstance(after_revision, bool)
        or after_revision < 0
    ):
        raise ProvenanceError(
            "after_revision must be a non-negative integer"
        )
    current_revision = category["semantic_revision"]
    if after_revision > current_revision:
        raise ProvenanceError(
            "after_revision {} is newer than category revision {}".format(
                after_revision, current_revision
            )
        )
    _validate_scope(target_scope, "target_scope")
    return [
        event
        for event in category["events"]
        if event["affects_outputs"]
        and event["semantic_revision"] > after_revision
        and scope_affects_target(event["scope"], target_scope)
    ]


def _scope_description(scope):
    if scope["all"]:
        return "all scopes"
    parts = []
    for field in ("elections", "parties", "stages"):
        if scope[field]:
            parts.append("{}={}".format(field, "|".join(scope[field])))
    return ", ".join(parts)


def add_category(
    manifest_path,
    category_id,
    description,
    file_patterns,
    exclude_patterns=None,
    summary="Initial provenance baseline.",
    source=None,
    allow_empty=False,
):
    manifest = load_manifest(manifest_path)
    if category_id in manifest["categories"]:
        raise ProvenanceError(
            "category '{}' already exists".format(category_id)
        )
    category = {
        "description": description,
        "file_patterns": list(file_patterns),
        "exclude_patterns": list(exclude_patterns or []),
        "semantic_revision": 1,
        "files": {},
        "events": [],
    }
    _validate_string_list(
        category["file_patterns"],
        "file_patterns",
        allow_empty=False,
        relative_paths=True,
        allow_patterns=True,
    )
    _validate_string_list(
        category["exclude_patterns"],
        "exclude_patterns",
        relative_paths=True,
        allow_patterns=True,
    )
    category["files"] = snapshot_category(manifest_path, manifest, category)
    if not category["files"] and not allow_empty:
        raise ProvenanceError(
            "category '{}' matched no files; use --allow-empty if intentional".format(
                category_id
            )
        )

    existing_paths = {
        path
        for existing_category in manifest["categories"].values()
        for path in existing_category["files"]
    }
    overlap = sorted(existing_paths & set(category["files"]))
    if overlap:
        raise ProvenanceError(
            "category '{}' overlaps files already assigned elsewhere: {}".format(
                category_id, ", ".join(overlap)
            )
        )

    event = {
        "id": _event_id("baseline", category_id),
        "recorded_at_utc": utc_now(),
        "change_type": "baseline",
        "magnitude": "unknown",
        "affects_outputs": True,
        "semantic_revision": 1,
        "summary": summary,
        "files": sorted(category["files"]),
        "scope": {
            "all": True,
            "elections": [],
            "parties": [],
            "stages": [],
        },
    }
    if source is not None:
        event["source"] = source
    category["events"].append(event)
    manifest["categories"][category_id] = category
    manifest["updated_at_utc"] = utc_now()
    validate_manifest(manifest)
    _atomic_write_json(manifest_path, manifest)
    return category


def record_change(
    manifest_path,
    category_id,
    summary,
    change_type,
    magnitude,
    affects_outputs,
    scope,
    source=None,
    allow_empty=False,
):
    manifest = load_manifest(manifest_path)
    try:
        category = manifest["categories"][category_id]
    except KeyError as error:
        raise ProvenanceError(
            "unknown category '{}' in {}".format(category_id, manifest_path)
        ) from error
    if change_type not in RECORD_CHANGE_TYPES:
        raise ProvenanceError(
            "recorded change_type must be one of: {}".format(
                ", ".join(sorted(RECORD_CHANGE_TYPES))
            )
        )
    if magnitude not in RECORD_MAGNITUDES:
        raise ProvenanceError(
            "recorded magnitude must be one of: {}".format(
                ", ".join(sorted(RECORD_MAGNITUDES))
            )
        )
    if not affects_outputs and magnitude != "negligible":
        raise ProvenanceError(
            "non-output-affecting changes must have negligible magnitude"
        )
    _require_non_empty_string(summary, "summary")
    _validate_scope(scope, "scope")

    current_files = snapshot_category(manifest_path, manifest, category)
    if not current_files and not allow_empty:
        raise ProvenanceError(
            "category '{}' now matches no files; use --allow-empty if intentional".format(
                category_id
            )
        )
    comparison = compare_snapshots(category["files"], current_files)
    changed_paths = _changed_paths(comparison)
    if not changed_paths:
        raise ProvenanceError(
            "category '{}' has no physical file changes to record".format(
                category_id
            )
        )

    next_revision = category["semantic_revision"] + (1 if affects_outputs else 0)
    event = {
        "id": _event_id(change_type, category_id),
        "recorded_at_utc": utc_now(),
        "change_type": change_type,
        "magnitude": magnitude,
        "affects_outputs": affects_outputs,
        "semantic_revision": next_revision,
        "summary": summary,
        "files": changed_paths,
        "scope": scope,
    }
    if source is not None:
        event["source"] = source

    category["files"] = current_files
    category["semantic_revision"] = next_revision
    category["events"].append(event)
    manifest["updated_at_utc"] = utc_now()
    validate_manifest(manifest)
    _atomic_write_json(manifest_path, manifest)
    return event, comparison


def check_manifest(manifest_path):
    manifest = load_manifest(manifest_path)
    result = {}
    matched_paths = {}
    for category_id, category in manifest["categories"].items():
        current_files = snapshot_category(manifest_path, manifest, category)
        comparison = compare_snapshots(category["files"], current_files)
        result[category_id] = comparison
        for path in current_files:
            if path in matched_paths:
                raise ProvenanceError(
                    "current file '{}' matches both '{}' and '{}'".format(
                        path, matched_paths[path], category_id
                    )
                )
            matched_paths[path] = category_id
    return result


def _print_comparison(category_id, comparison):
    physical_changes = any(
        comparison[field] for field in ("added", "removed", "modified")
    )
    touched = bool(comparison["touched"])
    if not physical_changes and not touched:
        print("{}: current".format(category_id))
        return
    if physical_changes:
        print("{}: UNRECORDED CONTENT CHANGE".format(category_id))
    else:
        print("{}: content current; modification times changed".format(category_id))
    for field in ("added", "removed", "modified", "touched"):
        if comparison[field]:
            print("  {}: {}".format(field, ", ".join(comparison[field])))


def _add_source_arguments(parser):
    parser.add_argument("--source-description")
    parser.add_argument("--source-uri")
    parser.add_argument("--retrieved-at-utc")
    parser.add_argument("--coverage-through")


def _source_from_args(args):
    return _build_source(
        source_description=args.source_description,
        source_uri=args.source_uri,
        retrieved_at_utc=args.retrieved_at_utc,
        coverage_through=args.coverage_through,
    )


def _yes_no(value):
    normalized = value.lower()
    if normalized == "yes":
        return True
    if normalized == "no":
        return False
    raise argparse.ArgumentTypeError("expected 'yes' or 'no'")


def _non_negative_int(value):
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "expected a non-negative integer"
        ) from error
    if parsed < 0:
        raise argparse.ArgumentTypeError(
            "expected a non-negative integer"
        )
    return parsed


def _add_scope_arguments(parser):
    parser.add_argument("--all-scopes", action="store_true")
    parser.add_argument("--election", action="append", default=[])
    parser.add_argument("--party", action="append", default=[])
    parser.add_argument("--stage", action="append", default=[])


def build_parser():
    parser = argparse.ArgumentParser(
        description="Record and validate maintained analysis provenance."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    init_parser = subparsers.add_parser(
        "init", help="Create an empty folder provenance manifest."
    )
    init_parser.add_argument("manifest", type=Path)
    init_parser.add_argument("--description", required=True)
    init_parser.add_argument("--folder", default=".")

    add_parser = subparsers.add_parser(
        "add-category", help="Record the baseline for a source category."
    )
    add_parser.add_argument("manifest", type=Path)
    add_parser.add_argument("category")
    add_parser.add_argument("--description", required=True)
    add_parser.add_argument("--pattern", action="append", required=True)
    add_parser.add_argument("--exclude", action="append", default=[])
    add_parser.add_argument(
        "--summary", default="Initial provenance baseline."
    )
    add_parser.add_argument("--allow-empty", action="store_true")
    _add_source_arguments(add_parser)

    record_parser = subparsers.add_parser(
        "record", help="Assess and record current source-file changes."
    )
    record_parser.add_argument("manifest", type=Path)
    record_parser.add_argument("category")
    record_parser.add_argument("--summary", required=True)
    record_parser.add_argument(
        "--change-type",
        required=True,
        choices=sorted(RECORD_CHANGE_TYPES),
    )
    record_parser.add_argument(
        "--magnitude",
        required=True,
        choices=sorted(RECORD_MAGNITUDES),
    )
    record_parser.add_argument(
        "--affects-outputs", required=True, type=_yes_no
    )
    _add_scope_arguments(record_parser)
    record_parser.add_argument("--allow-empty", action="store_true")
    _add_source_arguments(record_parser)

    impact_parser = subparsers.add_parser(
        "impact",
        help="List semantic changes that affect a scoped downstream work unit.",
    )
    impact_parser.add_argument("manifest", type=Path)
    impact_parser.add_argument("category")
    impact_parser.add_argument(
        "--after-revision", required=True, type=_non_negative_int
    )
    _add_scope_arguments(impact_parser)

    validate_parser = subparsers.add_parser(
        "validate", help="Validate manifest structure and event history."
    )
    validate_parser.add_argument("manifests", nargs="+", type=Path)

    check_parser = subparsers.add_parser(
        "check",
        help="Validate manifests and compare recorded files with the filesystem.",
    )
    check_parser.add_argument("manifests", nargs="+", type=Path)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "init":
            initialize_manifest(args.manifest, args.description, args.folder)
            print("Created {}".format(args.manifest))
            return 0
        if args.command == "add-category":
            category = add_category(
                args.manifest,
                args.category,
                args.description,
                args.pattern,
                exclude_patterns=args.exclude,
                summary=args.summary,
                source=_source_from_args(args),
                allow_empty=args.allow_empty,
            )
            print(
                "Recorded baseline for {}: {} file(s)".format(
                    args.category, len(category["files"])
                )
            )
            return 0
        if args.command == "record":
            scope = _build_scope(
                all_scopes=args.all_scopes,
                elections=args.election,
                parties=args.party,
                stages=args.stage,
            )
            event, comparison = record_change(
                args.manifest,
                args.category,
                args.summary,
                args.change_type,
                args.magnitude,
                args.affects_outputs,
                scope,
                source=_source_from_args(args),
                allow_empty=args.allow_empty,
            )
            print(
                "Recorded {} at semantic revision {} ({} file(s))".format(
                    event["id"],
                    event["semantic_revision"],
                    len(_changed_paths(comparison)),
                )
            )
            return 0
        if args.command == "impact":
            manifest = load_manifest(args.manifest)
            try:
                category = manifest["categories"][args.category]
            except KeyError as error:
                raise ProvenanceError(
                    "unknown category '{}' in {}".format(
                        args.category, args.manifest
                    )
                ) from error
            target_scope = _build_scope(
                all_scopes=args.all_scopes,
                elections=args.election,
                parties=args.party,
                stages=args.stage,
            )
            events = semantic_events_affecting(
                category,
                args.after_revision,
                target_scope,
            )
            print(
                "{} at revision {}; target {}".format(
                    args.category,
                    category["semantic_revision"],
                    _scope_description(target_scope),
                )
            )
            if not events:
                print(
                    "No matching semantic changes after revision {}.".format(
                        args.after_revision
                    )
                )
                return 0
            print(
                "{} matching semantic change(s) after revision {}:".format(
                    len(events), args.after_revision
                )
            )
            for event in events:
                print(
                    "  revision {}: {} [{}; {}]".format(
                        event["semantic_revision"],
                        event["summary"],
                        event["magnitude"],
                        _scope_description(event["scope"]),
                    )
                )
            return 0
        if args.command == "validate":
            for manifest_path in args.manifests:
                load_manifest(manifest_path)
                print("{}: valid".format(manifest_path))
            return 0
        if args.command == "check":
            has_unrecorded_content = False
            for manifest_path in args.manifests:
                print("{}:".format(manifest_path))
                result = check_manifest(manifest_path)
                for category_id, comparison in result.items():
                    _print_comparison(category_id, comparison)
                    if any(
                        comparison[field]
                        for field in ("added", "removed", "modified")
                    ):
                        has_unrecorded_content = True
            return 2 if has_unrecorded_content else 0
    except ProvenanceError as error:
        print("Error: {}".format(error), file=sys.stderr)
        return 2
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())

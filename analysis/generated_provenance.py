"""Create and check bundled provenance manifests for generated analysis data.

This shared persistence layer does not perform forecast calculations. Generator
stages provide their outputs and direct dependencies; this module fingerprints
them, records one bundled manifest, then checks whether those exact inputs and
outputs remain current.

Main functions:
* ``file_dependency``, ``source_manifest_dependency`` and
  ``generated_manifest_dependency`` build validated direct-dependency records.
* ``generation_record`` and ``generation_run`` create standard work-unit
  metadata for a completed generator invocation.
* ``update_manifest`` atomically publishes one or more generated records.
* ``check_record`` and ``check_manifest`` perform the core freshness checks.
"""

import argparse
import copy
import concurrent.futures
import hashlib
import importlib.metadata
import json
import os
import platform
import subprocess
import sys
import tempfile
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

import source_provenance

if os.name == "nt":
    import msvcrt
else:
    import fcntl


SCHEMA_VERSION = 1
SCHEMA_PATH = Path(__file__).with_name("generated_provenance.schema.json")
SHA256_LENGTH = 64
GENERATED_MANIFEST_SUFFIX = "generated-provenance.json"
LOCK_RETRY_SECONDS = 0.05


# Manifest schema validation and safe concurrent writes

class GeneratedProvenanceError(ValueError):
    """Raised when generated provenance is invalid or cannot be verified."""


class ConcurrentManifestUpdate(GeneratedProvenanceError):
    """Raised when a record changed before its replacement was published."""


class _ManifestWriteLock:
    """Serialize the brief read-modify-write transaction for one manifest."""

    def __init__(self, manifest_path):
        manifest_path = Path(manifest_path)
        self.path = manifest_path.with_name(manifest_path.name + ".lock")
        self._file = None

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open("a+b")
        try:
            if os.name == "nt":
                self._file.seek(0, os.SEEK_END)
                if self._file.tell() == 0:
                    self._file.write(b"\0")
                    self._file.flush()
                while True:
                    try:
                        self._file.seek(0)
                        msvcrt.locking(
                            self._file.fileno(), msvcrt.LK_NBLCK, 1
                        )
                        break
                    except OSError:
                        time.sleep(LOCK_RETRY_SECONDS)
            else:
                fcntl.flock(self._file.fileno(), fcntl.LOCK_EX)
        except BaseException:
            self._file.close()
            self._file = None
            raise
        return self

    def __exit__(self, exception_type, exception, traceback):
        try:
            if os.name == "nt":
                self._file.seek(0)
                msvcrt.locking(
                    self._file.fileno(), msvcrt.LK_UNLCK, 1
                )
            else:
                fcntl.flock(self._file.fileno(), fcntl.LOCK_UN)
        finally:
            self._file.close()
            self._file = None
        return False


def _validate_manifest_path(path):
    if not Path(path).name.endswith(GENERATED_MANIFEST_SUFFIX):
        raise GeneratedProvenanceError(
            "generated provenance must use a filename ending in "
            "'{}'; plain provenance.json is reserved for committed source "
            "provenance".format(GENERATED_MANIFEST_SUFFIX)
        )


def utc_now():
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def _require_string(value, context):
    if not isinstance(value, str) or not value:
        raise GeneratedProvenanceError(
            "{} must be a non-empty string".format(context)
        )


def _validate_datetime(value, context):
    _require_string(value, context)
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise GeneratedProvenanceError(
            "{} must be an ISO date-time".format(context)
        ) from error


def _validate_relative_path(value, context, allow_parent=False):
    _require_string(value, context)
    if "\\" in value:
        raise GeneratedProvenanceError(
            "{} must use forward slashes".format(context)
        )
    path = PurePosixPath(value)
    if path.is_absolute() or (not allow_parent and ".." in path.parts):
        raise GeneratedProvenanceError(
            "{} must be a portable relative path".format(context)
        )


def _validate_fingerprint(value, context):
    if not {"sha256", "size_bytes"} <= set(value) or (
        set(value) - {"sha256", "size_bytes", "mtime_ns"}
    ):
        raise GeneratedProvenanceError(
            "{} has invalid fields".format(context)
        )
    digest = value["sha256"]
    if (
        not isinstance(digest, str)
        or len(digest) != SHA256_LENGTH
        or any(character not in "0123456789abcdef" for character in digest)
    ):
        raise GeneratedProvenanceError(
            "{}.sha256 is invalid".format(context)
        )
    if (
        not isinstance(value["size_bytes"], int)
        or isinstance(value["size_bytes"], bool)
        or value["size_bytes"] < 0
    ):
        raise GeneratedProvenanceError(
            "{}.size_bytes is invalid".format(context)
        )
    if "mtime_ns" in value and (
        not isinstance(value["mtime_ns"], int)
        or isinstance(value["mtime_ns"], bool)
        or value["mtime_ns"] < 0
    ):
        raise GeneratedProvenanceError(
            "{}.mtime_ns is invalid".format(context)
        )


def _validate_scope(scope, context):
    if set(scope) != {"all", "elections", "parties", "qualifiers"}:
        raise GeneratedProvenanceError(
            "{} has invalid fields".format(context)
        )
    if not isinstance(scope["all"], bool):
        raise GeneratedProvenanceError(
            "{}.all must be a boolean".format(context)
        )
    for field in ("elections", "parties"):
        values = scope[field]
        if (
            not isinstance(values, list)
            or any(not isinstance(value, str) or not value for value in values)
            or len(values) != len(set(values))
        ):
            raise GeneratedProvenanceError(
                "{}.{} must contain unique strings".format(context, field)
            )
    if not isinstance(scope["qualifiers"], dict) or any(
        not isinstance(key, str)
        or not key
        or not isinstance(value, str)
        for key, value in scope["qualifiers"].items()
    ):
        raise GeneratedProvenanceError(
            "{}.qualifiers must map strings to strings".format(context)
        )
    has_specific_scope = (
        scope["elections"] or scope["parties"] or scope["qualifiers"]
    )
    if scope["all"] == bool(has_specific_scope):
        raise GeneratedProvenanceError(
            "{} must select all scopes or at least one specific scope".format(
                context
            )
        )


def _validate_dependency(dependency, context):
    required = {
        "kind",
        "digest",
        "semantic_revision",
        "manifest",
        "files",
        "records",
    }
    allowed = required | {
        "non_invalidating_records",
        "provenance_revision",
    }
    if not required <= set(dependency) or not set(dependency) <= allowed:
        raise GeneratedProvenanceError(
            "{} has invalid fields".format(context)
        )
    if dependency["kind"] not in {
        "source_manifest",
        "files",
        "generated_manifest",
    }:
        raise GeneratedProvenanceError(
            "{}.kind is invalid".format(context)
        )
    _validate_fingerprint(
        {"sha256": dependency["digest"], "size_bytes": 0},
        "{}.digest".format(context),
    )
    revision = dependency["semantic_revision"]
    if revision is not None and (
        not isinstance(revision, int)
        or isinstance(revision, bool)
        or revision < 1
    ):
        raise GeneratedProvenanceError(
            "{}.semantic_revision is invalid".format(context)
        )
    provenance_revision = dependency.get(
        "provenance_revision", revision
    )
    if provenance_revision is not None and (
        not isinstance(provenance_revision, int)
        or isinstance(provenance_revision, bool)
        or provenance_revision < 1
    ):
        raise GeneratedProvenanceError(
            "{}.provenance_revision is invalid".format(context)
        )
    files = dependency["files"]
    if (
        not isinstance(files, list)
        or any(not isinstance(path, str) for path in files)
        or len(files) != len(set(files))
    ):
        raise GeneratedProvenanceError(
            "{}.files is invalid".format(context)
        )
    for index, path in enumerate(files):
        _validate_relative_path(
            path, "{}.files[{}]".format(context, index)
        )
    records = dependency["records"]
    if (
        not isinstance(records, list)
        or any(not isinstance(key, str) or not key for key in records)
        or len(records) != len(set(records))
    ):
        raise GeneratedProvenanceError(
            "{}.records is invalid".format(context)
        )
    non_invalidating_records = dependency.get(
        "non_invalidating_records", []
    )
    if (
        not isinstance(non_invalidating_records, list)
        or any(
            not isinstance(key, str) or not key
            for key in non_invalidating_records
        )
        or len(non_invalidating_records)
        != len(set(non_invalidating_records))
        or not set(non_invalidating_records) <= set(records)
    ):
        raise GeneratedProvenanceError(
            "{}.non_invalidating_records is invalid".format(context)
        )
    if dependency["kind"] == "source_manifest":
        if (
            revision is None
            or provenance_revision is None
            or not dependency["manifest"]
            or files
            or records
        ):
            raise GeneratedProvenanceError(
                "{} source-manifest dependency is incomplete".format(context)
            )
        _validate_relative_path(
            dependency["manifest"], "{}.manifest".format(context)
        )
    elif dependency["kind"] == "files":
        if (
            dependency["manifest"] is not None
            or revision is not None
            or provenance_revision is not None
            or not files
            or records
        ):
            raise GeneratedProvenanceError(
                "{} file dependency is incomplete".format(context)
            )
    elif (
        not dependency["manifest"]
        or revision is not None
        or provenance_revision is not None
        or files
        or not records
    ):
        raise GeneratedProvenanceError(
            "{} generated-manifest dependency is incomplete".format(context)
        )
    if (
        non_invalidating_records
        and dependency["kind"] != "generated_manifest"
    ):
        raise GeneratedProvenanceError(
            "{} only generated-manifest dependencies may contain "
            "non-invalidating records".format(context)
        )
    if dependency["manifest"] is not None:
        _validate_relative_path(
            dependency["manifest"], "{}.manifest".format(context)
        )


def validate_manifest(manifest):
    required = {
        "$schema",
        "schema_version",
        "path_base",
        "description",
        "updated_at_utc",
        "runs",
        "records",
    }
    if set(manifest) != required:
        raise GeneratedProvenanceError("manifest has invalid fields")
    if manifest["schema_version"] != SCHEMA_VERSION:
        raise GeneratedProvenanceError(
            "unsupported schema_version {}".format(manifest["schema_version"])
        )
    _require_string(manifest["$schema"], "manifest.$schema")
    _validate_relative_path(
        manifest["path_base"], "manifest.path_base", allow_parent=True
    )
    _require_string(manifest["description"], "manifest.description")
    _validate_datetime(
        manifest["updated_at_utc"], "manifest.updated_at_utc"
    )
    if not isinstance(manifest["runs"], dict):
        raise GeneratedProvenanceError("manifest.runs must be an object")
    for run_id, run in manifest["runs"].items():
        _require_string(run_id, "run id")
        if set(run) != {
            "generated_at_utc",
            "command",
            "source_revision",
            "environment",
        }:
            raise GeneratedProvenanceError(
                "run '{}' has invalid fields".format(run_id)
            )
        _validate_datetime(
            run["generated_at_utc"],
            "run '{}'.generated_at_utc".format(run_id),
        )
        if (
            not isinstance(run["command"], list)
            or not run["command"]
            or any(not isinstance(value, str) for value in run["command"])
        ):
            raise GeneratedProvenanceError(
                "run '{}'.command is invalid".format(run_id)
            )
        revision = run["source_revision"]
        if (
            set(revision) != {"system", "revision", "dirty"}
            or revision["system"] != "git"
            or (
                revision["revision"] is not None
                and not isinstance(revision["revision"], str)
            )
            or not isinstance(revision["dirty"], bool)
        ):
            raise GeneratedProvenanceError(
                "run '{}'.source_revision is invalid".format(run_id)
            )
        environment = run["environment"]
        if set(environment) != {
            "python_version",
            "python_implementation",
            "platform",
            "packages",
        }:
            raise GeneratedProvenanceError(
                "run '{}'.environment is invalid".format(run_id)
            )
        if any(
            not isinstance(environment[field], str)
            for field in (
                "python_version",
                "python_implementation",
                "platform",
            )
        ) or (
            not isinstance(environment["packages"], dict)
            or any(
                not isinstance(name, str)
                or not name
                or not isinstance(version, str)
                for name, version in environment["packages"].items()
            )
        ):
            raise GeneratedProvenanceError(
                "run '{}'.environment is invalid".format(run_id)
            )

    if not isinstance(manifest["records"], dict):
        raise GeneratedProvenanceError("manifest.records must be an object")

    output_owners = {}
    for record_key, record in manifest["records"].items():
        _require_string(record_key, "record key")
        required_fields = {
            "status",
            "category",
            "stage",
            "scope",
            "run",
            "random_seed",
            "dependencies",
            "outputs",
        }
        allowed_fields = required_fields | {"provenance_maintenance"}
        if (
            not required_fields <= set(record)
            or not set(record) <= allowed_fields
        ):
            raise GeneratedProvenanceError(
                "record '{}' has invalid fields".format(record_key)
            )
        maintenance = record.get("provenance_maintenance", [])
        if not isinstance(maintenance, list):
            raise GeneratedProvenanceError(
                "{}.provenance_maintenance is invalid".format(record_key)
            )
        for index, entry in enumerate(maintenance):
            entry_context = "{}.provenance_maintenance[{}]".format(
                record_key, index
            )
            if not isinstance(entry, dict) or set(entry) != {
                "event_id",
                "source_category",
                "upgrade",
                "applied_at_utc",
            }:
                raise GeneratedProvenanceError(
                    "{} has invalid fields".format(entry_context)
                )
            for field in ("event_id", "source_category", "upgrade"):
                _require_string(
                    entry[field],
                    "{}.{}".format(entry_context, field),
                )
            _validate_datetime(
                entry["applied_at_utc"],
                "{}.applied_at_utc".format(entry_context),
            )
        if record["status"] not in {"generated", "legacy"}:
            raise GeneratedProvenanceError(
                "{}.status is invalid".format(record_key)
            )
        _require_string(record["category"], "{}.category".format(record_key))
        _require_string(record["stage"], "{}.stage".format(record_key))
        _validate_scope(record["scope"], "{}.scope".format(record_key))
        if record["run"] not in manifest["runs"]:
            raise GeneratedProvenanceError(
                "{} references unknown run '{}'".format(
                    record_key, record["run"]
                )
            )
        random_seed = record["random_seed"]
        if not (
            random_seed is None
            or isinstance(random_seed, str)
            or (
                isinstance(random_seed, int)
                and not isinstance(random_seed, bool)
            )
        ):
            raise GeneratedProvenanceError(
                "{}.random_seed is invalid".format(record_key)
            )
        if not isinstance(record["dependencies"], dict):
            raise GeneratedProvenanceError(
                "{}.dependencies is invalid".format(record_key)
            )
        for category, dependency in record["dependencies"].items():
            _require_string(category, "dependency category")
            _validate_dependency(
                dependency,
                "{}.dependencies['{}']".format(record_key, category),
            )
        if not isinstance(record["outputs"], dict) or not record["outputs"]:
            raise GeneratedProvenanceError(
                "{}.outputs must not be empty".format(record_key)
            )
        for path, fingerprint in record["outputs"].items():
            _validate_relative_path(path, "{} output".format(record_key))
            _validate_fingerprint(
                fingerprint,
                "{}.outputs['{}']".format(record_key, path),
            )
            if path in output_owners:
                raise GeneratedProvenanceError(
                    "output '{}' is owned by both '{}' and '{}'".format(
                        path, output_owners[path], record_key
                    )
                )
            output_owners[path] = record_key


def load_manifest(path):
    _validate_manifest_path(path)
    try:
        with Path(path).open("r", encoding="utf-8") as manifest_file:
            manifest = json.load(manifest_file)
    except FileNotFoundError as error:
        raise GeneratedProvenanceError(
            "generated manifest does not exist: {}".format(path)
        ) from error
    except json.JSONDecodeError as error:
        raise GeneratedProvenanceError(
            "could not parse {}: {}".format(path, error)
        ) from error
    # Early v1 manifests predate generated-record dependencies. Upgrade them
    # in memory so the next successful generation can rewrite the current
    # shape without requiring users to delete local provenance.
    for record in manifest.get("records", {}).values():
        record.setdefault("status", "generated")
        record.setdefault("provenance_maintenance", [])
        for dependency in record.get("dependencies", {}).values():
            dependency.setdefault("records", [])
            dependency.setdefault(
                "provenance_revision",
                dependency.get("semantic_revision"),
            )
    for run in manifest.get("runs", {}).values():
        run.get("environment", {}).setdefault("packages", {})
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
            json.dump(value, temporary_file, indent=2)
            temporary_file.write("\n")
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(str(temporary_path), str(path))
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()


def _hash_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as input_file:
        for block in iter(lambda: input_file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _canonical_path_key(path):
    """Return one cache key for equivalent filesystem spellings.

    Windows can expose the same temporary directory through both an 8.3 short
    name and its expanded name. ``Path.resolve`` collapses that distinction;
    ``normcase`` also handles Windows' case-insensitive path semantics.
    """

    path = Path(path)
    if os.name == "nt":
        # Windows resolution expands 8.3 aliases such as RUNNER~1.
        path = path.resolve()
    else:
        # POSIX has no 8.3 aliases. Avoid filesystem traversal here because an
        # audit may normalize thousands of files on a mounted Windows volume.
        path = Path(os.path.abspath(str(path)))
    return os.path.normcase(str(path))


# File/dependency fingerprint construction

def fingerprint_file(path):
    path = Path(path)
    if not path.is_file():
        raise GeneratedProvenanceError(
            "required file does not exist: {}".format(path)
        )
    stat = path.stat()
    return {
        "sha256": _hash_file(path),
        "size_bytes": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
    }


def fingerprint_digest(fingerprints):
    digest = hashlib.sha256()
    for path, fingerprint in sorted(fingerprints.items()):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(fingerprint["sha256"].encode("ascii"))
        digest.update(b"\0")
        digest.update(str(fingerprint["size_bytes"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _relative_to_base(path, base_directory):
    try:
        return Path(path).resolve().relative_to(
            Path(base_directory).resolve()
        ).as_posix()
    except ValueError as error:
        raise GeneratedProvenanceError(
            "{} is outside provenance path base {}".format(
                path, base_directory
            )
        ) from error


def file_dependency(
    category, files, base_directory, fingerprint_cache=None
):
    relative_files = sorted(
        _relative_to_base(path, base_directory) for path in files
    )
    fingerprints = {}
    for path in relative_files:
        unresolved_path = Path(base_directory) / path
        absolute_path = unresolved_path.resolve()
        cache_keys = (
            str(unresolved_path),
            str(absolute_path),
            _canonical_path_key(absolute_path),
        )
        fingerprint = None
        if fingerprint_cache is not None:
            fingerprint = next(
                (
                    fingerprint_cache[cache_key]
                    for cache_key in cache_keys
                    if cache_key in fingerprint_cache
                ),
                None,
            )
        if fingerprint is None:
            fingerprint = fingerprint_file(absolute_path)
            if fingerprint_cache is not None:
                fingerprint_cache[
                    _canonical_path_key(absolute_path)
                ] = fingerprint
        fingerprints[path] = fingerprint
    return {
        "kind": "files",
        "digest": fingerprint_digest(fingerprints),
        "semantic_revision": None,
        "provenance_revision": None,
        "manifest": None,
        "files": relative_files,
        "records": [],
    }


def source_manifest_dependency(
    category_id, manifest_path, base_directory
):
    manifest = source_provenance.load_manifest(manifest_path)
    try:
        category = manifest["categories"][category_id]
    except KeyError as error:
        raise GeneratedProvenanceError(
            "source manifest {} has no category '{}'".format(
                manifest_path, category_id
            )
        ) from error
    comparison = source_provenance.check_manifest(manifest_path)[category_id]
    physical_changes = (
        comparison["added"]
        + comparison["removed"]
        + comparison["modified"]
    )
    if physical_changes:
        raise GeneratedProvenanceError(
            "source category '{}' has unrecorded content changes: {}".format(
                category_id, ", ".join(physical_changes)
            )
        )
    fingerprints = {
        path: {
            "sha256": fingerprint["sha256"],
            "size_bytes": fingerprint["size_bytes"],
        }
        for path, fingerprint in category["files"].items()
    }
    return {
        "kind": "source_manifest",
        "digest": fingerprint_digest(fingerprints),
        "semantic_revision": category["semantic_revision"],
        "provenance_revision":
            source_provenance.category_provenance_revision(category),
        "manifest": _relative_to_base(manifest_path, base_directory),
        "files": [],
        "records": [],
    }


def _canonical_record_for_digest(record):
    """Return the digest payload for one generated record."""

    # Execution time and environment do not make an unchanged generated
    # result stale. The record's inputs, scope and output hashes do.
    payload = {
        key: copy.deepcopy(value)
        for key, value in record.items()
        if key not in {"run", "outputs", "provenance_maintenance"}
    }
    for dependency in payload["dependencies"].values():
        dependency.pop("provenance_revision", None)
    payload["outputs"] = {
        path: {
            "sha256": fingerprint["sha256"],
            "size_bytes": fingerprint["size_bytes"],
        }
        for path, fingerprint in record["outputs"].items()
    }
    return payload


def _record_digest_fragment(record):
    """Serialize one record the same way ``json.dumps`` would in a digest."""

    return json.dumps(
        _canonical_record_for_digest(record),
        sort_keys=True,
        separators=(",", ":"),
    )


def _generated_records_digest(manifest, record_keys, fragment_cache=None):
    """Digest selected records, optionally reusing per-record JSON fragments."""

    parts = []
    for record_key in sorted(record_keys):
        try:
            record = manifest["records"][record_key]
        except KeyError as error:
            raise GeneratedProvenanceError(
                "generated manifest has no record '{}'".format(record_key)
            ) from error
        if fragment_cache is None:
            fragment = _record_digest_fragment(record)
        else:
            fragment = fragment_cache.get(record_key)
            if fragment is None:
                fragment = _record_digest_fragment(record)
                fragment_cache[record_key] = fragment
        parts.append((record_key, fragment))
    # Match json.dumps({key: payload}, sort_keys=True, separators=(",", ":")).
    serialized = (
        "{"
        + ",".join(
            "{}:{}".format(json.dumps(record_key), fragment)
            for record_key, fragment in parts
        )
        + "}"
    ).encode("utf-8")
    return hashlib.sha256(serialized).hexdigest()


def _data_affecting_issues(issues):
    """Exclude maintainable metadata-only notices from freshness checks."""

    return [
        issue
        for issue in issues
        if not issue.startswith("provenance-only dependency revision ")
    ]


def generated_manifest_dependency(
    category,
    manifest_path,
    record_keys,
    base_directory,
    allow_stale=False,
    non_invalidating_records=(),
    _context=None,
):
    record_keys = sorted(set(record_keys))
    non_invalidating_records = sorted(set(non_invalidating_records))
    if not record_keys:
        raise GeneratedProvenanceError(
            "generated dependency '{}' has no records".format(category)
        )
    context = _context or ManifestCheckContext()
    manifest = context.load_manifest(manifest_path)
    if not set(non_invalidating_records) <= set(record_keys):
        raise GeneratedProvenanceError(
            "non-invalidating records for '{}' are not dependencies".format(
                category
            )
        )
    tracked_record_keys = [
        record_key
        for record_key in record_keys
        if record_key not in non_invalidating_records
    ]
    checked_records = check_manifest(
        manifest_path,
        record_keys=record_keys,
        _context=context,
    )
    stale_records = {}
    for record_key in tracked_record_keys:
        if record_key not in checked_records:
            stale_records[record_key] = ["missing record"]
        else:
            # Metadata-only source revisions make a record maintainable, not
            # unsafe to consume.  Callers that require fresh data should
            # reject only data-affecting ancestry here; the audit can still
            # schedule separate provenance maintenance.
            data_issues = _data_affecting_issues(
                checked_records[record_key]
            )
            if data_issues:
                stale_records[record_key] = data_issues
    if stale_records and not allow_stale:
        raise GeneratedProvenanceError(
            "generated dependency '{}' is stale: {}".format(
                category,
                ", ".join(sorted(stale_records)),
            )
        )
    dependency = {
        "kind": "generated_manifest",
        "digest": _generated_records_digest(
            manifest, tracked_record_keys
        ),
        "semantic_revision": None,
        "provenance_revision": None,
        "manifest": _relative_to_base(manifest_path, base_directory),
        "files": [],
        "records": record_keys,
    }
    if non_invalidating_records:
        dependency["non_invalidating_records"] = (
            non_invalidating_records
        )
    return dependency


def output_fingerprints(files, base_directory):
    return {
        _relative_to_base(path, base_directory): fingerprint_file(path)
        for path in sorted(Path(path) for path in files)
    }


def current_source_revision(base_directory):
    def run_git(*arguments):
        return subprocess.run(
            ["git", "-C", str(base_directory)] + list(arguments),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        ).stdout.strip()

    try:
        revision = run_git("rev-parse", "HEAD")
        dirty = bool(run_git("status", "--porcelain", "--untracked-files=all"))
    except (OSError, subprocess.CalledProcessError):
        revision = None
        dirty = True
    return {"system": "git", "revision": revision, "dirty": dirty}


def current_environment(package_names=None):
    packages = {}
    for package_name in sorted(set(package_names or [])):
        try:
            packages[package_name] = importlib.metadata.version(package_name)
        except importlib.metadata.PackageNotFoundError:
            packages[package_name] = "not-installed"
    return {
        "python_version": platform.python_version(),
        "python_implementation": platform.python_implementation(),
        "platform": platform.platform(),
        "packages": packages,
    }


def generation_scope(elections=None, parties=None, qualifiers=None, all_scopes=False):
    scope = {
        "all": bool(all_scopes),
        "elections": sorted(set(elections or [])),
        "parties": sorted(set(parties or [])),
        "qualifiers": dict(sorted((qualifiers or {}).items())),
    }
    _validate_scope(scope, "scope")
    return scope


# Standard generation-record construction and publication

def generation_record(
    category,
    stage,
    scope,
    run,
    dependencies,
    outputs,
    random_seed=None,
    status="generated",
):
    record = {
        "status": status,
        "category": category,
        "stage": stage,
        "scope": scope,
        "run": run,
        "random_seed": random_seed,
        "dependencies": dict(dependencies),
        "outputs": dict(outputs),
        "provenance_maintenance": [],
    }
    temporary_manifest = {
        "$schema": "generated_provenance.schema.json",
        "schema_version": SCHEMA_VERSION,
        "path_base": ".",
        "description": "Validation wrapper.",
        "updated_at_utc": utc_now(),
        "runs": {
            run: {
                "generated_at_utc": utc_now(),
                "command": ["validation"],
                "source_revision": {
                    "system": "git",
                    "revision": None,
                    "dirty": True,
                },
                "environment": current_environment(),
            }
        },
        "records": {"record": record},
    }
    validate_manifest(temporary_manifest)
    return record


def generation_run(command, source_revision, environment):
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_id = "{}-{}".format(timestamp, uuid.uuid4().hex[:8])
    return run_id, {
        "generated_at_utc": utc_now(),
        "command": list(command),
        "source_revision": dict(source_revision),
        "environment": dict(environment),
    }


def _schema_reference(manifest_path):
    resolved_schema = SCHEMA_PATH.resolve()
    manifest_folder = Path(manifest_path).resolve().parent
    try:
        relative_schema = os.path.relpath(
            str(resolved_schema), str(manifest_folder)
        )
    except ValueError:
        # A file URI is portable when Windows paths are on different drives.
        return resolved_schema.as_uri()
    return Path(relative_schema).as_posix()


def update_manifest(
    path,
    records,
    runs,
    path_base="..",
    description=None,
    expected_records=None,
    remove_record_keys=(),
):
    path = Path(path)
    _validate_manifest_path(path)
    remove_record_keys = sorted(set(remove_record_keys))
    with _ManifestWriteLock(path):
        if path.exists():
            manifest = load_manifest(path)
            if manifest["path_base"] != path_base:
                raise GeneratedProvenanceError(
                    "{} path_base is '{}', expected '{}'".format(
                        path, manifest["path_base"], path_base
                    )
                )
        else:
            if expected_records:
                raise ConcurrentManifestUpdate(
                    "{} was created during a record update".format(path)
                )
            if remove_record_keys:
                raise GeneratedProvenanceError(
                    "cannot remove records from a missing manifest {}".format(
                        path
                    )
                )
            if not description:
                raise GeneratedProvenanceError(
                    "description is required when creating a manifest"
                )
            manifest = {
                "$schema": _schema_reference(path),
                "schema_version": SCHEMA_VERSION,
                "path_base": path_base,
                "description": description,
                "updated_at_utc": utc_now(),
                "runs": {},
                "records": {},
            }
        for record_key, expected_record in (
            expected_records or {}
        ).items():
            if manifest["records"].get(record_key) != expected_record:
                raise ConcurrentManifestUpdate(
                    "{} record '{}' changed during update".format(
                        path, record_key
                    )
                )
        normalized_runs = {}
        for run_id, run in runs.items():
            normalized_run = dict(run)
            normalized_run["environment"] = dict(run["environment"])
            normalized_run["environment"].setdefault("packages", {})
            normalized_runs[run_id] = normalized_run
        manifest["runs"].update(normalized_runs)
        manifest["records"].update(records)
        for record_key in remove_record_keys:
            manifest["records"].pop(record_key, None)
        referenced_runs = {
            record["run"] for record in manifest["records"].values()
        }
        manifest["runs"] = {
            run_id: run
            for run_id, run in manifest["runs"].items()
            if run_id in referenced_runs
        }
        manifest["updated_at_utc"] = utc_now()
        validate_manifest(manifest)
        _atomic_write_json(path, manifest)
        return manifest


# Core generated-data freshness checking

class ManifestCheckContext:
    """Share filesystem and dependency checks across one audit operation."""

    def __init__(self):
        self.manifests = {}
        self.manifest_bases = {}
        self.record_issues = {}
        self.records_in_progress = set()
        self.source_cache = {}
        self.file_fingerprints = {}
        self.expected_output_fingerprints = {}
        self.output_owners = {}
        self.indexed_manifests = set()
        self.output_stats = {}
        self.resolved_paths = {}
        self.path_keys = {}
        self.generated_records_digests = {}
        self.record_digest_fragments = {}

    def resolve_path(self, path):
        """Return a stable absolute path without WSL resolve storms on POSIX."""

        path_key = str(Path(path))
        if path_key not in self.resolved_paths:
            # Match _canonical_path_key: Windows needs resolve() for 8.3
            # aliases; POSIX audits on mounted volumes must avoid traversal.
            if os.name == "nt":
                self.resolved_paths[path_key] = Path(path).resolve()
            else:
                self.resolved_paths[path_key] = Path(
                    os.path.abspath(str(path))
                )
        return self.resolved_paths[path_key]

    def path_key(self, path):
        unresolved_key = str(Path(path))
        if unresolved_key not in self.path_keys:
            self.path_keys[unresolved_key] = _canonical_path_key(path)
        return self.path_keys[unresolved_key]

    def _index_manifest_outputs(self, manifest_key, manifest):
        """Index owned outputs once so file-dep priming stays O(|wanted|)."""

        base_directory = self.manifest_bases[manifest_key]
        for record in manifest["records"].values():
            for output_path, fingerprint in record["outputs"].items():
                path_key = self.path_key(base_directory / output_path)
                existing = self.output_owners.get(path_key)
                if existing is None:
                    self.output_owners[path_key] = fingerprint
                elif existing != fingerprint:
                    # Conflicting owners cannot provide a safe fast path.
                    self.output_owners[path_key] = False
        self.indexed_manifests.add(manifest_key)

    def _ensure_outputs_indexed(self, manifest_key=None):
        """Index outputs lazily; selection loads manifests without this cost."""

        if manifest_key is not None:
            if manifest_key not in self.indexed_manifests:
                self._index_manifest_outputs(
                    manifest_key, self.manifests[manifest_key]
                )
            return
        for key, manifest in self.manifests.items():
            if key not in self.indexed_manifests:
                self._index_manifest_outputs(key, manifest)

    def load_manifest(self, path):
        resolved_path = str(self.resolve_path(path))
        manifest_key = os.path.normcase(resolved_path)
        if manifest_key not in self.manifests:
            manifest = load_manifest(resolved_path)
            self.manifests[manifest_key] = manifest
            self.manifest_bases[manifest_key] = self.resolve_path(
                Path(resolved_path).parent / manifest["path_base"]
            )
        return self.manifests[manifest_key]

    def _store_expected_output(self, path_key, fingerprint):
        existing = self.expected_output_fingerprints.get(path_key)
        if existing is None:
            self.expected_output_fingerprints[path_key] = fingerprint
        elif existing != fingerprint:
            # Conflicting records cannot provide a safe fast path.
            self.expected_output_fingerprints[path_key] = False

    def prime_expected_outputs(self, path, record_keys):
        """Prime output fingerprints for records that will be checked."""

        resolved_path = self.resolve_path(path)
        manifest_key = os.path.normcase(str(resolved_path))
        manifest = self.load_manifest(resolved_path)
        self._ensure_outputs_indexed(manifest_key)
        base_directory = self.manifest_bases[manifest_key]
        for record_key in record_keys:
            record = manifest["records"].get(record_key)
            if record is None:
                continue
            for output_path, fingerprint in record["outputs"].items():
                self._store_expected_output(
                    self.path_key(base_directory / output_path),
                    fingerprint,
                )

    def prime_expected_file_dependencies(self, path, record_keys):
        """Prime owners of file dependencies referenced by selected records."""

        resolved_path = self.resolve_path(path)
        manifest_key = os.path.normcase(str(resolved_path))
        manifest = self.load_manifest(resolved_path)
        self._ensure_outputs_indexed()
        base_directory = self.manifest_bases[manifest_key]
        wanted = set()
        for record_key in record_keys:
            record = manifest["records"].get(record_key)
            if record is None:
                continue
            for dependency in record["dependencies"].values():
                if dependency["kind"] != "files":
                    continue
                for relative_path in dependency["files"]:
                    wanted.add(
                        self.path_key(base_directory / relative_path)
                    )
        for path_key in wanted:
            fingerprint = self.output_owners.get(path_key)
            if fingerprint:
                self._store_expected_output(path_key, fingerprint)

    def generated_records_digest(self, manifest_path, manifest, record_keys):
        """Return a cached digest of selected generated records."""

        manifest_key = os.path.normcase(str(self.resolve_path(manifest_path)))
        cache_key = (manifest_key, frozenset(record_keys))
        if cache_key not in self.generated_records_digests:
            fragment_cache = self.record_digest_fragments.setdefault(
                manifest_key, {}
            )
            self.generated_records_digests[cache_key] = (
                _generated_records_digest(
                    manifest, record_keys, fragment_cache=fragment_cache
                )
            )
        return self.generated_records_digests[cache_key]

    @staticmethod
    def _stat_output(path):
        try:
            return path, Path(path).stat()
        except FileNotFoundError:
            return path, None

    def prime_output_stats(self, paths):
        """Stat selected outputs concurrently on high-latency filesystems."""

        pending_by_key = {
            self.path_key(path): Path(path)
            for path in paths
            if self.path_key(path) not in self.output_stats
        }
        pending = [
            pending_by_key[path_key]
            for path_key in sorted(pending_by_key)
        ]
        if not pending:
            return
        worker_count = min(32, len(pending))
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=worker_count
        ) as executor:
            for path, stat in executor.map(self._stat_output, pending):
                self.output_stats[self.path_key(path)] = stat

    def output_stat(self, path):
        path_key = self.path_key(path)
        if path_key not in self.output_stats:
            _, stat = self._stat_output(Path(path))
            self.output_stats[path_key] = stat
        return self.output_stats[path_key]

    def fingerprint_file(self, path):
        """Hash a file only when recorded output metadata cannot verify it."""

        path_key = self.path_key(path)
        if path_key in self.file_fingerprints:
            return self.file_fingerprints[path_key]
        stat = self.output_stat(path)
        expected = self.expected_output_fingerprints.get(path_key)
        if expected is None:
            # Loaded manifests index their outputs lazily; use that when
            # priming has not copied the owner fingerprint into the map.
            self._ensure_outputs_indexed()
            expected = self.output_owners.get(path_key)
        if (
            stat is not None
            and expected
            and expected.get("mtime_ns") == stat.st_mtime_ns
            and expected["size_bytes"] == stat.st_size
        ):
            fingerprint = expected
        else:
            fingerprint = fingerprint_file(Path(path))
        self.file_fingerprints[path_key] = fingerprint
        return fingerprint


def check_record(
    record,
    base_directory,
    source_cache=None,
    generated_cache=None,
    output_stat_cache=None,
    check_context=None,
):
    source_cache = (
        check_context.source_cache
        if check_context is not None
        else source_cache if source_cache is not None else {}
    )
    generated_cache = (
        generated_cache if generated_cache is not None else {}
    )
    use_output_stat_cache = output_stat_cache is not None
    output_stat_cache = output_stat_cache or {}
    issues = []
    if record["status"] == "legacy":
        issues.append(
            "legacy provenance baseline; generation inputs unknown"
        )
    for path, expected in record["outputs"].items():
        actual_path = Path(base_directory) / path
        if check_context is not None:
            stat = check_context.output_stat(actual_path)
        else:
            stat = (
                output_stat_cache.get(str(actual_path))
                if use_output_stat_cache
                else actual_path.stat() if actual_path.is_file() else None
            )
        if stat is None:
            issues.append("missing output {}".format(path))
            continue
        if stat.st_size != expected["size_bytes"]:
            issues.append("changed output {}".format(path))
            continue
        if expected.get("mtime_ns") == stat.st_mtime_ns:
            continue
        if fingerprint_file(actual_path)["sha256"] != expected["sha256"]:
            issues.append("changed output {}".format(path))

    target_scope = source_provenance._build_scope(
        all_scopes=record["scope"]["all"],
        elections=record["scope"]["elections"],
        parties=record["scope"]["parties"],
        stages=[] if record["scope"]["all"] else [record["stage"]],
    )
    for category_id, dependency in record["dependencies"].items():
        if dependency["kind"] == "files":
            dependency_files = [
                Path(base_directory) / path
                for path in dependency["files"]
            ]
            if check_context is None:
                current = file_dependency(
                    category_id,
                    dependency_files,
                    base_directory,
                )
            else:
                fingerprints = {
                    relative_path: check_context.fingerprint_file(
                        Path(base_directory) / relative_path
                    )
                    for relative_path in dependency["files"]
                }
                current = {
                    "digest": fingerprint_digest(fingerprints),
                }
            if current["digest"] != dependency["digest"]:
                issues.append("changed dependency {}".format(category_id))
            continue

        manifest_path = Path(base_directory) / dependency["manifest"]
        if dependency["kind"] == "generated_manifest":
            cache_key = str(
                check_context.resolve_path(manifest_path)
                if check_context is not None
                else manifest_path.resolve()
            )
            try:
                if check_context is not None:
                    generated_manifest = check_context.load_manifest(
                        manifest_path
                    )
                    existing_record_keys = [
                        record_key
                        for record_key in dependency["records"]
                        if record_key in generated_manifest["records"]
                    ]
                    checked_records = check_manifest(
                        manifest_path,
                        record_keys=existing_record_keys,
                        _context=check_context,
                    )
                elif cache_key not in generated_cache:
                    generated_cache[cache_key] = (
                        load_manifest(manifest_path),
                        check_manifest(manifest_path),
                    )
                if check_context is None:
                    generated_manifest, checked_records = generated_cache[
                        cache_key
                    ]
                non_invalidating_records = set(
                    dependency.get("non_invalidating_records", [])
                )
                tracked_records = [
                    record_key
                    for record_key in dependency["records"]
                    if record_key not in non_invalidating_records
                ]
                stale_records = [
                    record_key
                    for record_key in tracked_records
                    if record_key not in checked_records
                    or _data_affecting_issues(
                        checked_records[record_key]
                    )
                ]
                if stale_records:
                    issues.append(
                        "stale generated dependency {} ({})".format(
                            category_id, ", ".join(stale_records)
                        )
                    )
                if check_context is not None:
                    current_digest = check_context.generated_records_digest(
                        manifest_path,
                        generated_manifest,
                        tracked_records,
                    )
                else:
                    current_digest = _generated_records_digest(
                        generated_manifest, tracked_records
                    )
                if current_digest != dependency["digest"]:
                    issues.append(
                        "changed dependency {}".format(category_id)
                    )
            except GeneratedProvenanceError as error:
                issues.append(
                    "invalid dependency {}: {}".format(
                        category_id, error
                    )
                )
            continue

        cache_key = (
            str(
                check_context.resolve_path(manifest_path)
                if check_context is not None
                else manifest_path.resolve()
            ),
            category_id,
        )
        try:
            if cache_key not in source_cache:
                source_manifest = source_provenance.load_manifest(
                    manifest_path
                )
                category = source_manifest["categories"][category_id]
                current_files = source_provenance.snapshot_category(
                    manifest_path, source_manifest, category
                )
                comparison = source_provenance.compare_snapshots(
                    category["files"], current_files
                )
                source_cache[cache_key] = (category, comparison)
            category, comparison = source_cache[cache_key]
        except (source_provenance.ProvenanceError, KeyError) as error:
            issues.append(
                "invalid dependency {}: {}".format(category_id, error)
            )
            continue
        if any(
            comparison[field]
            for field in ("added", "removed", "modified")
        ):
            issues.append(
                "unrecorded source change {}".format(category_id)
            )
            continue
        try:
            events = source_provenance.semantic_events_affecting(
                category,
                dependency["semantic_revision"],
                target_scope,
            )
        except source_provenance.ProvenanceError as error:
            issues.append(
                "invalid dependency {}: {}".format(category_id, error)
            )
            continue
        if events:
            issues.append(
                "new semantic dependency revision {} ({})".format(
                    category_id,
                    ", ".join(
                        str(event["semantic_revision"]) for event in events
                    ),
                )
            )
            continue
        try:
            provenance_events = (
                source_provenance.provenance_events_affecting(
                    category,
                    dependency.get(
                        "provenance_revision",
                        dependency["semantic_revision"],
                    ),
                    target_scope,
                )
            )
        except source_provenance.ProvenanceError as error:
            issues.append(
                "invalid dependency {}: {}".format(category_id, error)
            )
            continue
        if provenance_events:
            issues.append(
                "provenance-only dependency revision {} ({})".format(
                    category_id,
                    ", ".join(
                        "{}:{}".format(
                            event["id"],
                            event["provenance_upgrade"],
                        )
                        for event in provenance_events
                    ),
                )
            )
    return issues


def check_manifest(path, record_keys=None, _context=None):
    """Check selected records, sharing repeated work across dependencies."""

    context = _context or ManifestCheckContext()
    resolved_path = context.resolve_path(path)
    resolved_path_key = str(resolved_path)
    manifest = context.load_manifest(resolved_path)
    base_directory = context.resolve_path(
        resolved_path.parent / manifest["path_base"]
    )
    if record_keys is None:
        selected_keys = list(manifest["records"])
    else:
        selected_keys = list(dict.fromkeys(record_keys))
        missing_keys = [
            record_key
            for record_key in selected_keys
            if record_key not in manifest["records"]
        ]
        if missing_keys:
            raise GeneratedProvenanceError(
                "{} has no generated record(s): {}".format(
                    resolved_path, ", ".join(missing_keys)
                )
            )

    # Recursive generated deps often re-enter with already-checked records.
    # Skip priming/statting when every selected key is already cached.
    if selected_keys and all(
        (resolved_path_key, record_key) in context.record_issues
        for record_key in selected_keys
    ):
        return {
            record_key: context.record_issues[
                (resolved_path_key, record_key)
            ]
            for record_key in selected_keys
        }

    context.prime_expected_outputs(resolved_path, selected_keys)
    context.prime_expected_file_dependencies(resolved_path, selected_keys)
    context.prime_output_stats(
        base_directory / output_path
        for record_key in selected_keys
        for output_path in manifest["records"][record_key]["outputs"]
    )
    checked_records = {}
    for record_key in selected_keys:
        cache_key = (resolved_path_key, record_key)
        if cache_key not in context.record_issues:
            if cache_key in context.records_in_progress:
                raise GeneratedProvenanceError(
                    "cyclic generated dependency at {}:{}".format(
                        resolved_path, record_key
                    )
                )
            context.records_in_progress.add(cache_key)
            try:
                context.record_issues[cache_key] = check_record(
                    manifest["records"][record_key],
                    base_directory,
                    check_context=context,
                )
            finally:
                context.records_in_progress.remove(cache_key)
        checked_records[record_key] = context.record_issues[cache_key]
    return checked_records


def build_parser():
    parser = argparse.ArgumentParser(
        description="Validate bundled generated-data provenance."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("manifests", nargs="+", type=Path)
    check_parser = subparsers.add_parser("check")
    check_parser.add_argument("manifests", nargs="+", type=Path)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        if args.command == "validate":
            for path in args.manifests:
                load_manifest(path)
                print("{}: valid".format(path))
            return 0
        if args.command == "check":
            has_issues = False
            for path in args.manifests:
                print("{}:".format(path))
                manifest = load_manifest(path)
                results = check_manifest(path)
                legacy_issue = (
                    "legacy provenance baseline; generation inputs unknown"
                )
                legacy_count = 0
                for record_key, issues in results.items():
                    display_issues = list(issues)
                    if (
                        manifest["records"][record_key]["status"] == "legacy"
                        and legacy_issue in display_issues
                    ):
                        legacy_count += 1
                        display_issues.remove(legacy_issue)
                    if display_issues:
                        has_issues = True
                        print("  {}: STALE OR INVALID".format(record_key))
                        for issue in display_issues:
                            print("    {}".format(issue))
                if legacy_count:
                    has_issues = True
                    print(
                        "  {} legacy record(s) have unknown generation "
                        "inputs.".format(legacy_count)
                    )
                    print(
                        "  Missing historical seed metadata is "
                        "informational only."
                    )
                current_count = sum(
                    not issues for issues in results.values()
                )
                if current_count:
                    print(
                        "  {} record(s) current.".format(current_count)
                    )
            return 2 if has_issues else 0
    except GeneratedProvenanceError as error:
        print("Error: {}".format(error), file=sys.stderr)
        return 2
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())

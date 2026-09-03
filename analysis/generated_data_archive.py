"""Build and restore the validated generated-data archive used by new clones.

Parent flow: ``pipeline.py`` interactive archive actions.  The archive mirrors
generated/cache data only; authored files in mixed directories remain local.

Main functions:
* ``preflight_build`` requires a complete current provenance audit and excludes
  transient staging files from the archive.
* ``_managed_relative_paths`` selects permanent generated output paths while
  excluding large compatibility calibration traces.
* ``validate_archive`` verifies archive manifest structure and every payload
  fingerprint before restore.
* ``build_archive`` stages, validates and promotes a replacement archive.
* ``restore_archive`` stages and validates a saved archive before replacing
  only generated directory content in the working tree.
"""

import copy
import errno
import hashlib
import json
import os
import shutil
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

import generated_provenance
import analysis_provenance


ARCHIVE_MANIFEST_NAME = "generated-data-archive.json"
ARCHIVE_SCHEMA_VERSION = 1
FULL_ROOTS = (
    "Outputs",
    "Adjustments",
    "Fundamentals",
    "Seat Statistics",
    "Nationals",
    "elections",
    "Synthetic TPPs",
    "Booth Results",
)
REQUIRED_FULL_ROOTS = (
    "Outputs",
    "Adjustments",
    "Fundamentals",
    "Seat Statistics",
    "Nationals",
    "elections",
)
PARTIAL_ROOTS = ("Regional", "Federal-State")
ALLOWED_ROOTS = set(FULL_ROOTS) | set(PARTIAL_ROOTS)
EXCLUDED_OUTPUT_PREFIXES = (
    "Outputs/Calibration/Diagnostics/",
    "Outputs/Calibration/Staging/",
    "Outputs/Calibration/Checkpoints/",
)


# Archive payload selection and preflight validation

class GeneratedDataArchiveError(ValueError):
    """Raised when an archive cannot safely be built or restored."""


def _utc_now():
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def _hash_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as input_file:
        for block in iter(lambda: input_file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _fingerprint(path):
    path = Path(path)
    if not path.is_file():
        raise GeneratedDataArchiveError("missing archive file: {}".format(path))
    return {"sha256": _hash_file(path), "size_bytes": path.stat().st_size}


def _safe_relative_path(value):
    if not isinstance(value, str) or not value or "\\" in value:
        raise GeneratedDataArchiveError("archive path is invalid: {!r}".format(value))
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or len(path.parts) < 2:
        raise GeneratedDataArchiveError("archive path is unsafe: {}".format(value))
    if path.parts[0] not in ALLOWED_ROOTS:
        raise GeneratedDataArchiveError(
            "archive path has unsupported root: {}".format(value)
        )
    return path


def _is_regular_file(path):
    path = Path(path)
    return path.is_file() and not path.is_symlink()


def _is_excluded_output(relative_path):
    if relative_path.endswith(".zip") or relative_path.startswith(
        EXCLUDED_OUTPUT_PREFIXES
    ):
        return True
    path = PurePosixPath(relative_path)
    if (
        path.parts[:2] == ("Outputs", "Cutoffs")
        and ".in-progress" in path.name
    ):
        return True
    if path.parts[:2] != ("Outputs", "Calibration"):
        return False
    # Compact summaries are the permanent calibration archive format. The
    # detailed compatibility traces remain readable locally but are too large
    # and are not needed after pollster_analysis switched to the summary loader.
    return path.name.startswith(
        ("calib_", "fp_trend_", "fp_polls_", "fp_house_effects_")
    )


def _is_partial_generated_path(relative_path):
    path = PurePosixPath(relative_path)
    name = path.name
    if path.parts[0] == "Regional":
        return (
            name.endswith("-swing-deviations.csv")
            or name.endswith("-swing-deviations-ON.csv")
            or name.endswith("-swing-deviations-on.csv")
            or name.endswith("fed-mix-parameters.csv")
            or name.endswith("fed-mix-regions.csv")
            or name.endswith("fed-regions-polled.csv")
            or name.endswith("fed-regions-base.csv")
            or name.endswith("generated-provenance.json")
        )
    return path.parts[0] == "Federal-State" and name.endswith(".pkl")


def _managed_relative_paths(analysis_directory):
    """Return archive-relative generated/cache files in deterministic order."""

    analysis_directory = Path(analysis_directory).resolve()
    paths = []
    for root in FULL_ROOTS:
        root_path = analysis_directory / root
        if not root_path.is_dir():
            continue
        for path in root_path.rglob("*"):
            if not _is_regular_file(path):
                continue
            relative = path.relative_to(analysis_directory).as_posix()
            if not _is_excluded_output(relative):
                paths.append(relative)
    for root in PARTIAL_ROOTS:
        root_path = analysis_directory / root
        if not root_path.is_dir():
            continue
        for path in root_path.rglob("*"):
            if not _is_regular_file(path):
                continue
            relative = path.relative_to(analysis_directory).as_posix()
            if _is_partial_generated_path(relative):
                paths.append(relative)
    return sorted(paths)


def _archive_eligible_path(relative_path):
    """Return whether a generated record output belongs in the archive."""

    path = PurePosixPath(relative_path)
    if path.parts[0] in FULL_ROOTS:
        return not _is_excluded_output(relative_path)
    if path.parts[0] in PARTIAL_ROOTS:
        return _is_partial_generated_path(relative_path)
    return False


def _required_archive_payload(analysis_directory, audit):
    """Select required output files and matching filtered manifests."""

    required_graph = audit.get("required_graph")
    if not required_graph:
        return _managed_relative_paths(analysis_directory), {}

    required_ids = set(required_graph.get("required_work_unit_ids", []))
    selected_records = {}
    for work_unit in audit.get("work_units", []):
        if work_unit.get("id") not in required_ids:
            continue
        selected_records.setdefault(work_unit["manifest"], set()).add(
            work_unit["record_key"]
        )

    managed_files = set()
    manifest_overrides = {}
    analysis_directory = Path(analysis_directory).resolve()
    for manifest_label, record_keys in selected_records.items():
        manifest_path = analysis_directory / manifest_label
        if not manifest_path.is_file():
            # Missing required records are rejected as non-current before
            # payload construction. A missing containing manifest is a
            # structural error and should still fail explicitly here.
            raise GeneratedDataArchiveError(
                "required generated manifest is missing: {}".format(
                    manifest_label
                )
            )
        try:
            manifest = generated_provenance.load_manifest(manifest_path)
        except generated_provenance.GeneratedProvenanceError as error:
            raise GeneratedDataArchiveError(str(error)) from error
        missing_records = sorted(record_keys - set(manifest["records"]))
        if missing_records:
            raise GeneratedDataArchiveError(
                "required generated manifest {} lacks record(s): {}".format(
                    manifest_label, ", ".join(missing_records)
                )
            )

        filtered = copy.deepcopy(manifest)
        filtered["records"] = {
            record_key: manifest["records"][record_key]
            for record_key in sorted(record_keys)
        }
        run_ids = {record["run"] for record in filtered["records"].values()}
        filtered["runs"] = {
            run_id: manifest["runs"][run_id]
            for run_id in sorted(run_ids)
        }
        manifest_overrides[manifest_label] = filtered
        managed_files.add(manifest_label)

        base_directory = (manifest_path.parent / manifest["path_base"]).resolve()
        for record_key, record in filtered["records"].items():
            for output_path in record["outputs"]:
                absolute_output = (base_directory / output_path).resolve()
                try:
                    relative_output = absolute_output.relative_to(
                        analysis_directory
                    ).as_posix()
                except ValueError as error:
                    raise GeneratedDataArchiveError(
                        "required output for {} is outside analysis/: {}"
                        .format(record_key, output_path)
                    ) from error
                if not _archive_eligible_path(relative_output):
                    raise GeneratedDataArchiveError(
                        "required record {} owns non-archivable output {}"
                        .format(record_key, relative_output)
                    )
                managed_files.add(relative_output)

    return sorted(managed_files), manifest_overrides


def _require_complete_roots(analysis_directory):
    missing = []
    for root in REQUIRED_FULL_ROOTS:
        root_path = Path(analysis_directory) / root
        if not root_path.is_dir() or not any(
            _is_regular_file(path) for path in root_path.rglob("*")
        ):
            missing.append(root)
    if missing:
        raise GeneratedDataArchiveError(
            "cannot build archive; required generated roots are empty or "
            "missing: {}".format(", ".join(missing))
        )


def preflight_build(analysis_directory, audit_runner=analysis_provenance.audit_repository):
    """Require the operationally required graph to be current."""

    analysis_directory = Path(analysis_directory).resolve()
    audit = audit_runner()
    blockers = bool(audit.get("internal_errors"))
    source_issues = audit.get("source_issues", [])
    manifest_issues = audit.get("manifest_issues", [])
    required_graph = audit.get("required_graph") or {}
    required_ids = set(required_graph.get("required_work_unit_ids", []))
    noncurrent = [
        work_unit
        for work_unit in audit.get("work_units", [])
        if work_unit.get("status") != "current"
        and (
            not required_graph
            or work_unit.get("id") in required_ids
        )
    ]
    if blockers or source_issues or manifest_issues or noncurrent:
        examples = [
            "{}:{} ({})".format(
                work_unit.get("stage", "unknown"),
                work_unit.get("record_key", "unknown"),
                work_unit.get("status", "unknown"),
            )
            for work_unit in noncurrent[:10]
        ]
        details = []
        if blockers:
            details.append("provenance blockers are present")
        if source_issues:
            details.append("source provenance issues are present")
        if manifest_issues:
            details.append("generated-manifest issues are present")
        if examples:
            details.append("non-current work: {}".format(", ".join(examples)))
        raise GeneratedDataArchiveError(
            "cannot build archive until the required generated graph is current; {}"
            .format("; ".join(details))
        )
    _require_complete_roots(analysis_directory)
    managed_files, manifest_overrides = _required_archive_payload(
        analysis_directory, audit
    )
    return {
        "managed_files": managed_files,
        "manifest_overrides": manifest_overrides,
        "work_units": len(required_ids) if required_graph else len(
            audit.get("work_units", [])
        ),
    }


# Archive-manifest construction and validation

def _archive_manifest(files):
    return {
        "schema_version": ARCHIVE_SCHEMA_VERSION,
        "generated_at_utc": _utc_now(),
        "description": (
            "Validated generated/cache analysis data. Authored analysis inputs "
            "are deliberately excluded."
        ),
        "full_roots": sorted(
            {
                PurePosixPath(entry["path"]).parts[0]
                for entry in files
                if PurePosixPath(entry["path"]).parts[0] in FULL_ROOTS
            }
        ),
        "partial_roots": sorted(
            {
                PurePosixPath(entry["path"]).parts[0]
                for entry in files
                if PurePosixPath(entry["path"]).parts[0] in PARTIAL_ROOTS
            }
        ),
        "files": files,
    }


def _write_manifest(directory, manifest):
    path = Path(directory) / ARCHIVE_MANIFEST_NAME
    with path.open("w", encoding="utf-8", newline="\n") as output:
        json.dump(manifest, output, indent=2, sort_keys=True)
        output.write("\n")


def _load_archive_manifest(archive_directory):
    path = Path(archive_directory) / ARCHIVE_MANIFEST_NAME
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise GeneratedDataArchiveError(
            "archive has no {}".format(ARCHIVE_MANIFEST_NAME)
        ) from error
    except json.JSONDecodeError as error:
        raise GeneratedDataArchiveError(
            "could not parse archive manifest: {}".format(error)
        ) from error
    required = {
        "schema_version",
        "generated_at_utc",
        "description",
        "full_roots",
        "partial_roots",
        "files",
    }
    if set(manifest) != required or manifest["schema_version"] != ARCHIVE_SCHEMA_VERSION:
        raise GeneratedDataArchiveError("archive manifest has an unsupported format")
    if not isinstance(manifest["files"], list) or not manifest["files"]:
        raise GeneratedDataArchiveError("archive manifest has no files")
    if (
        not isinstance(manifest["full_roots"], list)
        or len(manifest["full_roots"]) != len(set(manifest["full_roots"]))
        or any(root not in FULL_ROOTS for root in manifest["full_roots"])
    ):
        raise GeneratedDataArchiveError("archive manifest has invalid full roots")
    if (
        not isinstance(manifest["partial_roots"], list)
        or len(manifest["partial_roots"]) != len(set(manifest["partial_roots"]))
        or any(root not in PARTIAL_ROOTS for root in manifest["partial_roots"])
    ):
        raise GeneratedDataArchiveError("archive manifest has invalid partial roots")
    return manifest


def validate_archive(archive_directory):
    """Validate archive shape and every payload file before restoration."""

    archive_directory = Path(archive_directory).resolve()
    manifest = _load_archive_manifest(archive_directory)
    paths = set()
    payload_roots = set()
    for entry in manifest["files"]:
        if set(entry) != {"path", "sha256", "size_bytes"}:
            raise GeneratedDataArchiveError("archive manifest file entry is invalid")
        relative = _safe_relative_path(entry["path"])
        if entry["path"] in paths:
            raise GeneratedDataArchiveError(
                "archive manifest repeats {}".format(entry["path"])
            )
        paths.add(entry["path"])
        payload_roots.add(relative.parts[0])
        if (
            not isinstance(entry["sha256"], str)
            or len(entry["sha256"]) != 64
            or any(character not in "0123456789abcdef" for character in entry["sha256"])
            or not isinstance(entry["size_bytes"], int)
            or entry["size_bytes"] < 0
        ):
            raise GeneratedDataArchiveError(
                "archive fingerprint is invalid for {}".format(entry["path"])
            )
        payload = archive_directory.joinpath(*relative.parts)
        if not _is_regular_file(payload):
            raise GeneratedDataArchiveError(
                "archive payload is missing or not a regular file: {}".format(
                    entry["path"]
                )
            )
        if _fingerprint(payload) != {
            "sha256": entry["sha256"],
            "size_bytes": entry["size_bytes"],
        }:
            raise GeneratedDataArchiveError(
                "archive payload fingerprint does not match: {}".format(
                    entry["path"]
                )
            )
    declared_roots = set(manifest["full_roots"]) | set(
        manifest["partial_roots"]
    )
    if payload_roots != declared_roots:
        raise GeneratedDataArchiveError(
            "archive manifest roots do not match its payload"
        )
    return manifest


def _temporary_sibling(path, purpose):
    return Path(path).with_name(
        ".{}-{}-{}".format(Path(path).name, purpose, uuid.uuid4().hex)
    )


def _remove_tree(path):
    path = Path(path)
    if path.exists():
        shutil.rmtree(path)


def _replace_directory(source, destination):
    """Rename a directory, tolerating short-lived Windows/WSL file locks."""

    retryable_errors = {errno.EACCES, errno.EBUSY, errno.EPERM}
    attempts = 20
    for attempt in range(attempts):
        try:
            os.replace(source, destination)
            return
        except OSError as error:
            if error.errno not in retryable_errors or attempt == attempts - 1:
                raise
            if attempt == 0:
                print(
                    "Directory promotion is temporarily blocked; retrying for "
                    "up to 5 seconds...",
                    flush=True,
                )
            time.sleep(0.25)


def _promote_directory(staging_directory, destination_directory):
    """Replace one directory via rename, restoring its prior version on error."""

    staging_directory = Path(staging_directory)
    destination_directory = Path(destination_directory)
    backup = _temporary_sibling(destination_directory, "archive-backup")
    moved_existing = False
    try:
        if destination_directory.exists():
            _replace_directory(destination_directory, backup)
            moved_existing = True
        _replace_directory(staging_directory, destination_directory)
    except OSError as error:
        if moved_existing and backup.exists() and not destination_directory.exists():
            _replace_directory(backup, destination_directory)
        raise GeneratedDataArchiveError(
            "could not promote {}: {}".format(destination_directory, error)
        ) from error
    else:
        _remove_tree(backup)


# Staged archive build and restore

def build_archive(analysis_directory, archive_directory=None, audit_runner=analysis_provenance.audit_repository):
    """Build, validate and atomically replace the archive after strict preflight."""

    analysis_directory = Path(analysis_directory).resolve()
    archive_directory = Path(
        archive_directory or analysis_directory / "Archived"
    ).resolve()
    if archive_directory.parent != analysis_directory:
        raise GeneratedDataArchiveError("archive directory must be below analysis/")
    preflight = preflight_build(analysis_directory, audit_runner=audit_runner)
    staging = _temporary_sibling(archive_directory, "archive-build")
    try:
        files = []
        for relative in preflight["managed_files"]:
            source = analysis_directory / relative
            destination = staging / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if relative in preflight["manifest_overrides"]:
                with destination.open(
                    "w", encoding="utf-8", newline="\n"
                ) as output:
                    json.dump(
                        preflight["manifest_overrides"][relative],
                        output,
                        indent=2,
                    )
                    output.write("\n")
            else:
                shutil.copy2(source, destination)
            fingerprint = _fingerprint(destination)
            files.append({"path": relative, **fingerprint})
        _write_manifest(staging, _archive_manifest(files))
        manifest = validate_archive(staging)
        _promote_directory(staging, archive_directory)
        return {
            "archive_directory": archive_directory,
            "files": len(files),
            "manifest": manifest,
        }
    finally:
        _remove_tree(staging)


def _prepare_partial_root(analysis_directory, staging_directory, root):
    """Preserve authored files while replacing only archived generated files."""

    source = Path(analysis_directory) / root
    destination = Path(staging_directory) / root
    if source.is_dir():
        shutil.copytree(source, destination, symlinks=False)
    else:
        destination.mkdir(parents=True)
    for path in sorted(destination.rglob("*"), reverse=True):
        relative = path.relative_to(staging_directory).as_posix()
        if path.is_file() and _is_partial_generated_path(relative):
            path.unlink()
        elif path.is_dir() and not any(path.iterdir()):
            path.rmdir()


def _copy_archive_files(archive_directory, staging_directory, entries, root):
    for entry in entries:
        relative = PurePosixPath(entry["path"])
        if relative.parts[0] != root:
            continue
        source = Path(archive_directory).joinpath(*relative.parts)
        destination = Path(staging_directory).joinpath(*relative.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def _promote_roots(staging_directory, analysis_directory, roots):
    """Promote staged roots, rolling back already-promoted roots on failure."""

    staging_directory = Path(staging_directory)
    analysis_directory = Path(analysis_directory)
    backups = {}
    promoted = []
    try:
        for root in roots:
            staged_root = staging_directory / root
            destination = analysis_directory / root
            backup = _temporary_sibling(destination, "restore-backup")
            if destination.exists():
                _replace_directory(destination, backup)
                backups[root] = backup
            _replace_directory(staged_root, destination)
            promoted.append(root)
    except OSError as error:
        for root in reversed(promoted):
            destination = analysis_directory / root
            failed_new = staging_directory / root
            if destination.exists():
                _replace_directory(destination, failed_new)
            if root in backups and backups[root].exists():
                _replace_directory(backups[root], destination)
        for root, backup in backups.items():
            destination = analysis_directory / root
            if root not in promoted and backup.exists() and not destination.exists():
                _replace_directory(backup, destination)
        raise GeneratedDataArchiveError(
            "could not promote restored generated data: {}".format(error)
        ) from error
    else:
        for backup in backups.values():
            _remove_tree(backup)


def restore_archive(analysis_directory, archive_directory=None):
    """Validate then restore an archive without replacing authored inputs."""

    analysis_directory = Path(analysis_directory).resolve()
    archive_directory = Path(
        archive_directory or analysis_directory / "Archived"
    ).resolve()
    manifest = validate_archive(archive_directory)
    staging = _temporary_sibling(analysis_directory / "restore", "archive")
    try:
        full_roots = manifest["full_roots"]
        partial_roots = manifest["partial_roots"]
        for root in full_roots:
            _copy_archive_files(
                archive_directory, staging, manifest["files"], root
            )
        for root in partial_roots:
            _prepare_partial_root(analysis_directory, staging, root)
            _copy_archive_files(
                archive_directory, staging, manifest["files"], root
            )
        _promote_roots(staging, analysis_directory, full_roots + partial_roots)
        return {
            "archive_directory": archive_directory,
            "files": len(manifest["files"]),
            "roots": full_roots + partial_roots,
        }
    finally:
        _remove_tree(staging)

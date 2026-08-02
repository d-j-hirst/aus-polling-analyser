"""Small, atomic restart checkpoints for long fp-model calibration batches."""

import hashlib
import json
import math
import os
import shutil
import tempfile
from datetime import date
from pathlib import Path


SCHEMA_VERSION = 1
DEFAULT_DIRECTORY = Path("./Outputs/Calibration/Checkpoints")


def fingerprint_files(paths):
    """Return a deterministic digest of the named files and their contents."""

    digest = hashlib.sha256()
    for path in sorted({str(Path(path)) for path in paths}):
        file_path = Path(path)
        digest.update(path.replace("\\", "/").encode("utf-8"))
        digest.update(b"\0")
        if not file_path.is_file():
            digest.update(b"<missing>")
            continue
        with file_path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        digest.update(b"\0")
    return digest.hexdigest()


def _checkpoint_name(excluded_pollster):
    if not excluded_pollster:
        return "full.json"
    digest = hashlib.sha256(
        excluded_pollster.encode("utf-8")
    ).hexdigest()[:16]
    return "excluded-{}.json".format(digest)


def _write_json_atomically(path, payload):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=".{}-".format(path.stem),
        suffix=".tmp",
        text=True,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(
                payload,
                output,
                allow_nan=False,
                indent=2,
                sort_keys=True,
            )
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


class CalibrationCheckpointStore:
    """Persist completed excluded-pollster blocks without publishing results."""

    def __init__(self, directory=DEFAULT_DIRECTORY):
        self.directory = Path(directory)

    def path(self, election, excluded_pollster):
        return (
            self.directory
            / election
            / _checkpoint_name(excluded_pollster)
        )

    def load(self, identity):
        """Return checkpoint payload when its complete identity still matches."""

        path = self.path(
            identity["election"], identity["excluded_pollster"]
        )
        if not path.is_file():
            return None
        try:
            with path.open(encoding="utf-8") as source:
                payload = json.load(source)
            if payload.get("schema_version") != SCHEMA_VERSION:
                return None
            if payload.get("identity") != identity:
                return None
            if not isinstance(payload.get("poll_calibrations"), list):
                return None
            if not isinstance(payload.get("federal_priors"), dict):
                return None
            if not isinstance(payload.get("stan_seeds"), list):
                return None
            parties = set(identity["parties"])
            for record in payload["poll_calibrations"]:
                if not isinstance(record, dict):
                    return None
                values = record.get("values")
                if (
                    record.get("party") not in parties
                    or not isinstance(record.get("day_index"), int)
                    or record["day_index"] < 0
                    or not isinstance(record.get("poll_index"), int)
                    or record["poll_index"] < 0
                    or not isinstance(values, list)
                    or len(values) != 7
                    or any(
                        value is not None
                        and (
                            not isinstance(value, (int, float))
                            or not math.isfinite(value)
                        )
                        for value in values
                    )
                ):
                    return None
            for party, values in payload["federal_priors"].items():
                if (
                    party not in parties
                    or not isinstance(values, list)
                    or any(
                        not isinstance(value, list)
                        or len(value) != 2
                        or not isinstance(value[0], str)
                        or not isinstance(value[1], (int, float))
                        or not math.isfinite(value[1])
                        for value in values
                    )
                ):
                    return None
                for value in values:
                    date.fromisoformat(value[0])
            for record in payload["stan_seeds"]:
                if (
                    not isinstance(record, dict)
                    or record.get("party") not in parties
                    or not isinstance(record.get("seed"), int)
                    or not 1 <= record["seed"] < 2 ** 31
                ):
                    return None
        except (OSError, TypeError, ValueError):
            return None
        return payload

    def write(
        self,
        identity,
        poll_calibrations,
        federal_priors,
        stan_seeds=None,
    ):
        path = self.path(
            identity["election"], identity["excluded_pollster"]
        )
        _write_json_atomically(
            path,
            {
                "schema_version": SCHEMA_VERSION,
                "identity": identity,
                "poll_calibrations": poll_calibrations,
                "federal_priors": federal_priors,
                "stan_seeds": list(stan_seeds or []),
            },
        )
        return path

    def clear_election(self, election):
        directory = self.directory / election
        if directory.is_dir():
            shutil.rmtree(directory)

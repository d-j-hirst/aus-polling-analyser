"""Record provenance for pure, final and point-in-time poll-trend outputs.

Pure and final work units contain the trend, adjusted-poll and house-effect
files for one election and party. Pure trends feed the synthetic-TPP analysis;
final trends combine polling, synthetic TPP observations and any applicable
federal minor-party prior for direct use by downstream forecast stages.
Point-in-time fits are bundled into one file per election. Each cutoff run
starts that election's file afresh, then writes completed endpoints
incrementally during the run.

Pollster parameters may retain deliberately stale calibration ancestry. The
generated dependency preserves that ancestry while allowing routine trend
generation to continue.

Main functions:
* ``cutoff_schedule`` and ``effective_cutoff_schedule`` select point-in-time
  model endpoints from configured triangular days and actual poll arrivals.
* ``CutoffOutputStore`` stages and incrementally records one consolidated
  cutoff file while a long-running election completes. Resume identity uses
  schedule/parties/seeds, stable provenance dependencies and a local
  ``source_fingerprint``; accumulating federal cutoff parents are tracked
  separately and validated by content digest.
* ``PureTrendRecorder``, ``FinalTrendRecorder`` and ``CutoffTrendRecorder``
  publish completed fp_model work-unit provenance.
* ``baseline_existing_*`` records older output files as legacy compatibility
  data without falsely marking them current.
"""

import argparse
import bisect
import csv
import json
import math
import os
import re
import sys
import tempfile
from datetime import date, timedelta
from pathlib import Path

import approvals_provenance
import fp_model_checkpoints
import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / "Outputs"
CUTOFF_OUTPUT_DIRECTORY = OUTPUT_DIRECTORY / "Cutoffs"
MANIFEST_PATH = OUTPUT_DIRECTORY / "pure-generated-provenance.json"
FINAL_MANIFEST_PATH = (
    OUTPUT_DIRECTORY / "poll-trend-generated-provenance.json"
)
CUTOFF_MANIFEST_PATH = (
    OUTPUT_DIRECTORY / "cutoff-generated-provenance.json"
)
POLLSTER_MANIFEST_PATH = (
    OUTPUT_DIRECTORY
    / "Calibration"
    / "pollster-generated-provenance.json"
)
MANIFEST_DESCRIPTION = (
    "Bundled provenance for voting-intention-only poll trends, adjusted "
    "polls and house effects."
)
FINAL_MANIFEST_DESCRIPTION = (
    "Bundled provenance for final poll trends, adjusted polls and house "
    "effects consumed by forecasts."
)
CUTOFF_MANIFEST_DESCRIPTION = (
    "Bundled provenance for consolidated point-in-time historical poll "
    "trend distributions."
)
SOURCE_DEPENDENCIES = {
    "election_catalogue": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "raw_poll_data": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "poll_model_configuration":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "preference_estimates":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "prior_result_inputs":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "fp_model_script": ANALYSIS_DIRECTORY / "provenance.json",
    "fp_model_provenance_script": ANALYSIS_DIRECTORY / "provenance.json",
    "calibration_provenance_script":
        ANALYSIS_DIRECTORY / "provenance.json",
    "stan_cache_script": ANALYSIS_DIRECTORY / "provenance.json",
    "election_code_script": ANALYSIS_DIRECTORY / "provenance.json",
    "fp_stan_model": ANALYSIS_DIRECTORY / "Models" / "provenance.json",
}
OUTPUT_PATTERN = re.compile(
    r"^fp_(trend|polls|house_effects)_(\d{4}[a-z]+)_(.+)_pure\.csv$"
)
FINAL_OUTPUT_PATTERN = re.compile(
    r"^fp_(trend|polls|house_effects)_(\d{4}[a-z]+)_(.+)\.csv$"
)
OUTPUT_KINDS = {"trend", "polls", "house_effects"}
APPROVAL_PARTIES = {"@TPP", "ALP FP", "LNP FP", "LIB FP"}
FEDERAL_PRIOR_PARTIES = {
    "ONP FP",
    "UAP FP",
    "SFF FP",
    "CA FP",
    "KAP FP",
    "SAB FP",
    "DEM FP",
    "FF FP",
    "DLP FP",
    "GRN FP",
    "OTH FP",
}
ELECTION_CODE_PATTERN = re.compile(r"^(\d{4})([a-z]+)$")
CUTOFF_DAY_TEST_COUNT = 46
# Registered only for scoped metadata maintenance after an upstream generated
# record has had a dependency-only correction. It never changes trend values.
DIRECT_GENERATED_DEPENDENCY_REFRESH_UPGRADE = (
    "refresh-direct-generated-dependencies-v1"
)


# Cutoff schedule selection and consolidated-file staging

def cutoff_schedule():
    """Return the triangular day schedule also used by trend_adjust.py."""

    return [
        n * (n + 1) // 2
        for n in range(CUTOFF_DAY_TEST_COUNT)
    ]


def effective_cutoff_schedule(election_day, poll_dates, schedule=None):
    """Map scheduled cutoffs to distinct poll-trend endpoints.

    A scheduled day may fall between polls. The fit then ends at the latest
    poll available by that day, while retaining the scheduled day for context.
    Consecutive scheduled points with the same poll set require only one fit.
    """

    schedule = cutoff_schedule() if schedule is None else list(schedule)
    poll_dates = sorted(set(poll_dates))
    effective_cutoffs = []
    seen_poll_endpoints = set()
    for scheduled_days in sorted(set(schedule), reverse=True):
        latest_allowed_date = (
            election_day - timedelta(days=scheduled_days)
        )
        poll_index = bisect.bisect_right(
            poll_dates, latest_allowed_date
        ) - 1
        if poll_index < 0:
            continue
        poll_end_date = poll_dates[poll_index]
        poll_trend_end_days = (election_day - poll_end_date).days
        if poll_trend_end_days in seen_poll_endpoints:
            continue
        seen_poll_endpoints.add(poll_trend_end_days)
        effective_cutoffs.append(
            (scheduled_days, poll_trend_end_days)
        )
    return effective_cutoffs


def cutoff_output_path(election):
    return CUTOFF_OUTPUT_DIRECTORY / "cutoffs_{}.csv".format(election)


def cutoff_working_path(election):
    path = cutoff_output_path(election)
    return path.with_suffix(path.suffix + ".in-progress")


def cutoff_working_metadata_path(election):
    path = cutoff_working_path(election)
    return path.with_suffix(path.suffix + ".json")


def _write_json_atomically(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=".{}-".format(path.stem),
        suffix=".tmp",
        text=True,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(payload, output, indent=2, sort_keys=True)
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


_CUTOFF_RESUME_EXCLUDED_KEYS = frozenset({
    "endpoint_parties",
    "federal_prior_files",
    "federal_prior_digest",
})


def _federal_prior_digest(files):
    files = sorted({str(Path(path)) for path in files})
    if not files:
        return ""
    return fp_model_checkpoints.fingerprint_files(files)


def _cutoff_resume_identity(metadata):
    """Stable identity for whether a cutoff draft may resume.

    Federal prior files accumulate as later endpoints are prepared, so their
    paths and digests are tracked separately and validated against current
    file contents rather than being part of the equality key. Provenance
    ``dependencies`` are compared without the evolving ``cutoff_poll_outputs``
    entry. ``source_fingerprint`` captures local code/Stan contents so
    uncommitted edits invalidate resume even before manifests are re-recorded.
    """

    identity = {
        key: value for key, value in metadata.items()
        if key not in _CUTOFF_RESUME_EXCLUDED_KEYS
    }
    dependencies = identity.get("dependencies")
    if isinstance(dependencies, dict):
        identity["dependencies"] = {
            key: value for key, value in dependencies.items()
            if key != "cutoff_poll_outputs"
        }
    return identity


def _normalize_cutoff_metadata(metadata):
    files = sorted({
        str(Path(path))
        for path in metadata.get("federal_prior_files") or []
    })
    metadata = dict(metadata)
    metadata["federal_prior_files"] = files
    metadata["federal_prior_digest"] = _federal_prior_digest(files)
    # Normalize tuples and other JSON-compatible containers so equality
    # remains stable after a cross-process round trip.
    return json.loads(json.dumps(metadata, sort_keys=True))


class CutoffOutputStore:
    """Build one election in a draft before atomically promoting it."""

    KEY_COLUMNS = (
        "ScheduledCutoffDays",
        "PollTrendEndDays",
        "Party",
        "StanSeed",
    )
    COMPLETE_MARKER = "#COMPLETE"

    def __init__(self):
        self._headers = {}
        self._rows = {}
        self._metadata = {}

    def begin(self, election, metadata):
        """Resume a matching draft or start a new isolated cutoff batch."""

        # Normalize tuples and other JSON-compatible containers so equality
        # remains stable after a cross-process round trip.
        metadata = _normalize_cutoff_metadata(metadata)
        working_path = cutoff_working_path(election)
        metadata_path = cutoff_working_metadata_path(election)
        if working_path.is_file() and metadata_path.is_file():
            try:
                with metadata_path.open(encoding="utf-8") as source:
                    existing = json.load(source)
            except (OSError, TypeError, ValueError):
                existing = None
            if (
                isinstance(existing, dict)
                and _cutoff_resume_identity(existing)
                == _cutoff_resume_identity(metadata)
                and (
                    existing.get("federal_prior_digest", "")
                    == _federal_prior_digest(
                        existing.get("federal_prior_files") or []
                    )
                )
            ):
                try:
                    self._metadata[election] = existing
                    self._load(election)
                    self._validate_resumed_draft(election)
                    return True
                except generated_provenance.GeneratedProvenanceError:
                    # A corrupt local restart aid must never block regeneration
                    # or affect the previously certified output.
                    pass

        self.reset(election)
        self._metadata[election] = metadata
        _write_json_atomically(metadata_path, metadata)
        return False

    def update_federal_priors(
        self, election, federal_prior_files, dependencies=None
    ):
        """Refresh mid-run federal prior identity used for resume validation."""

        metadata = self._metadata.get(election)
        if metadata is None:
            return
        metadata["federal_prior_files"] = sorted({
            str(Path(path)) for path in federal_prior_files
        })
        metadata["federal_prior_digest"] = _federal_prior_digest(
            metadata["federal_prior_files"]
        )
        if dependencies is not None:
            metadata["dependencies"] = json.loads(
                json.dumps(dependencies, sort_keys=True)
            )
        _write_json_atomically(
            cutoff_working_metadata_path(election), metadata
        )

    def federal_prior_files(self, election):
        metadata = self._metadata.get(election) or {}
        return list(metadata.get("federal_prior_files") or [])

    def _validate_resumed_draft(self, election):
        metadata = self._metadata[election]
        expected_header = list(self.KEY_COLUMNS) + list(
            metadata.get("probability_columns", [])
        )
        if self._headers[election] != expected_header:
            raise generated_provenance.GeneratedProvenanceError(
                "Cutoff draft header does not match its sidecar."
            )
        expected_endpoints = {
            int(entry["scheduled_cutoff_days"]):
                int(entry["poll_trend_end_days"])
            for entry in metadata.get("expected_endpoints", [])
        }
        configured_parties = set(metadata.get("parties", []))
        endpoint_parties = metadata.setdefault("endpoint_parties", {})
        changed = False
        completed = []
        for (scheduled_days, party), row in self._rows[election].items():
            poll_end_days = int(row[1])
            if (
                scheduled_days not in expected_endpoints
                or expected_endpoints[scheduled_days] != poll_end_days
            ):
                raise generated_provenance.GeneratedProvenanceError(
                    "Cutoff draft contains an unexpected endpoint."
                )
            if party == self.COMPLETE_MARKER:
                completed.append((scheduled_days, poll_end_days))
                continue
            if party not in configured_parties:
                raise generated_provenance.GeneratedProvenanceError(
                    "Cutoff draft contains an unexpected party."
                )
            try:
                seed = int(row[3])
                values = [float(value) for value in row[4:]]
            except ValueError as error:
                raise generated_provenance.GeneratedProvenanceError(
                    "Cutoff draft contains a malformed seed or percentile."
                ) from error
            if (
                seed < 1
                or any(
                    not math.isfinite(value) or not 0 <= value <= 100
                    for value in values
                )
            ):
                raise generated_provenance.GeneratedProvenanceError(
                    "Cutoff draft contains an invalid seed or percentile."
                )

        for scheduled_days, poll_end_days in completed:
            key = "{}:{}".format(scheduled_days, poll_end_days)
            parties = endpoint_parties.get(key)
            if (
                not isinstance(parties, list)
                or any(party not in configured_parties for party in parties)
                or any(
                    (scheduled_days, party) not in self._rows[election]
                    for party in parties
                )
            ):
                del self._rows[election][
                    (scheduled_days, self.COMPLETE_MARKER)
                ]
                endpoint_parties.pop(key, None)
                changed = True

        completed_keys = {
            "{}:{}".format(scheduled_days, poll_end_days)
            for scheduled_days, poll_end_days in completed
            if (
                scheduled_days, self.COMPLETE_MARKER
            ) in self._rows[election]
        }
        for key in list(endpoint_parties):
            if key not in completed_keys:
                del endpoint_parties[key]
                changed = True
        if changed:
            _write_json_atomically(
                cutoff_working_metadata_path(election), metadata
            )
            self._write_atomic(election)

    def reset(self, election):
        """Start a fresh cutoff batch for an election."""

        self._headers[election] = None
        self._rows[election] = {}
        self._metadata.pop(election, None)
        working_path = cutoff_working_path(election)
        for path in (
            working_path,
            working_path.with_suffix(working_path.suffix + ".tmp"),
            cutoff_working_metadata_path(election),
            cutoff_working_metadata_path(election).with_suffix(
                cutoff_working_metadata_path(election).suffix + ".tmp"
            ),
        ):
            if path.exists():
                path.unlink()

    def _load(self, election):
        if election in self._rows:
            return
        working_path = cutoff_working_path(election)
        path = (
            working_path
            if working_path.is_file()
            else cutoff_output_path(election)
        )
        rows = {}
        header = None
        if path.is_file():
            with path.open(newline="", encoding="utf-8") as source:
                reader = csv.reader(source)
                try:
                    header = next(reader)
                except StopIteration as error:
                    raise generated_provenance.GeneratedProvenanceError(
                        "Cutoff output is empty: {}".format(path)
                    ) from error
                if (
                    tuple(header[:len(self.KEY_COLUMNS)])
                    != self.KEY_COLUMNS
                ):
                    raise generated_provenance.GeneratedProvenanceError(
                        "Cutoff output has an invalid header: {}".format(path)
                    )
                for line_number, row in enumerate(reader, start=2):
                    if len(row) != len(header):
                        raise generated_provenance.GeneratedProvenanceError(
                            "{}:{} has {} columns; expected {}".format(
                                path, line_number, len(row), len(header)
                            )
                        )
                    try:
                        scheduled_days = int(row[0])
                        int(row[1])
                    except ValueError as error:
                        raise generated_provenance.GeneratedProvenanceError(
                            "{}:{} has an invalid cutoff".format(
                                path, line_number
                            )
                        ) from error
                    key = (scheduled_days, row[2])
                    if key in rows:
                        raise generated_provenance.GeneratedProvenanceError(
                            "{}:{} duplicates cutoff {} for {}".format(
                                path, line_number, scheduled_days, row[2]
                            )
                        )
                    rows[key] = row
        self._headers[election] = header
        self._rows[election] = rows

    def contains(
        self,
        election,
        party,
        scheduled_cutoff_days,
        poll_trend_end_days=None,
    ):
        self._load(election)
        row = self._rows[election].get(
            (int(scheduled_cutoff_days), party)
        )
        return (
            row is not None
            and (
                poll_trend_end_days is None
                or int(row[1]) == int(poll_trend_end_days)
            )
        )

    def is_complete(
        self,
        election,
        scheduled_cutoff_days,
        poll_trend_end_days,
    ):
        return self.contains(
            election,
            self.COMPLETE_MARKER,
            scheduled_cutoff_days,
            poll_trend_end_days,
        )

    def write(
        self,
        election,
        party,
        scheduled_cutoff_days,
        poll_trend_end_days,
        random_seed,
        probabilities,
        values,
    ):
        self._load(election)
        probability_columns = [
            "{}%".format(round(probability * 100))
            for probability in probabilities
        ]
        expected_header = list(self.KEY_COLUMNS) + probability_columns
        header = self._headers[election]
        if header is not None and header != expected_header:
            raise generated_provenance.GeneratedProvenanceError(
                "Cutoff output percentile columns do not match: {}".format(
                    cutoff_output_path(election)
                )
            )
        if len(values) != len(probability_columns):
            raise generated_provenance.GeneratedProvenanceError(
                "Cutoff trend contained {} values; expected {}".format(
                    len(values), len(probability_columns)
                )
            )

        self._headers[election] = expected_header
        self._rows[election][
            (int(scheduled_cutoff_days), party)
        ] = [
            str(int(scheduled_cutoff_days)),
            str(int(poll_trend_end_days)),
            party,
            str(random_seed),
            *[str(round(value, 3)) for value in values],
        ]
        self._write_atomic(election)

    def mark_complete(
        self,
        election,
        scheduled_cutoff_days,
        poll_trend_end_days,
        expected_parties=None,
    ):
        self._load(election)
        header = self._headers[election]
        if header is None:
            raise generated_provenance.GeneratedProvenanceError(
                "Cannot complete cutoff {} for {} without any trend rows."
                .format(scheduled_cutoff_days, election)
            )
        expected_parties = (
            None
            if expected_parties is None
            else sorted(set(expected_parties))
        )
        if expected_parties is not None:
            missing = [
                party for party in expected_parties
                if not self.contains(
                    election,
                    party,
                    scheduled_cutoff_days,
                    poll_trend_end_days,
                )
            ]
            if missing:
                raise generated_provenance.GeneratedProvenanceError(
                    "Cannot complete cutoff {} for {}; missing parties: {}"
                    .format(
                        scheduled_cutoff_days,
                        election,
                        ", ".join(missing),
                    )
                )
            metadata = self._metadata.get(election)
            if metadata is not None:
                key = "{}:{}".format(
                    int(scheduled_cutoff_days),
                    int(poll_trend_end_days),
                )
                metadata.setdefault("endpoint_parties", {})[key] = (
                    expected_parties
                )
                _write_json_atomically(
                    cutoff_working_metadata_path(election), metadata
                )
        self._rows[election][
            (int(scheduled_cutoff_days), self.COMPLETE_MARKER)
        ] = [
            str(int(scheduled_cutoff_days)),
            str(int(poll_trend_end_days)),
            self.COMPLETE_MARKER,
            "",
            *([""] * (len(header) - len(self.KEY_COLUMNS))),
        ]
        self._write_atomic(election)

    def promote(self, election, certify=None):
        """Replace the certified output only after the batch is complete.

        If certification fails, restore the previous certified file and keep
        the completed draft so the expensive batch can be certified again.
        """

        self._load(election)
        completed_cutoffs = {
            scheduled_days
            for scheduled_days, party in self._rows[election]
            if party == self.COMPLETE_MARKER
        }
        incomplete_cutoffs = {
            scheduled_days
            for scheduled_days, party in self._rows[election]
            if (
                party != self.COMPLETE_MARKER
                and scheduled_days not in completed_cutoffs
            )
        }
        if incomplete_cutoffs:
            raise generated_provenance.GeneratedProvenanceError(
                "Cannot promote incomplete cutoff(s) for {}: {}".format(
                    election,
                    ", ".join(
                        str(value)
                        for value in sorted(incomplete_cutoffs)
                    ),
                )
            )
        metadata = self._metadata.get(election)
        if metadata is not None:
            endpoint_parties = metadata.get("endpoint_parties", {})
            for expected in metadata.get("expected_endpoints", []):
                scheduled_days = int(expected["scheduled_cutoff_days"])
                poll_end_days = int(expected["poll_trend_end_days"])
                key = "{}:{}".format(scheduled_days, poll_end_days)
                parties = endpoint_parties.get(key)
                if parties is None:
                    raise generated_provenance.GeneratedProvenanceError(
                        "Cannot promote {}: cutoff {} has no completed party "
                        "manifest.".format(election, scheduled_days)
                    )
                if not self.is_complete(
                    election, scheduled_days, poll_end_days
                ):
                    raise generated_provenance.GeneratedProvenanceError(
                        "Cannot promote {}: cutoff {} is incomplete.".format(
                            election, scheduled_days
                        )
                    )
                missing = [
                    party for party in parties
                    if not self.contains(
                        election,
                        party,
                        scheduled_days,
                        poll_end_days,
                    )
                ]
                if missing:
                    raise generated_provenance.GeneratedProvenanceError(
                        "Cannot promote {} cutoff {}; missing parties: {}"
                        .format(
                            election,
                            scheduled_days,
                            ", ".join(missing),
                        )
                    )
        working_path = cutoff_working_path(election)
        if not working_path.is_file():
            raise generated_provenance.GeneratedProvenanceError(
                "Cannot promote missing cutoff working file: {}".format(
                    working_path
                )
            )
        final_path = cutoff_output_path(election)
        final_path.parent.mkdir(parents=True, exist_ok=True)
        metadata_path = cutoff_working_metadata_path(election)
        if certify is None:
            os.replace(working_path, final_path)
            if metadata_path.exists():
                metadata_path.unlink()
            self._metadata.pop(election, None)
            return

        backup_path = final_path.with_suffix(
            final_path.suffix + ".previous"
        )
        if backup_path.exists():
            backup_path.unlink()
        # Keep resume metadata until certification succeeds so a failed
        # certify/rollback can still resume the completed draft.
        had_certified_output = final_path.is_file()
        if had_certified_output:
            os.replace(final_path, backup_path)
        os.replace(working_path, final_path)
        try:
            certify(final_path)
        except Exception:
            os.replace(final_path, working_path)
            if had_certified_output:
                os.replace(backup_path, final_path)
            raise
        if backup_path.exists():
            backup_path.unlink()
        if metadata_path.exists():
            metadata_path.unlink()
        self._metadata.pop(election, None)

    def _write_atomic(self, election):
        path = cutoff_working_path(election)
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = path.with_suffix(path.suffix + ".tmp")
        with temporary_path.open(
            "w", newline="", encoding="utf-8"
        ) as output:
            writer = csv.writer(output, lineterminator="\n")
            writer.writerow(self._headers[election])
            for key in sorted(
                self._rows[election],
                key=lambda item: (-item[0], item[1]),
            ):
                writer.writerow(self._rows[election][key])
        os.replace(temporary_path, path)


def _record_key(election, party):
    return "pure_poll_outputs:{}:{}".format(election, party)


def _final_record_key(election, party):
    return "poll_trend_outputs:{}:{}".format(election, party)


def _cutoff_record_key(election):
    return "cutoff_poll_outputs:{}".format(election)


def _source_dependencies():
    return {
        category: generated_provenance.source_manifest_dependency(
            category,
            manifest_path,
            ANALYSIS_DIRECTORY,
        )
        for category, manifest_path in SOURCE_DEPENDENCIES.items()
    }


def _load_election_cycles():
    """Load election periods used to infer dependencies of legacy outputs."""

    path = ANALYSIS_DIRECTORY / "Data" / "election-cycles.csv"
    cycles = {}
    try:
        with path.open(newline="", encoding="utf-8") as source:
            rows = csv.reader(source)
            for line_number, row in enumerate(rows, start=1):
                if not row:
                    continue
                if len(row) != 4:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} must contain four columns".format(
                            path, line_number
                        )
                    )
                year, region, start, end = row
                election = "{}{}".format(year, region)
                if election in cycles:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} duplicates election {}".format(
                            path, line_number, election
                        )
                    )
                try:
                    cycles[election] = (
                        date.fromisoformat(start),
                        date.fromisoformat(end),
                    )
                except ValueError as error:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} contains an invalid ISO date".format(
                            path, line_number
                        )
                    ) from error
    except OSError as error:
        raise generated_provenance.GeneratedProvenanceError(
            "could not read election cycles from {}: {}".format(path, error)
        ) from error
    return cycles


def _load_significant_parties():
    """Load the parties modelled separately in each election."""

    path = ANALYSIS_DIRECTORY / "Data" / "significant-parties.csv"
    parties = {}
    try:
        with path.open(newline="", encoding="utf-8") as source:
            rows = csv.reader(source)
            for line_number, row in enumerate(rows, start=1):
                if not row:
                    continue
                if len(row) < 3:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} must contain an election and parties".format(
                            path, line_number
                        )
                    )
                election = "{}{}".format(row[0], row[1])
                if election in parties:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} duplicates election {}".format(
                            path, line_number, election
                        )
                    )
                parties[election] = set(row[2:])
    except OSError as error:
        raise generated_provenance.GeneratedProvenanceError(
            "could not read significant parties from {}: {}".format(
                path, error
            )
        ) from error
    return parties


def _legacy_federal_prior_files(
    election, cycles, significant_parties, party=None, pure=True
):
    """Infer federal trends available to a legacy state trend.

    Legacy outputs predate provenance recording, so their exact runtime
    dependencies cannot be recovered. Election periods provide a conservative
    reconstruction: include existing minor-party federal trends from every
    federal cycle overlapping the state cycle. Newly generated records instead
    store the exact files opened by fp_model.py.
    """

    match = ELECTION_CODE_PATTERN.fullmatch(election)
    if match is None:
        raise generated_provenance.GeneratedProvenanceError(
            "invalid election code in pure-trend output: {}".format(election)
        )
    if match.group(2) == "fed":
        return []
    try:
        state_start, state_end = cycles[election]
    except KeyError as error:
        raise generated_provenance.GeneratedProvenanceError(
            "no election cycle is configured for {}".format(election)
        ) from error
    try:
        state_parties = significant_parties[election]
    except KeyError as error:
        raise generated_provenance.GeneratedProvenanceError(
            "no significant parties are configured for {}".format(election)
        ) from error

    federal_elections = [
        federal_election
        for federal_election, (federal_start, federal_end) in cycles.items()
        if (
            federal_election.endswith("fed")
            and federal_start <= state_end
            and state_start <= federal_end
        )
    ]
    files = []
    for federal_election in sorted(federal_elections):
        try:
            federal_parties = significant_parties[federal_election]
        except KeyError as error:
            raise generated_provenance.GeneratedProvenanceError(
                "no significant parties are configured for {}".format(
                    federal_election
                )
            ) from error
        common_prior_parties = (
            FEDERAL_PRIOR_PARTIES
            & state_parties
            & federal_parties
        )
        if party is not None:
            common_prior_parties &= {party}
        for party in sorted(common_prior_parties):
            pure_suffix = "_pure" if pure else ""
            path = OUTPUT_DIRECTORY / (
                "fp_trend_{}_{}{}.csv".format(
                    federal_election, party, pure_suffix
                )
            )
            if path.is_file():
                files.append(path)
    return files


def _legacy_dependencies(election, party, cycles, significant_parties):
    federal_prior_files = _legacy_federal_prior_files(
        election, cycles, significant_parties, party
    )
    if not federal_prior_files:
        return {}
    return {
        "pure_poll_outputs": generated_provenance.file_dependency(
            "pure_poll_outputs",
            federal_prior_files,
            ANALYSIS_DIRECTORY,
        )
    }


def _prune_irrelevant_federal_priors(
    record, cycles, significant_parties
):
    """Remove over-recorded, unused federal priors from a generated record.

    Recompute dependency metadata only when every originally recorded file is
    unchanged. This prevents a baseline refresh from concealing genuine
    post-generation input changes.
    """

    elections = record["scope"]["elections"]
    if len(elections) != 1 or elections[0].endswith("fed"):
        return False
    dependency = record["dependencies"].get("pure_poll_outputs")
    if dependency is None or dependency["kind"] != "files":
        return False

    original_files = [
        ANALYSIS_DIRECTORY / path for path in dependency["files"]
    ]
    current_dependency = generated_provenance.file_dependency(
        "pure_poll_outputs", original_files, ANALYSIS_DIRECTORY
    )
    if current_dependency["digest"] != dependency["digest"]:
        return False

    allowed_files = {
        path.resolve()
        for path in _legacy_federal_prior_files(
            elections[0],
            cycles,
            significant_parties,
            (
                record["scope"]["parties"][0]
                if len(record["scope"]["parties"]) == 1
                else None
            ),
        )
    }
    retained_files = [
        path for path in original_files if path.resolve() in allowed_files
    ]
    if len(retained_files) == len(original_files):
        return False
    if retained_files:
        record["dependencies"]["pure_poll_outputs"] = (
            generated_provenance.file_dependency(
                "pure_poll_outputs",
                retained_files,
                ANALYSIS_DIRECTORY,
            )
        )
    else:
        del record["dependencies"]["pure_poll_outputs"]
    return True


# Pure, final and cutoff work-unit provenance publication

class PureTrendRecorder:
    """Preflight dependencies and certify completed pure-trend work units."""

    def __init__(self, command):
        self.source_dependencies = _source_dependencies()
        self.run_id, self.run = generated_provenance.generation_run(
            command=command,
            source_revision=generated_provenance.current_source_revision(
                ANALYSIS_DIRECTORY
            ),
            environment=generated_provenance.current_environment(
                ("numpy", "pandas", "pystan")
            ),
        )

    def dependencies_for(self, election, feedback_files):
        dependencies = dict(self.source_dependencies)
        pollster_record = "pollster_parameters:{}".format(election)
        dependencies["pollster_parameters"] = (
            generated_provenance.generated_manifest_dependency(
                "pollster_parameters",
                POLLSTER_MANIFEST_PATH,
                [pollster_record],
                ANALYSIS_DIRECTORY,
                allow_stale=True,
            )
        )
        feedback_files = sorted(set(feedback_files))
        if feedback_files:
            dependencies["pure_poll_outputs"] = (
                generated_provenance.file_dependency(
                    "pure_poll_outputs",
                    feedback_files,
                    ANALYSIS_DIRECTORY,
                )
            )
        return dependencies

    def record(self, election, party, outputs, dependencies, random_seed):
        record = generated_provenance.generation_record(
            category="pure_poll_outputs",
            stage="generate_pure_poll_trends",
            scope=generated_provenance.generation_scope(
                elections=[election],
                parties=[party],
            ),
            run=self.run_id,
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                outputs, ANALYSIS_DIRECTORY
            ),
            random_seed=random_seed,
        )
        generated_provenance.update_manifest(
            MANIFEST_PATH,
            {_record_key(election, party): record},
            {self.run_id: self.run},
            path_base="..",
            description=MANIFEST_DESCRIPTION,
        )


class FinalTrendRecorder:
    """Preflight dependencies and certify completed final-trend work units."""

    def __init__(self, command):
        self.source_dependencies = _source_dependencies()
        self.approval_dependencies = {}
        self.approval_elections = set(
            approvals_provenance.approval_elections()
        )
        self.run_id, self.run = generated_provenance.generation_run(
            command=command,
            source_revision=generated_provenance.current_source_revision(
                ANALYSIS_DIRECTORY
            ),
            environment=generated_provenance.current_environment(
                ("numpy", "pandas", "pystan")
            ),
        )

    def _approval_dependencies(
        self, election, non_invalidating_elections=()
    ):
        cache_key = (
            election,
            tuple(sorted(set(non_invalidating_elections))),
        )
        if cache_key not in self.approval_dependencies:
            self.approval_dependencies[cache_key] = (
                approvals_provenance.generation_dependencies(
                    [election],
                    non_invalidating_elections=cache_key[1],
                )
            )
        return self.approval_dependencies[cache_key]

    def dependencies_for(
        self, election, party, federal_prior_files
    ):
        dependencies = dict(self.source_dependencies)
        dependencies["pollster_parameters"] = (
            generated_provenance.generated_manifest_dependency(
                "pollster_parameters",
                POLLSTER_MANIFEST_PATH,
                ["pollster_parameters:{}".format(election)],
                ANALYSIS_DIRECTORY,
                allow_stale=True,
            )
        )
        if party in APPROVAL_PARTIES and election in self.approval_elections:
            dependencies.update(self._approval_dependencies(election))
        federal_prior_files = sorted(set(federal_prior_files))
        if federal_prior_files:
            dependencies["poll_trend_outputs"] = (
                generated_provenance.file_dependency(
                    "poll_trend_outputs",
                    federal_prior_files,
                    ANALYSIS_DIRECTORY,
                )
            )
        return dependencies

    def record(
        self, election, party, outputs, dependencies, random_seed
    ):
        record = generated_provenance.generation_record(
            category="poll_trend_outputs",
            stage="generate_poll_trends",
            scope=generated_provenance.generation_scope(
                elections=[election],
                parties=[party],
            ),
            run=self.run_id,
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                outputs, ANALYSIS_DIRECTORY
            ),
            random_seed=random_seed,
        )
        generated_provenance.update_manifest(
            FINAL_MANIFEST_PATH,
            {_final_record_key(election, party): record},
            {self.run_id: self.run},
            path_base="..",
            description=FINAL_MANIFEST_DESCRIPTION,
        )


class CutoffTrendRecorder(FinalTrendRecorder):
    """Certify the current consolidated cutoff output for one election."""

    def __init__(self, command):
        super().__init__(command)
        # Cutoffs stage progress in CutoffOutputStore drafts, not calibration
        # checkpoints, so checkpoint helper code is not a cutoff dependency.
        self.source_dependencies = _source_dependencies()

    def preflight_election(self, election):
        """Reject stale local inputs before an expensive cutoff batch starts."""

        # Federal cutoff parents are discovered while individual state-party
        # fits are prepared. Pollster parameters and approval/pure-trend input
        # are known before any fit, so validate them before resetting or
        # sampling the election's consolidated output.
        self.dependencies_for_election(election, [])

    def dependencies_for_election(self, election, federal_prior_files):
        dependencies = dict(self.source_dependencies)
        dependencies["pollster_parameters"] = (
            generated_provenance.generated_manifest_dependency(
                "pollster_parameters",
                POLLSTER_MANIFEST_PATH,
                ["pollster_parameters:{}".format(election)],
                ANALYSIS_DIRECTORY,
                # Cutoffs calibrate historical adjustments and must not be
                # generated from stale pollster parameters.
                allow_stale=False,
            )
        )
        if election in self.approval_elections:
            # Current forecast cycles can gain polls several times a week.
            # Preserve those pure-trend inputs in cutoff lineage, but do not
            # invalidate an expensive historical cutoff batch when they move.
            dependencies.update(
                self._approval_dependencies(
                    election,
                    approvals_provenance.current_elections(),
                )
            )
        federal_prior_files = sorted(set(federal_prior_files))
        if federal_prior_files:
            dependencies["cutoff_poll_outputs"] = (
                generated_provenance.file_dependency(
                    "cutoff_poll_outputs",
                    federal_prior_files,
                    ANALYSIS_DIRECTORY,
                )
            )
        return dependencies

    def record(self, election, output, dependencies):
        record = generated_provenance.generation_record(
            category="cutoff_poll_outputs",
            stage="generate_cutoff_poll_trends",
            scope=generated_provenance.generation_scope(
                elections=[election],
            ),
            run=self.run_id,
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                [output], ANALYSIS_DIRECTORY
            ),
            random_seed="stored per row in consolidated cutoff output",
        )
        generated_provenance.update_manifest(
            CUTOFF_MANIFEST_PATH,
            {_cutoff_record_key(election): record},
            {self.run_id: self.run},
            path_base="..",
            description=CUTOFF_MANIFEST_DESCRIPTION,
        )


def _legacy_records():
    grouped = {}
    for path in sorted(OUTPUT_DIRECTORY.glob("*_pure.csv")):
        match = OUTPUT_PATTERN.fullmatch(path.name)
        if not match:
            continue
        key = (match.group(2), match.group(3))
        grouped.setdefault(key, {})[match.group(1)] = path

    incomplete = {
        key: sorted(OUTPUT_KINDS - set(outputs))
        for key, outputs in grouped.items()
        if set(outputs) != OUTPUT_KINDS
    }
    if incomplete:
        details = "; ".join(
            "{} {} missing {}".format(
                election, party, ", ".join(missing)
            )
            for (election, party), missing in sorted(incomplete.items())
        )
        raise generated_provenance.GeneratedProvenanceError(
            "cannot baseline incomplete pure-trend work units: {}".format(
                details
            )
        )

    records = {}
    cycles = None
    significant_parties = None
    for (election, party), outputs_by_kind in grouped.items():
        if not election.endswith("fed") and cycles is None:
            cycles = _load_election_cycles()
            significant_parties = _load_significant_parties()
        records[_record_key(election, party)] = (
            generated_provenance.generation_record(
                category="pure_poll_outputs",
                stage="generate_pure_poll_trends",
                scope=generated_provenance.generation_scope(
                    elections=[election],
                    parties=[party],
                ),
                run="legacy-pure-trend-baseline",
                dependencies=_legacy_dependencies(
                    election,
                    party,
                    cycles or {},
                    significant_parties or {},
                ),
                outputs=generated_provenance.output_fingerprints(
                    outputs_by_kind.values(), ANALYSIS_DIRECTORY
                ),
                random_seed=None,
                status="legacy",
            )
        )
    return records


def _legacy_final_dependencies(
    election,
    party,
    cycles,
    significant_parties,
    approval_dependencies,
):
    dependencies = {}
    federal_prior_files = _legacy_federal_prior_files(
        election,
        cycles,
        significant_parties,
        party,
        pure=False,
    )
    if federal_prior_files:
        dependencies["poll_trend_outputs"] = (
            generated_provenance.file_dependency(
                "poll_trend_outputs",
                federal_prior_files,
                ANALYSIS_DIRECTORY,
            )
        )
    if party in APPROVAL_PARTIES:
        dependencies.update(approval_dependencies)
    return dependencies


def _legacy_final_records():
    cycles = _load_election_cycles()
    significant_parties = _load_significant_parties()
    grouped = {}
    for path in sorted(OUTPUT_DIRECTORY.glob("*.csv")):
        if path.name.endswith("_pure.csv"):
            continue
        match = FINAL_OUTPUT_PATTERN.fullmatch(path.name)
        if not match:
            continue
        key = (match.group(2), match.group(3))
        if key[1] not in significant_parties.get(key[0], set()):
            continue
        grouped.setdefault(key, {})[match.group(1)] = path

    incomplete = {
        key: sorted(OUTPUT_KINDS - set(outputs))
        for key, outputs in grouped.items()
        if set(outputs) != OUTPUT_KINDS
    }
    if incomplete:
        details = "; ".join(
            "{} {} missing {}".format(
                election, party, ", ".join(missing)
            )
            for (election, party), missing in sorted(incomplete.items())
        )
        raise generated_provenance.GeneratedProvenanceError(
            "cannot baseline incomplete final-trend work units: {}".format(
                details
            )
        )

    records = {}
    approval_dependencies = None
    for (election, party), outputs_by_kind in grouped.items():
        if party in APPROVAL_PARTIES and approval_dependencies is None:
            approval_dependencies = (
                approvals_provenance.generation_dependencies()
            )
        records[_final_record_key(election, party)] = (
            generated_provenance.generation_record(
                category="poll_trend_outputs",
                stage="generate_poll_trends",
                scope=generated_provenance.generation_scope(
                    elections=[election],
                    parties=[party],
                ),
                run="legacy-final-trend-baseline",
                dependencies=_legacy_final_dependencies(
                    election,
                    party,
                    cycles,
                    significant_parties,
                    approval_dependencies or {},
                ),
                outputs=generated_provenance.output_fingerprints(
                    outputs_by_kind.values(), ANALYSIS_DIRECTORY
                ),
                random_seed=None,
                status="legacy",
            )
        )
    return records


def baseline_existing_outputs():
    records = _legacy_records()
    if MANIFEST_PATH.exists():
        existing = generated_provenance.load_manifest(MANIFEST_PATH)
        legacy_records = {
            key: record
            for key, record in records.items()
            if (
                key not in existing["records"]
                or existing["records"][key]["status"] == "legacy"
            )
        }
        cycles = _load_election_cycles()
        significant_parties = _load_significant_parties()
        corrected_records = {
            key: record
            for key, record in existing["records"].items()
            if (
                record["status"] == "generated"
                and _prune_irrelevant_federal_priors(
                    record, cycles, significant_parties
                )
            )
        }
        records = {**legacy_records, **corrected_records}
    run = {
        "generated_at_utc": generated_provenance.utc_now(),
        "command": [Path(sys.executable).name] + sys.argv,
        "source_revision": generated_provenance.current_source_revision(
            ANALYSIS_DIRECTORY
        ),
        "environment": generated_provenance.current_environment(),
    }
    manifest = generated_provenance.update_manifest(
        MANIFEST_PATH,
        records,
        {"legacy-pure-trend-baseline": run},
        path_base="..",
        description=MANIFEST_DESCRIPTION,
    )
    legacy_count = sum(
        record["status"] == "legacy"
        for record in manifest["records"].values()
    )
    print(
        "Recorded {} legacy pure-trend work units in {}".format(
            legacy_count, MANIFEST_PATH
        )
    )


def baseline_existing_final_outputs():
    records = _legacy_final_records()
    if FINAL_MANIFEST_PATH.exists():
        existing = generated_provenance.load_manifest(
            FINAL_MANIFEST_PATH
        )
        records = {
            key: record
            for key, record in records.items()
            if (
                key not in existing["records"]
                or existing["records"][key]["status"] == "legacy"
            )
        }
    run = {
        "generated_at_utc": generated_provenance.utc_now(),
        "command": [Path(sys.executable).name] + sys.argv,
        "source_revision": generated_provenance.current_source_revision(
            ANALYSIS_DIRECTORY
        ),
        "environment": generated_provenance.current_environment(),
    }
    manifest = generated_provenance.update_manifest(
        FINAL_MANIFEST_PATH,
        records,
        {"legacy-final-trend-baseline": run},
        path_base="..",
        description=FINAL_MANIFEST_DESCRIPTION,
    )
    legacy_count = sum(
        record["status"] == "legacy"
        for record in manifest["records"].values()
    )
    print(
        "Recorded {} legacy final-trend work units in {}".format(
            legacy_count, FINAL_MANIFEST_PATH
        )
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            "Maintain provenance for pure and final poll trends."
        )
    )
    parser.add_argument(
        "command",
        choices=("baseline", "baseline-final"),
        help=(
            "baseline fingerprints pure outputs; baseline-final does the "
            "same for final outputs, without claiming either was reproduced "
            "under the current sources"
        ),
    )
    args = parser.parse_args(argv)
    try:
        if args.command == "baseline":
            baseline_existing_outputs()
            return 0
        if args.command == "baseline-final":
            baseline_existing_final_outputs()
            return 0
    except generated_provenance.GeneratedProvenanceError as error:
        print("Error: {}".format(error), file=sys.stderr)
        return 2
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())

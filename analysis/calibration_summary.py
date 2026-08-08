"""Compact calibration components into one strict CSV per election.

Calibrate and bias publish durable abridged Components under
``Outputs/Calibration/Components``. This stage merges them into Summaries
for pollster analysis. Detailed ``calib_*`` / ``fp_*_biascal`` archives remain
readable when no Components or transitional Staging record exists.

Main functions:
* ``read_*`` functions load and strictly validate detailed calibration
  evidence required for a compact summary.
* ``compact_election`` performs the actual reduction for one election.
* ``write_summary_atomically`` and ``write_component_atomically`` publish
  complete CSVs without exposing partial files.
* ``compact`` selects requested elections and coordinates provenance-aware
  batch compaction.
"""

import argparse
import csv
import math
import os
import re
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


SCHEMA_VERSION = "1"
RECENT_POLL_WINDOW_DAYS = 183
SUMMARY_DIRECTORY_NAME = "Summaries"
STAGING_DIRECTORY_NAME = "Staging"
COMPONENT_DIRECTORY_NAME = "Components"
RESIDUAL_EVIDENCE_DIRECTORY_NAME = "Evidence"
SEED_DIRECTORY_NAME = "Seeds"
SUMMARY_FIELDS = (
    "schema_version",
    "record_type",
    "election",
    "party",
    "pollster",
    "weighted_abs_error",
    "error_weight",
    "final_trend_median",
    "new_house_effect_median",
    "recent_poll_count",
)
RECORD_LEAVE_ONE_OUT = "leave_one_out"
RECORD_BIAS_TREND = "bias_trend"
RECORD_BIAS_POLLSTER = "bias_pollster"
RESIDUAL_EVIDENCE_SCHEMA_VERSION = "1"
RESIDUAL_EVIDENCE_FIELDS = (
    "schema_version",
    "election",
    "party",
    "pollster",
    "poll_day_index",
    "poll_index",
    "observed_vote",
    "loo_trend_median",
    "adjusted_vote",
    "loo_percentile",
    "loo_deviation",
    "probability_deviation",
    "neighbour_weight",
    "full_deviation",
    "quotient_weight",
    "final_weight",
)
SEED_SCHEMA_VERSION = "1"
SEED_FIELDS = (
    "schema_version",
    "election",
    "mode",
    "excluded_pollster",
    "party",
    "stan_seed",
)

ELECTION_PATTERN = r"(?P<election>[0-9]{4}[a-z]+)"
PARTY_PATTERN = r"(?P<party>@TPP|[^_]+ FP)"
CALIBRATION_FILENAME = re.compile(
    r"^calib_{}_".format(ELECTION_PATTERN)
    + r"(?P<pollster>.+)_{}\.csv$".format(PARTY_PATTERN)
)
BIAS_FILENAME = re.compile(
    r"^fp_(?P<kind>trend|polls|house_effects)_{}"
    r"_{}_biascal\.csv$".format(ELECTION_PATTERN, PARTY_PATTERN)
)


class CalibrationSummaryError(ValueError):
    """Raised when legacy calibration output cannot be safely compacted."""


@dataclass(frozen=True)
class LeaveOneOutRecord:
    election: str
    party: str
    pollster: str
    weighted_abs_error: float
    error_weight: float


# Legacy-input discovery, loading and validation

def _finite_float(value, description):
    try:
        parsed = float(value)
    except (TypeError, ValueError) as error:
        raise CalibrationSummaryError(
            "{} is not numeric".format(description)
        ) from error
    if not math.isfinite(parsed):
        raise CalibrationSummaryError(
            "{} is not finite".format(description)
        )
    return parsed


def _parse_calibration_filename(path):
    match = CALIBRATION_FILENAME.fullmatch(path.name)
    if match is None:
        raise CalibrationSummaryError(
            "{} has an invalid leave-one-out calibration filename".format(
                path
            )
        )
    values = match.groupdict()
    if not values["pollster"]:
        raise CalibrationSummaryError(
            "{} has an empty pollster name".format(path)
        )
    return values["election"], values["party"], values["pollster"]


def _parse_bias_filename(path):
    match = BIAS_FILENAME.fullmatch(path.name)
    if match is None:
        raise CalibrationSummaryError(
            "{} has an invalid bias-calibration filename".format(path)
        )
    values = match.groupdict()
    return values["kind"], values["election"], values["party"]


def _read_csv(path):
    try:
        with path.open("r", newline="", encoding="utf-8-sig") as source:
            return list(csv.reader(source))
    except OSError as error:
        raise CalibrationSummaryError(
            "could not read {}: {}".format(path, error)
        ) from error


def _nonempty_rows(rows):
    return [row for row in rows if any(field.strip() for field in row)]


def read_leave_one_out(path):
    """Read the small summary at the top of one ``calib_*`` file."""

    election, party, pollster = _parse_calibration_filename(path)
    rows = _nonempty_rows(_read_csv(path))
    if not rows or len(rows[0]) < 2:
        raise CalibrationSummaryError(
            "{} lacks its leave-one-out error summary".format(path)
        )
    error = _finite_float(rows[0][0], "{} weighted absolute error".format(path))
    weight = _finite_float(rows[0][1], "{} error weight".format(path))
    if error < 0 or weight < 0:
        raise CalibrationSummaryError(
            "{} has a negative leave-one-out error or weight".format(path)
        )
    return LeaveOneOutRecord(election, party, pollster, error, weight)


def read_final_trend_median(path):
    """Read the final 50th-percentile trend value from a bias run."""

    kind, election, party = _parse_bias_filename(path)
    if kind != "trend":
        raise CalibrationSummaryError("{} is not a trend file".format(path))
    rows = _nonempty_rows(_read_csv(path))
    if len(rows) < 4:
        raise CalibrationSummaryError(
            "{} lacks a complete trend table".format(path)
        )
    header = rows[2]
    required_columns = ("Day", "Party", "50%")
    missing_columns = [column for column in required_columns if column not in header]
    if missing_columns:
        raise CalibrationSummaryError(
            "{} lacks required trend column(s): {}".format(
                path, ", ".join(missing_columns)
            )
        )
    party_index = header.index("Party")
    median_index = header.index("50%")
    data_rows = rows[3:]
    if not data_rows or len(data_rows[-1]) <= max(party_index, median_index):
        raise CalibrationSummaryError(
            "{} lacks a final trend median".format(path)
        )
    if data_rows[-1][party_index] != party:
        raise CalibrationSummaryError(
            "{} final trend party {} does not match filename party {}".format(
                path, data_rows[-1][party_index], party
            )
        )
    return election, party, _finite_float(
        data_rows[-1][median_index], "{} final trend median".format(path)
    )


def read_new_house_effects(path):
    """Read one party's new house-effect medians by pollster."""

    kind, election, party = _parse_bias_filename(path)
    if kind != "house_effects":
        raise CalibrationSummaryError(
            "{} is not a house-effects file".format(path)
        )
    rows = _nonempty_rows(_read_csv(path))
    if not rows or "50%" not in rows[0]:
        raise CalibrationSummaryError(
            "{} lacks a house-effect median column".format(path)
        )
    header = rows[0]
    for field in ("House", "Party", "50%"):
        if field not in header:
            raise CalibrationSummaryError(
                "{} lacks required house-effect column {}".format(path, field)
            )
    house_index = header.index("House")
    party_index = header.index("Party")
    median_index = header.index("50%")
    in_new_section = False
    house_effects = {}
    for row in rows[1:]:
        if row[0] == "New house effects":
            in_new_section = True
            continue
        if row[0] == "Old house effects":
            break
        if not in_new_section:
            raise CalibrationSummaryError(
                "{} has data before its new house-effects section".format(path)
            )
        if len(row) <= max(house_index, party_index, median_index):
            raise CalibrationSummaryError(
                "{} has a short new house-effect row".format(path)
            )
        pollster = row[house_index].strip()
        if not pollster:
            raise CalibrationSummaryError(
                "{} has an empty house-effect pollster".format(path)
            )
        if row[party_index] != party:
            raise CalibrationSummaryError(
                "{} has party {} but its filename specifies {}".format(
                    path, row[party_index], party
                )
            )
        if pollster in house_effects:
            raise CalibrationSummaryError(
                "{} repeats new house effect for {}".format(path, pollster)
            )
        house_effects[pollster] = _finite_float(
            row[median_index], "{} {} new house-effect median".format(
                path, pollster
            )
        )
    if not in_new_section:
        raise CalibrationSummaryError(
            "{} lacks its new house-effects section".format(path)
        )
    if not house_effects:
        raise CalibrationSummaryError(
            "{} has no new house effects".format(path)
        )
    return election, party, house_effects


def read_recent_poll_counts(path):
    """Count each pollster's entries in the final 183 model days."""

    kind, election, party = _parse_bias_filename(path)
    if kind != "polls":
        raise CalibrationSummaryError("{} is not a poll file".format(path))
    try:
        with path.open("r", newline="", encoding="utf-8-sig") as source:
            reader = csv.DictReader(source)
            expected = ("Firm", "Day", party, "{} adj".format(party))
            # ``@TPP reported`` is retained for diagnostics but is not an
            # input to pollster analysis.  Some legitimate historical rows
            # contain ``nan`` there, so require its header without treating
            # that unused source value as a compaction failure.
            required_headers = expected
            if party == "@TPP":
                required_headers += ("{} reported".format(party),)
            if reader.fieldnames is None:
                raise CalibrationSummaryError("{} lacks a poll header".format(path))
            missing = [
                field for field in required_headers if field not in reader.fieldnames
            ]
            if missing:
                raise CalibrationSummaryError(
                    "{} lacks required poll column(s): {}".format(
                        path, ", ".join(missing)
                    )
                )
            polls = []
            for row_number, row in enumerate(reader, start=2):
                if not any((value or "").strip() for value in row.values()):
                    continue
                pollster = (row["Firm"] or "").strip()
                if not pollster:
                    raise CalibrationSummaryError(
                        "{}:{} has an empty pollster".format(path, row_number)
                    )
                day = _finite_float(
                    row["Day"], "{}:{} poll day".format(path, row_number)
                )
                for field in expected[2:]:
                    _finite_float(
                        row[field], "{}:{} {}".format(path, row_number, field)
                    )
                polls.append((pollster, day))
    except OSError as error:
        raise CalibrationSummaryError(
            "could not read {}: {}".format(path, error)
        ) from error
    if not polls:
        raise CalibrationSummaryError("{} has no poll rows".format(path))
    final_day = max(int(day + 0.01) for _, day in polls)
    start_day = final_day - RECENT_POLL_WINDOW_DAYS
    # Keep zero-count pollsters.  A new house effect can validly survive past
    # the 183-day window; the existing analysis simply gives it no recent-poll
    # weight.  The compact format preserves that distinction explicitly.
    counts = {pollster: 0 for pollster, _ in polls}
    for pollster, day in polls:
        if int(day + 0.01) >= start_day:
            counts[pollster] += 1
    return election, party, dict(counts)


def _filename_election_hint(path):
    """Return an election prefix even when a legacy filename is malformed."""

    match = re.match(
        r"^(?:calib_|fp_(?:trend|polls|house_effects)_)([0-9]{4}[a-z]+)",
        path.name,
    )
    return match.group(1) if match else None


def _discover_files(calibration_directory, selected_elections=None):
    """Group recognised inputs, rejecting malformed files in the active scope."""

    leave_one_out = defaultdict(list)
    bias_files = defaultdict(dict)
    invalid = []
    for path in calibration_directory.glob("*.csv"):
        if path.name.startswith("calib_"):
            try:
                election, party, pollster = _parse_calibration_filename(path)
            except CalibrationSummaryError as error:
                if (
                    selected_elections is None
                    or _filename_election_hint(path) in selected_elections
                ):
                    invalid.append(str(error))
                continue
            leave_one_out[election].append(path)
            continue
        if path.name.startswith("fp_") and path.name.endswith("_biascal.csv"):
            try:
                kind, election, party = _parse_bias_filename(path)
            except CalibrationSummaryError as error:
                if (
                    selected_elections is None
                    or _filename_election_hint(path) in selected_elections
                ):
                    invalid.append(str(error))
                continue
            key = (election, party)
            if kind in bias_files[key]:
                invalid.append(
                    "{} duplicates {} bias calibration for {} {}".format(
                        path, kind, election, party
                    )
                )
            bias_files[key][kind] = path
    if invalid:
        raise CalibrationSummaryError("\n".join(sorted(invalid)))
    return leave_one_out, bias_files


def discover_elections(calibration_directory):
    """Return election codes represented by recognised legacy calibration files."""

    leave_one_out, bias_files = _discover_files(Path(calibration_directory))
    return sorted(set(leave_one_out) | {key[0] for key in bias_files})


def _format_number(value):
    return format(value, ".17g")


def _summary_row(record_type, election, party, pollster="", **values):
    row = {field: "" for field in SUMMARY_FIELDS}
    row.update(
        {
            "schema_version": SCHEMA_VERSION,
            "record_type": record_type,
            "election": election,
            "party": party,
            "pollster": pollster,
        }
    )
    for field, value in values.items():
        row[field] = _format_number(value) if isinstance(value, float) else str(value)
    return row


def _calibration_component_name(component):
    if component not in {"leave-one-out", "bias"}:
        raise CalibrationSummaryError(
            "{} is not a recognised calibration component".format(component)
        )
    return component


def direct_staging_path(calibration_directory, election, component):
    """Return the transitional Staging path (read fallback during migration)."""

    component = _calibration_component_name(component)
    return (
        Path(calibration_directory)
        / STAGING_DIRECTORY_NAME
        / "{}-{}.csv".format(normalize_election(election), component)
    )


def direct_component_path(calibration_directory, election, component):
    """Return the durable abridged component path for one calibration half.

    Calibrate and bias each publish one Components CSV. Compact merges them
    into Summaries; components are never deleted by a different work unit.
    """

    component = _calibration_component_name(component)
    return (
        Path(calibration_directory)
        / COMPONENT_DIRECTORY_NAME
        / "{}-{}.csv".format(normalize_election(election), component)
    )


def write_component_atomically(path, rows):
    """Publish one durable abridged component (header-only when empty LOO)."""

    write_summary_atomically(path, rows)


def active_component_path(
    calibration_directory, election, component, allowed_paths=None
):
    """Prefer durable Components, then transitional Staging, when active."""

    for path in (
        direct_component_path(calibration_directory, election, component),
        direct_staging_path(calibration_directory, election, component),
    ):
        if _path_is_active(path, allowed_paths):
            return path
    return None


# Core calibration-evidence reduction

def build_leave_one_out_rows(election, values):
    """Create compact leave-one-out rows from in-memory reducer values."""

    election = normalize_election(election)
    rows = []
    for party, pollster, weighted_abs_error, error_weight in values:
        if not pollster:
            raise CalibrationSummaryError(
                "{} has an empty leave-one-out pollster".format(election)
            )
        error = _finite_float(
            weighted_abs_error,
            "{} {} weighted leave-one-out error".format(election, pollster),
        )
        weight = _finite_float(
            error_weight,
            "{} {} leave-one-out weight".format(election, pollster),
        )
        if error < 0 or weight < 0:
            raise CalibrationSummaryError(
                "{} {} has a negative leave-one-out value".format(
                    election, pollster
                )
            )
        rows.append((party, pollster, error, weight))
    return [
        _summary_row(
            RECORD_LEAVE_ONE_OUT,
            election,
            party,
            pollster,
            weighted_abs_error=error,
            error_weight=weight,
        )
        for party, pollster, error, weight in sorted(rows)
    ]


def build_bias_rows(election, values):
    """Create compact bias rows from final in-memory model summaries.

    ``values`` contains ``(party, final_median, house_effects, poll_counts)``
    for every successfully modelled party.
    """

    election = normalize_election(election)
    rows = []
    for party, final_median, house_effects, poll_counts in values:
        final_median = _finite_float(
            final_median, "{} {} final trend median".format(election, party)
        )
        if set(house_effects) != set(poll_counts):
            raise CalibrationSummaryError(
                "{} {} has mismatched direct bias pollster keys".format(
                    election, party
                )
            )
        rows.append(
            _summary_row(
                RECORD_BIAS_TREND,
                election,
                party,
                final_trend_median=final_median,
            )
        )
        for pollster in sorted(house_effects):
            median = _finite_float(
                house_effects[pollster],
                "{} {} {} house-effect median".format(
                    election, party, pollster
                ),
            )
            count = poll_counts[pollster]
            if not isinstance(count, int) or count < 0:
                raise CalibrationSummaryError(
                    "{} {} {} recent poll count is invalid".format(
                        election, party, pollster
                    )
                )
            rows.append(
                _summary_row(
                    RECORD_BIAS_POLLSTER,
                    election,
                    party,
                    pollster,
                    new_house_effect_median=median,
                    recent_poll_count=count,
                )
            )
    return rows


def _restrict_to_active_paths(discovered_files, allowed_paths):
    """Keep only files owned by the selected provenance work units."""

    allowed_paths = {Path(path).resolve() for path in allowed_paths}
    leave_one_out, bias_files = discovered_files
    return (
        {
            election: [
                path for path in paths if path.resolve() in allowed_paths
            ]
            for election, paths in leave_one_out.items()
        },
        {
            key: {
                kind: path
                for kind, path in files.items()
                if path.resolve() in allowed_paths
            }
            for key, files in bias_files.items()
        },
    )


def _path_is_active(path, allowed_paths):
    """Return whether a file exists and is in the active provenance set."""

    path = Path(path)
    if not path.is_file():
        return False
    if allowed_paths is None:
        return True
    return path.resolve() in {Path(item).resolve() for item in allowed_paths}


def _component_elections(calibration_directory, selected_elections=None):
    """Return elections with durable Components or transitional Staging files."""

    found = set()
    for directory_name in (
        COMPONENT_DIRECTORY_NAME,
        STAGING_DIRECTORY_NAME,
    ):
        directory = Path(calibration_directory) / directory_name
        if not directory.is_dir():
            continue
        for path in directory.glob("*.csv"):
            name = path.name
            if name.endswith("-bias.csv"):
                election = name[: -len("-bias.csv")]
            elif name.endswith("-leave-one-out.csv"):
                election = name[: -len("-leave-one-out.csv")]
            else:
                continue
            try:
                found.add(normalize_election(election))
            except CalibrationSummaryError:
                continue
    if selected_elections is not None:
        return found & set(selected_elections)
    return found


# Backward-compatible alias used by older call sites/tests.
_staging_elections = _component_elections


def _sort_summary_rows(rows):
    order = {
        RECORD_LEAVE_ONE_OUT: 0,
        RECORD_BIAS_TREND: 1,
        RECORD_BIAS_POLLSTER: 2,
    }
    return sorted(
        rows,
        key=lambda row: (
            order[row["record_type"]],
            row["party"],
            row["pollster"],
        ),
    )


def compact_election(
    calibration_directory, election, discovered_files=None, allowed_paths=None
):
    """Validate one election's active inputs and return its compact rows.

    Modern ``--bias`` / ``--calibrate`` runs publish durable Components CSVs
    (with Staging retained only as a transitional read fallback). Empty LOO
    Components yield bias-only summaries. When either abridged half is present,
    both are required. Detailed ``fp_*_biascal.csv`` / ``calib_*`` triples
    remain supported only for pre-component archives.
    """

    calibration_directory = Path(calibration_directory)
    election = normalize_election(election)
    if discovered_files is None:
        discovered_files = _discover_files(
            calibration_directory, selected_elections={election}
        )
    if allowed_paths is not None:
        discovered_files = _restrict_to_active_paths(
            discovered_files, allowed_paths
        )
    leave_one_out_files, bias_files = discovered_files

    loo_component = active_component_path(
        calibration_directory, election, "leave-one-out", allowed_paths
    )
    bias_component = active_component_path(
        calibration_directory, election, "bias", allowed_paths
    )
    if (loo_component is None) != (bias_component is None):
        missing = "leave-one-out" if loo_component is None else "bias"
        present = "bias" if loo_component is None else "leave-one-out"
        raise CalibrationSummaryError(
            "{} has an abridged {} component but no abridged {} component; "
            "run calibrate and bias (or restore both Components) before "
            "compacting".format(election, present, missing)
        )

    rows = []
    if loo_component is not None:
        rows.extend(
            _read_direct_staging(
                loo_component,
                election,
                {RECORD_LEAVE_ONE_OUT},
                allow_empty=True,
            )
        )
    else:
        leave_one_out = [
            read_leave_one_out(path)
            for path in leave_one_out_files.get(election, [])
        ]
        if len(
            {(record.party, record.pollster) for record in leave_one_out}
        ) != len(leave_one_out):
            raise CalibrationSummaryError(
                "{} has duplicate leave-one-out election/party/pollster "
                "keys".format(election)
            )
        for record in leave_one_out:
            rows.append(
                _summary_row(
                    RECORD_LEAVE_ONE_OUT,
                    record.election,
                    record.party,
                    record.pollster,
                    weighted_abs_error=record.weighted_abs_error,
                    error_weight=record.error_weight,
                )
            )

    if bias_component is not None:
        rows.extend(
            _read_direct_staging(
                bias_component,
                election,
                {RECORD_BIAS_TREND, RECORD_BIAS_POLLSTER},
            )
        )
        return _sort_summary_rows(rows)

    records_by_party = defaultdict(dict)
    for (file_election, party), files in bias_files.items():
        if file_election != election:
            continue
        if not files:
            # Provenance filtering removed every legacy path for this party.
            continue
        missing = {"trend", "polls", "house_effects"} - set(files)
        if missing:
            raise CalibrationSummaryError(
                "{} {} lacks bias-calibration file(s): {}".format(
                    election, party, ", ".join(sorted(missing))
                )
            )
        trend_election, trend_party, trend_median = read_final_trend_median(
            files["trend"]
        )
        house_election, house_party, house_effects = read_new_house_effects(
            files["house_effects"]
        )
        poll_election, poll_party, poll_counts = read_recent_poll_counts(
            files["polls"]
        )
        identities = {
            (trend_election, trend_party),
            (house_election, house_party),
            (poll_election, poll_party),
        }
        if identities != {(election, party)}:
            raise CalibrationSummaryError(
                "{} {} has mismatched bias-calibration file identities".format(
                    election, party
                )
            )
        if set(house_effects) != set(poll_counts):
            missing_house_effects = sorted(set(poll_counts) - set(house_effects))
            missing_polls = sorted(set(house_effects) - set(poll_counts))
            details = []
            if missing_house_effects:
                details.append(
                    "polls without new house effects: {}".format(
                        ", ".join(missing_house_effects)
                    )
                )
            if missing_polls:
                details.append(
                    "new house effects without recent polls: {}".format(
                        ", ".join(missing_polls)
                    )
                )
            raise CalibrationSummaryError(
                "{} {} has mismatched pollster keys ({})".format(
                    election, party, "; ".join(details)
                )
            )
        records_by_party[party] = {
            "trend_median": trend_median,
            "house_effects": house_effects,
            "poll_counts": poll_counts,
        }

    if not rows and not records_by_party:
        raise CalibrationSummaryError(
            "{} has no recognised calibration files".format(election)
        )
    for party in sorted(records_by_party):
        record = records_by_party[party]
        rows.append(
            _summary_row(
                RECORD_BIAS_TREND,
                election,
                party,
                final_trend_median=record["trend_median"],
            )
        )
        for pollster in sorted(record["house_effects"]):
            rows.append(
                _summary_row(
                    RECORD_BIAS_POLLSTER,
                    election,
                    party,
                    pollster,
                    new_house_effect_median=record["house_effects"][pollster],
                    recent_poll_count=record["poll_counts"][pollster],
                )
            )
    return _sort_summary_rows(rows)


# Staging, atomic publication and direct-summary support

def summary_path(calibration_directory, election):
    return Path(calibration_directory) / SUMMARY_DIRECTORY_NAME / "{}.csv".format(
        election
    )


def residual_evidence_path(calibration_directory, election):
    return (
        Path(calibration_directory)
        / RESIDUAL_EVIDENCE_DIRECTORY_NAME
        / "{}.csv".format(normalize_election(election))
    )


def build_residual_evidence_row(
    election,
    party,
    pollster,
    poll_day_index,
    poll_index,
    values,
):
    """Build one versioned held-out-poll record for later reducer research."""

    row = {
        "schema_version": RESIDUAL_EVIDENCE_SCHEMA_VERSION,
        "election": normalize_election(election),
        "party": str(party),
        "pollster": str(pollster),
        "poll_day_index": int(poll_day_index),
        "poll_index": int(poll_index),
    }
    for field, value in zip(
        RESIDUAL_EVIDENCE_FIELDS[6:],
        values,
    ):
        row[field] = "" if value is None else _finite_float(
            value,
            "{} {} residual {}".format(election, pollster, field),
        )
    return row


def write_residual_evidence_atomically(path, rows):
    """Publish complete per-poll calibration evidence without partial files."""

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=".{}-".format(path.stem),
        suffix=".tmp",
        text=True,
    )
    try:
        with os.fdopen(descriptor, "w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(
                output,
                fieldnames=RESIDUAL_EVIDENCE_FIELDS,
                lineterminator="\n",
            )
            writer.writeheader()
            writer.writerows(rows)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def seed_manifest_path(calibration_directory, election, mode):
    normalized_mode = str(mode).strip().lower()
    if normalized_mode not in {"calibration", "bias"}:
        raise CalibrationSummaryError(
            "seed manifest mode must be calibration or bias"
        )
    return (
        Path(calibration_directory)
        / SEED_DIRECTORY_NAME
        / "{}-{}.csv".format(normalize_election(election), normalized_mode)
    )


def build_seed_rows(election, mode, resolved_seeds):
    rows = []
    for (seed_mode, excluded_pollster, party), seed in sorted(
        resolved_seeds.items()
    ):
        if seed_mode != mode:
            continue
        try:
            seed = int(seed)
        except (TypeError, ValueError) as error:
            raise CalibrationSummaryError(
                "{} {} has a non-integer Stan seed".format(election, party)
            ) from error
        if not 1 <= seed < 2 ** 31:
            raise CalibrationSummaryError(
                "{} {} has an out-of-range Stan seed".format(election, party)
            )
        rows.append({
            "schema_version": SEED_SCHEMA_VERSION,
            "election": normalize_election(election),
            "mode": mode,
            "excluded_pollster": excluded_pollster,
            "party": party,
            "stan_seed": seed,
        })
    return rows


def write_seed_manifest_atomically(path, rows):
    """Persist every completed calibration fit seed for reproducibility."""

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=".{}-".format(path.stem),
        suffix=".tmp",
        text=True,
    )
    try:
        with os.fdopen(descriptor, "w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(
                output,
                fieldnames=SEED_FIELDS,
                lineterminator="\n",
            )
            writer.writeheader()
            writer.writerows(rows)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def write_summary_atomically(path, rows):
    """Write one complete summary before replacing the previous version."""

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=".{}-".format(path.stem),
        suffix=".tmp",
        text=True,
    )
    try:
        with os.fdopen(descriptor, "w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(
                output, fieldnames=SUMMARY_FIELDS, lineterminator="\n"
            )
            writer.writeheader()
            writer.writerows(rows)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def write_direct_staging_atomically(path, rows):
    """Write one non-empty abridged component atomically (tests/transitional)."""

    if not rows:
        raise CalibrationSummaryError("direct calibration staging has no rows")
    write_summary_atomically(path, rows)


def _read_abridged_component(
    path, election, expected_record_types, allow_empty=False
):
    """Read one abridged component and reject cross-election contamination."""

    try:
        with Path(path).open(newline="", encoding="utf-8-sig") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames != list(SUMMARY_FIELDS):
                raise CalibrationSummaryError(
                    "{} has an invalid direct-calibration header".format(path)
                )
            rows = list(reader)
    except OSError as error:
        raise CalibrationSummaryError(
            "could not read calibration component {}: {}".format(
                path, error
            )
        ) from error
    return validate_abridged_rows(
        path,
        rows,
        election,
        expected_record_types,
        allow_empty=allow_empty,
    )


def _required_component_value(row, field, path, row_number):
    value = row.get(field)
    if value is None or not value.strip():
        raise CalibrationSummaryError(
            "{}:{} has an empty {}".format(path, row_number, field)
        )
    if value != value.strip():
        raise CalibrationSummaryError(
            "{}:{} has whitespace around {}".format(path, row_number, field)
        )
    return value


def _require_empty_component_fields(row, allowed, path, row_number):
    if row.get(None):
        raise CalibrationSummaryError(
            "{}:{} has more fields than the direct-calibration schema".format(
                path, row_number
            )
        )
    for field in SUMMARY_FIELDS:
        if field in allowed:
            continue
        if (row.get(field) or "").strip():
            raise CalibrationSummaryError(
                "{}:{} has an unexpected {} value".format(
                    path, row_number, field
                )
            )


def _nonnegative_component_float(row, field, path, row_number):
    value = _finite_float(
        _required_component_value(row, field, path, row_number),
        "{}:{} {}".format(path, row_number, field),
    )
    if value < 0:
        raise CalibrationSummaryError(
            "{}:{} has a negative {}".format(path, row_number, field)
        )
    return value


def _nonnegative_component_integer(row, field, path, row_number):
    value = _nonnegative_component_float(row, field, path, row_number)
    if int(value) != value:
        raise CalibrationSummaryError(
            "{}:{} has a non-integer {}".format(path, row_number, field)
        )
    return int(value)


def validate_abridged_rows(
    path,
    rows,
    election,
    expected_record_types,
    allow_empty=False,
):
    """Validate compact rows shared by Components and consumer summaries."""

    expected_record_types = set(expected_record_types)
    if not rows:
        if allow_empty:
            return []
        raise CalibrationSummaryError(
            "{} has no direct calibration rows".format(path)
        )

    seen_loo = set()
    seen_trends = set()
    seen_pollsters = set()
    for row_number, row in enumerate(rows, start=2):
        unexpected_fields = set(row) - set(SUMMARY_FIELDS) - {None}
        if unexpected_fields:
            raise CalibrationSummaryError(
                "{}:{} has unknown direct-calibration field(s): {}".format(
                    path, row_number, ", ".join(sorted(unexpected_fields))
                )
            )
        if row["schema_version"] != SCHEMA_VERSION:
            raise CalibrationSummaryError(
                "{}:{} has unsupported schema version".format(path, row_number)
            )
        if row["election"] != election:
            raise CalibrationSummaryError(
                "{}:{} belongs to {} rather than {}".format(
                    path, row_number, row["election"], election
                )
            )
        record_type = row["record_type"]
        if record_type not in expected_record_types:
            raise CalibrationSummaryError(
                "{}:{} has an unexpected record type {}".format(
                    path, row_number, record_type
                )
            )
        party = _required_component_value(row, "party", path, row_number)
        common = {"schema_version", "record_type", "election", "party"}
        if record_type == RECORD_LEAVE_ONE_OUT:
            pollster = _required_component_value(
                row, "pollster", path, row_number
            )
            _nonnegative_component_float(
                row, "weighted_abs_error", path, row_number
            )
            _nonnegative_component_float(
                row, "error_weight", path, row_number
            )
            _require_empty_component_fields(
                row,
                common | {"pollster", "weighted_abs_error", "error_weight"},
                path,
                row_number,
            )
            key = (party, pollster)
            if key in seen_loo:
                raise CalibrationSummaryError(
                    "{}:{} repeats leave-one-out evidence for {} {}".format(
                        path, row_number, party, pollster
                    )
                )
            seen_loo.add(key)
        elif record_type == RECORD_BIAS_TREND:
            final_median = _required_component_value(
                row, "final_trend_median", path, row_number
            )
            _finite_float(
                final_median,
                "{}:{} final_trend_median".format(path, row_number),
            )
            _require_empty_component_fields(
                row, common | {"final_trend_median"}, path, row_number
            )
            if party in seen_trends:
                raise CalibrationSummaryError(
                    "{}:{} repeats a bias trend for {}".format(
                        path, row_number, party
                    )
                )
            seen_trends.add(party)
        elif record_type == RECORD_BIAS_POLLSTER:
            pollster = _required_component_value(
                row, "pollster", path, row_number
            )
            house_effect = _required_component_value(
                row, "new_house_effect_median", path, row_number
            )
            _finite_float(
                house_effect,
                "{}:{} new_house_effect_median".format(path, row_number),
            )
            _nonnegative_component_integer(
                row, "recent_poll_count", path, row_number
            )
            _require_empty_component_fields(
                row,
                common | {
                    "pollster", "new_house_effect_median",
                    "recent_poll_count",
                },
                path,
                row_number,
            )
            key = (party, pollster)
            if key in seen_pollsters:
                raise CalibrationSummaryError(
                    "{}:{} repeats a bias pollster for {} {}".format(
                        path, row_number, party, pollster
                    )
                )
            seen_pollsters.add(key)

    if {
        RECORD_BIAS_TREND,
        RECORD_BIAS_POLLSTER,
    }.issubset(expected_record_types):
        pollster_parties = {party for party, _ in seen_pollsters}
        if seen_trends != pollster_parties:
            raise CalibrationSummaryError(
                "{} has incomplete bias evidence: trend and pollster parties differ"
                .format(path)
            )
        if not seen_trends:
            raise CalibrationSummaryError(
                "{} has no complete bias evidence".format(path)
            )
    return rows


_read_direct_staging = _read_abridged_component


def promote_direct_summary(calibration_directory, election):
    """Merge abridged components into one Summary (test/helper path).

    Production Summaries are published by ``compact_calibration_summaries``.
    Prefer durable Components, then transitional Staging. Only Staging files
    are deleted after a successful write; Components are never removed.
    """

    election = normalize_election(election)
    loo_path = active_component_path(
        calibration_directory, election, "leave-one-out"
    )
    bias_path = active_component_path(
        calibration_directory, election, "bias"
    )
    if loo_path is None or bias_path is None:
        raise CalibrationSummaryError(
            "{} lacks leave-one-out or bias components for promotion".format(
                election
            )
        )
    loo_rows = _read_direct_staging(
        loo_path, election, {RECORD_LEAVE_ONE_OUT}, allow_empty=True
    )
    bias_rows = _read_direct_staging(
        bias_path, election, {RECORD_BIAS_TREND, RECORD_BIAS_POLLSTER}
    )
    rows = _sort_summary_rows(loo_rows + bias_rows)
    write_summary_atomically(summary_path(calibration_directory, election), rows)
    for path in (loo_path, bias_path):
        if STAGING_DIRECTORY_NAME in Path(path).parts:
            path.unlink()
    return summary_path(calibration_directory, election), len(rows)


# Batch selection and command-line entry point

def normalize_election(value):
    normalized = value.strip().lower().replace("-", "")
    if not re.fullmatch(r"[0-9]{4}[a-z]+", normalized):
        raise CalibrationSummaryError(
            "{} is not a valid election code".format(value)
        )
    return normalized


def prepare_compact_rows(
    calibration_directory,
    elections,
    input_paths_for_election=None,
):
    """Validate every selected election before publishing any summary."""

    calibration_directory = Path(calibration_directory)
    if not calibration_directory.is_dir():
        raise CalibrationSummaryError(
            "calibration directory {} does not exist".format(
                calibration_directory
            )
        )
    if elections == "all":
        discovered_files = _discover_files(calibration_directory)
        available = set(discovered_files[0]) | {
            key[0] for key in discovered_files[1]
        } | _staging_elections(calibration_directory)
        selected = sorted(available)
    else:
        selected = [normalize_election(election) for election in elections]
        discovered_files = _discover_files(
            calibration_directory, selected_elections=set(selected)
        )
        available = set(discovered_files[0]) | {
            key[0] for key in discovered_files[1]
        } | _staging_elections(
            calibration_directory, selected_elections=set(selected)
        )
    if not selected:
        raise CalibrationSummaryError("no elections were selected")
    prepared = []
    for election in selected:
        if election not in available:
            raise CalibrationSummaryError(
                "{} has no recognised calibration files".format(election)
            )
        rows = compact_election(
            calibration_directory,
            election,
            discovered_files=discovered_files,
            allowed_paths=(
                input_paths_for_election(election)
                if input_paths_for_election is not None
                else None
            ),
        )
        prepared.append((election, rows))
    return prepared


def compact(
    calibration_directory,
    elections,
    dry_run=False,
    input_paths_for_election=None,
):
    """Compact selected elections after validating the complete selection."""

    prepared = prepare_compact_rows(
        calibration_directory,
        elections,
        input_paths_for_election=input_paths_for_election,
    )
    calibration_directory = Path(calibration_directory)
    if not dry_run:
        for election, rows in prepared:
            write_summary_atomically(
                summary_path(calibration_directory, election), rows
            )
    return [(election, len(rows)) for election, rows in prepared]


def publish_prepared_summaries(
    calibration_directory, prepared, record_published=None
):
    """Publish prevalidated summaries and record each one before continuing."""

    calibration_directory = Path(calibration_directory)
    for election, rows in prepared:
        output = summary_path(calibration_directory, election)
        write_summary_atomically(output, rows)
        if record_published is not None:
            try:
                record_published(election, output)
            except Exception as error:
                raise CalibrationSummaryError(
                    "{} was published but provenance recording failed: {}"
                    .format(election, error)
                ) from error
    return [(election, len(rows)) for election, rows in prepared]


def parse_arguments(arguments):
    parser = argparse.ArgumentParser(
        description="Compact legacy poll calibration files into CSV summaries."
    )
    parser.add_argument(
        "--election",
        required=True,
        nargs="+",
        help="Election code(s), or 'all' to compact every recognised election.",
    )
    parser.add_argument(
        "--dry-run",
        "--report-only",
        action="store_true",
        help="Validate and report summaries without writing them.",
    )
    parser.add_argument(
        "--calibration-directory",
        default="Outputs/Calibration",
        help="Calibration directory, primarily for maintenance and testing.",
    )
    return parser.parse_args(arguments)


def main(arguments=None):
    args = parse_arguments(arguments)
    if "all" in args.election:
        if len(args.election) != 1:
            print("Could not compact calibration data: 'all' cannot be combined with election codes.", file=sys.stderr)
            return 2
        elections = "all"
    else:
        elections = args.election
    try:
        input_paths_for_election = None
        default_directory = Path("Outputs/Calibration").resolve()
        if Path(args.calibration_directory).resolve() == default_directory:
            import calibration_summary_provenance
            import generated_provenance

            if calibration_summary_provenance.MANIFEST_PATH.is_file():
                manifest = generated_provenance.load_manifest(
                    calibration_summary_provenance.MANIFEST_PATH
                )
                input_paths_for_election = lambda election: (
                    calibration_summary_provenance.compatibility_input_paths(
                        election, manifest
                    )
                )
        prepared = prepare_compact_rows(
            args.calibration_directory,
            elections,
            input_paths_for_election=input_paths_for_election,
        )
    except CalibrationSummaryError as error:
        print("Could not compact calibration data: {}".format(error), file=sys.stderr)
        return 2
    results = [(election, len(rows)) for election, rows in prepared]
    if not args.dry_run:
        # Custom directories exist for maintenance/testing and intentionally
        # do not write repository provenance. The standard path publishes one
        # summary then immediately records it before moving to the next.
        standard_directory = (
            Path(args.calibration_directory).resolve() == default_directory
        )
        recorder = None
        manifest = None
        if standard_directory:
            import calibration_summary_provenance
            import generated_provenance

            recorder = calibration_summary_provenance.CalibrationSummaryRecorder(
                [Path(sys.executable).name] + sys.argv
            )
            manifest = generated_provenance.load_manifest(
                calibration_summary_provenance.MANIFEST_PATH
            )

        def record_published(election, output):
            input_paths = (
                input_paths_for_election(election)
                if input_paths_for_election is not None
                else None
            )
            recorder.record(election, output, input_paths, manifest)
            recorder.flush()

        try:
            results = publish_prepared_summaries(
                args.calibration_directory,
                prepared,
                record_published if recorder is not None else None,
            )
        except Exception as error:
            print(
                "Calibration summary publication failed: {}".format(error),
                file=sys.stderr,
            )
            return 2
    action = "Validated" if args.dry_run else "Wrote"
    for election, row_count in results:
        print("{} {} calibration summary row(s) for {}.".format(
            action, row_count, election
        ))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Load typed calibration evidence for pollster_analysis.py.

Parent: pollster_analysis.py selects provenance-approved files, then passes the
typed evidence returned here to its numerical reducers.  Compact election
summaries are preferred when present.  The legacy adapter is the only place
outside calibration_summary.py that interprets historical calibration file
names and layouts.
"""

import csv
from dataclasses import dataclass
from pathlib import Path

import calibration_summary
from election_code import ElectionCode
from pollster_analysis_common import ConfigError, canonical_party


COMPACT_SUMMARY_DIRECTORY = "Summaries"


@dataclass(frozen=True)
class LeaveOneOutEvidence:
    election: ElectionCode
    party: str
    pollster: str
    weighted_abs_error: float
    error_weight: float


@dataclass(frozen=True)
class BiasEvidence:
    election: ElectionCode
    party: str
    final_trend_median: float
    house_effects: dict
    recent_poll_counts: dict


@dataclass(frozen=True)
class CalibrationEvidence:
    """The complete typed inputs used by the pollster reducers."""

    leave_one_out: tuple
    bias: tuple

    def bias_for_election(self, election):
        return tuple(item for item in self.bias if item.election == election)

    def recent_poll_counts(self):
        counts = {}
        for item in self.bias:
            party = canonical_party(item.party)
            overall_key = (item.election, "all", party)
            for pollster, count in item.recent_poll_counts.items():
                # The legacy reducer omitted pollsters outside the recent
                # window entirely. Retain that behaviour: a zero count must
                # not create a zero-weight pseudo-observation for bias.
                if count <= 0:
                    continue
                counts[(item.election, pollster, party)] = count
                counts[overall_key] = counts.get(overall_key, 0) + count
        return counts


def _election(code, context):
    try:
        return ElectionCode(int(code[:4]), code[4:])
    except (TypeError, ValueError) as error:
        raise ConfigError("{} has invalid election code {!r}.".format(context, code)) from error


def _compact_path(path):
    return (
        path.parent.name == COMPACT_SUMMARY_DIRECTORY
        and path.suffix.casefold() == ".csv"
    )


def _finite_float(value, context):
    return calibration_summary._finite_float(value, context)


def _nonnegative_count(value, context):
    parsed = _finite_float(value, context)
    if parsed < 0 or int(parsed) != parsed:
        raise ConfigError("{} is not a non-negative integer.".format(context))
    return int(parsed)


def _read_compact_summary(path):
    """Read one complete compact election unit without legacy file parsing."""

    try:
        with path.open(newline="", encoding="utf-8-sig") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames is None:
                raise ConfigError("{} lacks a summary header.".format(path))
            missing = [
                field
                for field in calibration_summary.SUMMARY_FIELDS
                if field not in reader.fieldnames
            ]
            if missing:
                raise ConfigError(
                    "{} lacks summary column(s): {}.".format(
                        path, ", ".join(missing)
                    )
                )
            rows = list(reader)
    except OSError as error:
        raise ConfigError("could not read {}: {}".format(path, error)) from error
    if not rows:
        raise ConfigError("{} contains no calibration evidence.".format(path))

    expected_election = path.stem
    leave_one_out = []
    trends = {}
    pollsters = {}
    for row_number, row in enumerate(rows, start=2):
        context = "{}:{}".format(path, row_number)
        if row["schema_version"] != calibration_summary.SCHEMA_VERSION:
            raise ConfigError(
                "{} has unsupported calibration summary schema {}.".format(
                    context, row["schema_version"]
                )
            )
        election = row["election"]
        if election != expected_election:
            raise ConfigError(
                "{} has election {} but filename specifies {}.".format(
                    context, election, expected_election
                )
            )
        party = row["party"]
        if not party:
            raise ConfigError("{} has an empty party.".format(context))
        record_type = row["record_type"]
        if record_type == calibration_summary.RECORD_LEAVE_ONE_OUT:
            pollster = row["pollster"]
            if not pollster:
                raise ConfigError("{} has an empty pollster.".format(context))
            leave_one_out.append(
                LeaveOneOutEvidence(
                    _election(election, context),
                    party,
                    pollster,
                    _finite_float(row["weighted_abs_error"], context + " error"),
                    _finite_float(row["error_weight"], context + " weight"),
                )
            )
            continue
        key = (election, party)
        if record_type == calibration_summary.RECORD_BIAS_TREND:
            if key in trends:
                raise ConfigError("{} repeats a bias trend.".format(context))
            trends[key] = _finite_float(
                row["final_trend_median"], context + " trend median"
            )
            continue
        if record_type == calibration_summary.RECORD_BIAS_POLLSTER:
            pollster = row["pollster"]
            if not pollster:
                raise ConfigError("{} has an empty pollster.".format(context))
            party_pollsters = pollsters.setdefault(key, {})
            if pollster in party_pollsters:
                raise ConfigError(
                    "{} repeats a bias pollster {}.".format(context, pollster)
                )
            party_pollsters[pollster] = (
                _finite_float(
                    row["new_house_effect_median"],
                    context + " house-effect median",
                ),
                _nonnegative_count(
                    row["recent_poll_count"], context + " recent poll count"
                ),
            )
            continue
        raise ConfigError(
            "{} has unknown calibration record type {!r}.".format(
                context, record_type
            )
        )

    if set(trends) != set(pollsters):
        raise ConfigError(
            "{} has incomplete bias evidence: trend and pollster parties differ."
            .format(path)
        )
    if not trends:
        raise ConfigError(
            "{} has incomplete bias evidence: no complete party bundles."
            .format(path)
        )
    bias = []
    for key in sorted(trends):
        party_pollsters = pollsters[key]
        if not party_pollsters:
            raise ConfigError("{} has no bias pollsters for {}.".format(path, key[1]))
        bias.append(
            BiasEvidence(
                _election(key[0], str(path)),
                key[1],
                trends[key],
                {
                    pollster: values[0]
                    for pollster, values in party_pollsters.items()
                },
                {
                    pollster: values[1]
                    for pollster, values in party_pollsters.items()
                },
            )
        )
    return leave_one_out, bias


def _read_legacy_files(paths):
    """Adapt detailed historical files into the same typed evidence format."""

    leave_one_out = []
    bias_files = {}
    for path in paths:
        if path.name.startswith("calib_"):
            try:
                record = calibration_summary.read_leave_one_out(path)
            except calibration_summary.CalibrationSummaryError as error:
                raise ConfigError(str(error)) from error
            leave_one_out.append(
                LeaveOneOutEvidence(
                    _election(record.election, str(path)),
                    record.party,
                    record.pollster,
                    record.weighted_abs_error,
                    record.error_weight,
                )
            )
            continue
        if not (
            path.name.startswith("fp_")
            and path.name.endswith("_biascal.csv")
        ):
            continue
        try:
            kind, election, party = calibration_summary._parse_bias_filename(path)
        except calibration_summary.CalibrationSummaryError as error:
            raise ConfigError(str(error)) from error
        files = bias_files.setdefault((election, party), {})
        if kind in files:
            raise ConfigError("{} duplicates {} bias evidence.".format(path, kind))
        files[kind] = path

    bias = []
    for (election, party), files in sorted(bias_files.items()):
        required = {"trend", "polls", "house_effects"}
        missing = required - set(files)
        if missing:
            raise ConfigError(
                "{} {} lacks bias file(s): {}.".format(
                    election, party, ", ".join(sorted(missing))
                )
            )
        try:
            _, _, trend_median = calibration_summary.read_final_trend_median(
                files["trend"]
            )
            _, _, house_effects = calibration_summary.read_new_house_effects(
                files["house_effects"]
            )
            _, _, recent_counts = calibration_summary.read_recent_poll_counts(
                files["polls"]
            )
        except calibration_summary.CalibrationSummaryError as error:
            raise ConfigError(str(error)) from error
        if set(house_effects) != set(recent_counts):
            raise ConfigError(
                "{} {} has mismatched bias pollster evidence.".format(
                    election, party
                )
            )
        bias.append(
            BiasEvidence(
                _election(election, str(files["trend"])),
                party,
                trend_median,
                house_effects,
                recent_counts,
            )
        )
    return leave_one_out, bias


def load_calibration_evidence(paths):
    """Load compact summaries first and legacy files only when no summary exists."""

    paths = [Path(path) for path in paths]
    compact_paths = sorted(path for path in paths if _compact_path(path))
    legacy_paths = [path for path in paths if not _compact_path(path)]
    legacy_by_election = {}
    for path in legacy_paths:
        election = calibration_summary._filename_election_hint(path)
        if election is not None:
            legacy_by_election.setdefault(election, []).append(path)

    compact_elections = set()
    leave_one_out = []
    bias = []
    for path in compact_paths:
        election = path.stem
        if election in compact_elections:
            raise ConfigError("duplicate compact calibration summary {}.".format(path.stem))
        try:
            compact_loo, compact_bias = _read_compact_summary(path)
        except ConfigError:
            # A summary is a cache of detailed calibration evidence. If an
            # interrupted or malformed compact file has left the original
            # inputs available, retain the established legacy result instead
            # of making an otherwise usable analysis impossible.
            if election in legacy_by_election:
                continue
            raise
        compact_elections.add(election)
        leave_one_out.extend(compact_loo)
        bias.extend(compact_bias)

    for election, paths_for_election in legacy_by_election.items():
        if election in compact_elections:
            continue
        legacy_loo, legacy_bias = _read_legacy_files(paths_for_election)
        leave_one_out.extend(legacy_loo)
        bias.extend(legacy_bias)

    if not leave_one_out and not bias:
        raise ConfigError("no recognised calibration evidence was loaded.")
    return CalibrationEvidence(tuple(leave_one_out), tuple(bias))

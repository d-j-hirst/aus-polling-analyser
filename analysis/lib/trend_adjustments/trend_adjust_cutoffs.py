"""Load and interpolate consolidated historical cutoff trends.

Parent: trend_adjust.py uses this module to supply historical point-in-time
trend inputs for fitting forecast adjustments.

Main functions:
* ``triangular_root`` maps actual poll endpoints back onto the triangular
  cutoff schedule used to space historical fits.
* ``CutoffTrendData`` loads, validates and interpolates consolidated cutoff
  rows for point-in-time trend access.
"""

import bisect
import csv
import math
from pathlib import Path


PERCENTILE_COLUMNS = tuple(
    "{}%".format(percentile) for percentile in range(101)
)
KEY_COLUMNS = (
    "ScheduledCutoffDays",
    "PollTrendEndDays",
    "Party",
    "StanSeed",
)
COMPLETE_MARKER = "#COMPLETE"


class CutoffTrendError(ValueError):
    """Raised when consolidated cutoff data are absent or malformed."""


# Cutoff-schedule arithmetic

def triangular_root(days):
    """Return the continuous index whose triangular number is ``days``."""

    if days < 0:
        raise CutoffTrendError(
            "cutoff days cannot be negative: {}".format(days)
        )
    return 0.5 * (math.sqrt(8.0 * days + 1.0) - 1.0)


def _nonnegative_integer(value, field, path, line_number):
    try:
        parsed = int(value)
    except ValueError as error:
        raise CutoffTrendError(
            "{}:{} has invalid {} '{}'".format(
                path, line_number, field, value
            )
        ) from error
    if parsed < 0:
        raise CutoffTrendError(
            "{}:{} has negative {} {}".format(
                path, line_number, field, parsed
            )
        )
    return parsed


# Consolidated cutoff loading, validation and interpolation

class CutoffTrendData:
    """Posterior percentiles indexed by party and actual poll endpoint."""

    def __init__(self, path):
        self.path = Path(path)
        self._values = {}
        self._endpoints = {}
        self._load()

    def _load(self):
        try:
            source = self.path.open(
                newline="", encoding="utf-8-sig"
            )
        except FileNotFoundError as error:
            raise CutoffTrendError(
                "missing consolidated cutoff file {}; regenerate it with "
                "fp_model.py --election <election> --cutoff".format(
                    self.path
                )
            ) from error

        with source:
            reader = csv.DictReader(source)
            expected_header = list(KEY_COLUMNS + PERCENTILE_COLUMNS)
            if reader.fieldnames != expected_header:
                raise CutoffTrendError(
                    "{} has an invalid header; expected the four cutoff "
                    "columns followed by 0% through 100%".format(self.path)
                )
            for line_number, row in enumerate(reader, start=2):
                if None in row:
                    raise CutoffTrendError(
                        "{}:{} contains unexpected extra columns".format(
                            self.path, line_number
                        )
                    )
                party = (row["Party"] or "").strip()
                if party == COMPLETE_MARKER:
                    continue
                if not party:
                    raise CutoffTrendError(
                        "{}:{} has no party".format(
                            self.path, line_number
                        )
                    )
                _nonnegative_integer(
                    row["ScheduledCutoffDays"],
                    "ScheduledCutoffDays",
                    self.path,
                    line_number,
                )
                endpoint = _nonnegative_integer(
                    row["PollTrendEndDays"],
                    "PollTrendEndDays",
                    self.path,
                    line_number,
                )
                percentiles = []
                for column in PERCENTILE_COLUMNS:
                    try:
                        value = float(row[column])
                    except (TypeError, ValueError) as error:
                        raise CutoffTrendError(
                            "{}:{} has invalid {} value '{}'".format(
                                self.path,
                                line_number,
                                column,
                                row[column],
                            )
                        ) from error
                    if not math.isfinite(value):
                        raise CutoffTrendError(
                            "{}:{} has non-finite {} value".format(
                                self.path, line_number, column
                            )
                        )
                    percentiles.append(value)
                party_values = self._values.setdefault(party, {})
                if endpoint in party_values:
                    raise CutoffTrendError(
                        "{}:{} duplicates party {} at actual endpoint {}"
                        .format(
                            self.path, line_number, party, endpoint
                        )
                    )
                party_values[endpoint] = tuple(percentiles)

        if not self._values:
            raise CutoffTrendError(
                "{} contains no cutoff trend rows".format(self.path)
            )
        self._endpoints = {
            party: tuple(sorted(values))
            for party, values in self._values.items()
        }

    def require_parties(self, parties):
        missing = sorted(set(parties) - set(self._values))
        if missing:
            raise CutoffTrendError(
                "{} lacks configured party trend(s): {}".format(
                    self.path, ", ".join(missing)
                )
            )

    def value_at(self, party, day, percentile, default_value=None):
        if day < 0:
            return default_value
        if percentile < 0 or percentile > 100:
            raise CutoffTrendError(
                "percentile must be from 0 to 100, received {}".format(
                    percentile
                )
            )
        if party not in self._values:
            raise CutoffTrendError(
                "{} has no cutoff trend for {}".format(
                    self.path, party
                )
            )

        endpoints = self._endpoints[party]
        insertion = bisect.bisect_left(endpoints, day)
        if insertion < len(endpoints) and endpoints[insertion] == day:
            return self._values[party][day][percentile]
        if insertion == len(endpoints):
            # Before this party's first poll, matching the former absence of
            # a complete-cycle trend value at long forecast horizons.
            return default_value
        if insertion == 0:
            # No new information arrived between the latest poll and election
            # day, so retain that latest available estimate.
            return self._values[party][endpoints[0]][percentile]

        lower_endpoint = endpoints[insertion - 1]
        upper_endpoint = endpoints[insertion]
        lower_root = triangular_root(lower_endpoint)
        upper_root = triangular_root(upper_endpoint)
        requested_root = triangular_root(day)
        upper_weight = (
            (requested_root - lower_root)
            / (upper_root - lower_root)
        )
        lower_value = self._values[
            party
        ][lower_endpoint][percentile]
        upper_value = self._values[
            party
        ][upper_endpoint][percentile]
        return (
            lower_value * (1.0 - upper_weight)
            + upper_value * upper_weight
        )

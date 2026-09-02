"""Load the election catalogues used for operational pipeline selection.

This module does not decide model dependencies. It validates the authored
catalogue files and distinguishes open future terms from the subset selected
for routine generation.

Main functions:
* ``load_future_elections`` validates ordered future-election status rows.
* ``configured_future_elections`` and ``open_future_elections`` return every
  future term, including deliberately inactive terms.
* ``active_future_elections`` and ``inactive_future_elections`` apply the
  operational status used by audits, plans and archive construction.
* ``historical_elections`` returns completed terms from the polled catalogue.
"""

import csv
import re
from dataclasses import dataclass
from pathlib import Path

from election_code import ElectionCode


DATA_DIRECTORY = Path(__file__).resolve().parent / "Data"
FUTURE_ELECTIONS_PATH = DATA_DIRECTORY / "future-elections.csv"
HISTORICAL_ELECTIONS_PATH = DATA_DIRECTORY / "polled-elections.csv"
ACTIVE = "active"
INACTIVE = "inactive"
VALID_STATUSES = {ACTIVE, INACTIVE}
REGION_PATTERN = re.compile(r"[a-z]+")


class ElectionCatalogueError(ValueError):
    """Raised when an authored election catalogue is malformed."""


@dataclass(frozen=True)
class FutureElection:
    """One configured future election and its operational status."""

    code: ElectionCode
    status: str


def _read_rows(path):
    path = Path(path)
    try:
        source = path.open(newline="", encoding="utf-8-sig")
    except OSError as error:
        raise ElectionCatalogueError(
            "could not read election catalogue {}: {}".format(path, error)
        ) from error
    with source:
        return [
            (line_number, row)
            for line_number, row in enumerate(csv.reader(source), start=1)
            if row
        ]


def _parse_code(path, line_number, year_value, region_value):
    year_text = year_value.strip()
    region = region_value.strip().casefold()
    try:
        year = int(year_text)
    except ValueError as error:
        raise ElectionCatalogueError(
            "{}:{} has invalid election year {!r}".format(
                path, line_number, year_value
            )
        ) from error
    if len(year_text) != 4 or year < 1900 or not REGION_PATTERN.fullmatch(region):
        raise ElectionCatalogueError(
            "{}:{} has invalid election identifier {},{}".format(
                path, line_number, year_value, region_value
            )
        )
    return ElectionCode(year, region)


def load_future_elections(path=FUTURE_ELECTIONS_PATH):
    """Return ordered future-election rows after strict validation."""

    path = Path(path)
    elections = []
    seen = set()
    for line_number, row in _read_rows(path):
        if len(row) != 3:
            raise ElectionCatalogueError(
                "{}:{} must contain year, region and active/inactive status"
                .format(path, line_number)
            )
        code = _parse_code(path, line_number, row[0], row[1])
        status = row[2].strip().casefold()
        if status not in VALID_STATUSES:
            raise ElectionCatalogueError(
                "{}:{} has unsupported status {!r}; expected active or "
                "inactive".format(path, line_number, row[2])
            )
        if code in seen:
            raise ElectionCatalogueError(
                "{}:{} duplicates election {}".format(
                    path, line_number, code.short()
                )
            )
        seen.add(code)
        elections.append(FutureElection(code=code, status=status))
    return tuple(elections)


def _future_codes(path, status=None):
    return {
        election.code.short()
        for election in load_future_elections(path)
        if status is None or election.status == status
    }


def configured_future_elections(path=FUTURE_ELECTIONS_PATH):
    """Return every configured future election."""

    return _future_codes(path)


def open_future_elections(path=FUTURE_ELECTIONS_PATH):
    """Return every future term whose eventual result is still unknown."""

    return configured_future_elections(path)


def active_future_elections(path=FUTURE_ELECTIONS_PATH):
    """Return future elections selected for routine operational work."""

    return _future_codes(path, ACTIVE)


def inactive_future_elections(path=FUTURE_ELECTIONS_PATH):
    """Return configured future elections excluded from routine work."""

    return _future_codes(path, INACTIVE)


def historical_elections(path=HISTORICAL_ELECTIONS_PATH):
    """Return completed elections from the strict two-column catalogue."""

    path = Path(path)
    elections = set()
    for line_number, row in _read_rows(path):
        if len(row) != 2:
            raise ElectionCatalogueError(
                "{}:{} must contain year and region".format(path, line_number)
            )
        code = _parse_code(path, line_number, row[0], row[1])
        if code.short() in elections:
            raise ElectionCatalogueError(
                "{}:{} duplicates election {}".format(
                    path, line_number, code.short()
                )
            )
        elections.add(code.short())
    return elections

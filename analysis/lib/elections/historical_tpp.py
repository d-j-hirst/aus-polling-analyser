"""Supply official TPP history without changing the actual TCP contest.

Seat swing analysis normally uses a Labor/Coalition TCP count. Where the final
pair contains a minor party, the AEC's separate statistical TPP count supplies
the comparable share and redistribution-adjusted swing instead. Candidate and
incumbency analysis must continue to use the original TCP results.
"""

import csv
from dataclasses import dataclass
import math
import json
from typing import Optional

from lib.paths import ANALYSIS_DIRECTORY


HISTORY_PATH = ANALYSIS_DIRECTORY / 'Data' / 'federal-tpp-history.csv'
CORRECTIONS_PATH = ANALYSIS_DIRECTORY / 'Data' / 'historical-tpp-corrections.json'
COALITION_PARTIES = frozenset({'Liberal', 'National', 'Liberal National', 'Country Liberal'})


@dataclass(frozen=True)
class TppResult:
    percent: float
    swing: Optional[float]


def usable_swing(percent, swing):
    """A missing comparison must not become a swing from zero or 100 percent."""
    if swing is None:
        return None
    if not math.isfinite(swing):
        raise ValueError('Non-finite historical TPP swing.')
    return swing if 0 < percent - swing < 100 else None


def apply_reviewed_corrections(year, results, path=CORRECTIONS_PATH):
    """Apply sourced input repairs, without changing valid current vote shares."""
    for row in json.loads(path.read_text(encoding='utf-8')):
        if row['year'] != year or row['seat'] not in results:
            continue
        current = results[row['seat']]
        if abs(current.percent - row['alp_percent']) > .011:
            raise ValueError(f"Historical TPP correction no longer matches {year} {row['seat']}")
        results[row['seat']] = TppResult(current.percent,
            round(current.percent - row['previous_alp_percent'], 6))
    return results


def parse_aec_tpp(text):
    """Read an AEC division download; its swing follows the FIRST party column.

    The AEC reverses the column order between elections. PartyAb describes the
    TPP leader, not the swing's sign. Using that field, or subtracting consecutive
    shares across a redistribution, would silently reverse or distort swings.
    """
    rows = csv.DictReader(text.lstrip('\ufeff').splitlines()[1:])
    labor = 'Australian Labor Party Percentage'
    coalition = 'Liberal/National Coalition Percentage'
    fields = rows.fieldnames or []
    if not all(key in fields for key in ('DivisionNm', labor, coalition, 'Swing')):
        raise ValueError('Missing required AEC division TPP columns.')
    sign = 1 if fields.index(labor) < fields.index(coalition) else -1
    result = {}
    for row in rows:
        name = row['DivisionNm']
        percent = float(row[labor])
        raw_swing = row['Swing'].strip()
        swing = usable_swing(percent, sign * float(raw_swing)) if raw_swing else None
        if not name or name in result or not 0 <= percent <= 100:
            raise ValueError(f'Invalid or duplicate AEC TPP division: {name}')
        if abs(percent + float(row[coalition]) - 100) > .02:
            raise ValueError(f'AEC TPP percentages do not sum to 100: {name}')
        result[name] = TppResult(percent, swing)
    return result


def load_tpp_history(path=HISTORY_PATH):
    """Load the small, reviewed input table; routine analysis never downloads."""
    result = {}
    with path.open(encoding='utf-8', newline='') as stream:
        for row in csv.DictReader(stream):
            key = (int(row['year']), row['seat'])
            percent = float(row['alp_percent'])
            swing = float(row['alp_swing']) if row['alp_swing'].strip() else None
            value = TppResult(percent, usable_swing(percent, swing))
            if key in result or not 0 <= value.percent <= 100:
                raise ValueError(f'Invalid historical TPP record: {key}')
            result[key] = value
    return result


def seat_tpp(election, seat, history):
    """Prefer the established classic-count path; fill missing TPP from AEC."""
    if seat is None or election is None:
        return None
    pair = seat.tcp
    if len(pair) == 2:
        labor = next((candidate for candidate in pair if candidate.party == 'Labor'), None)
        coalition = next((candidate for candidate in pair if candidate.party in COALITION_PARTIES), None)
        if labor is not None and coalition is not None and pair[0].swing is not None:
            swing = pair[0].swing if pair[0].party == 'Labor' else -pair[0].swing
            swing = usable_swing(labor.percent, swing)
            if swing is not None:
                return TppResult(labor.percent, swing)
    return history.get((election.year(), seat.name)) if election.region() == 'fed' else None

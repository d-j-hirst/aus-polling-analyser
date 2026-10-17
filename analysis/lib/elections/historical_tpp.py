"""Supply official TPP history without changing the actual TCP contest.

Seat swing analysis normally uses a Labor/Coalition TCP count. Where the final
pair contains a minor party, the AEC's separate statistical TPP count supplies
the comparable share and redistribution-adjusted swing instead. Candidate and
incumbency analysis must continue to use the original TCP results.
"""

import csv
from dataclasses import dataclass
import math

from lib.paths import ANALYSIS_DIRECTORY


HISTORY_PATH = ANALYSIS_DIRECTORY / 'Data' / 'federal-tpp-history.csv'
COALITION_PARTIES = frozenset({'Liberal', 'National', 'Liberal National', 'Country Liberal'})


@dataclass(frozen=True)
class TppResult:
    percent: float
    swing: float


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
        swing = sign * float(row['Swing'])
        if not name or name in result or not 0 <= percent <= 100 or not math.isfinite(swing):
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
            value = TppResult(float(row['alp_percent']), float(row['alp_swing']))
            if key in result or not 0 <= value.percent <= 100 or not math.isfinite(value.swing):
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
            return TppResult(labor.percent, swing)
    return history.get((election.year(), seat.name)) if election.region() == 'fed' else None

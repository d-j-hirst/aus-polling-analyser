"""Estimate local seat TPP uncertainty using only elections before a forecast.

Seat and generic scales use squared errors on the same transformed share scale.
Recent elections receive more weight. Missing seat history is filled to four
observations, followed by one directionless allowance for political disruption.
This module neither fits Stan models nor writes forecast simulation results.
"""

import csv
import math
from collections import defaultdict
from dataclasses import dataclass

from lib.elections.historical_tpp import load_tpp_history, seat_tpp
from lib.paths import ANALYSIS_DIRECTORY

FLOOR = 2.0
HALF_LIFE = 4.0
MINIMUM_HISTORY = 4
ELASTICITY_SHRINKAGE = .38


@dataclass(frozen=True)
class Observation:
    year: int
    jurisdiction: str
    region: str
    seat: str
    swing: float
    transformed_swing: float


def transformed_change(current, swing):
    """Use the reported swing's redistributed baseline, never old boundaries."""
    if swing is None or not math.isfinite(current) or not math.isfinite(swing):
        return None
    previous = current - swing
    if not 0 < current < 100 or not 0 < previous < 100:
        return None
    transform = lambda p: 25 * math.log(p / (100 - p))
    return transform(current) - transform(previous)


def collect_observations(elections):
    """Read checked local history, retaining unavailable comparisons in the audit."""
    with (ANALYSIS_DIRECTORY / 'Data/seat-regions.csv').open(encoding='utf-8') as stream:
        regions = {(r[0], r[1]): r[2] for r in csv.reader(stream)}
    history = load_tpp_history()
    observations, excluded = [], []
    for election, results in elections.items():
        for seat in results.seat_results:
            # These old names do not describe a stable multi-seat region. Keep
            # the established historical exclusions used by the mean model.
            if election.region() == 'fed' and (seat.name == 'Northern Territory' or
                    (seat.name == 'Fraser' and election.year() < 2019)):
                continue
            result = seat_tpp(election, seat, history)
            change = transformed_change(result.percent, result.swing) if result else None
            if change is None:
                excluded.append(dict(year=election.year(), jurisdiction=election.region(),
                                     seat=seat.name, reason='No valid comparable TPP swing'))
                continue
            observations.append(Observation(election.year(), election.region(),
                regions.get((seat.name, election.region()), 'none'), seat.name, result.swing, change))
    return observations, excluded


def historical_comparisons(observations, target_year):
    """Build regional predictors after removing future and unusable observations."""
    previous = [r for r in observations if r.year < target_year]
    regions = defaultdict(list)
    years = defaultdict(set)
    for r in previous:
        regions[(r.jurisdiction, r.year, r.region)].append(r.swing)
        years[r.jurisdiction].add(r.year)
    ages = {jurisdiction: {year: age for age, year in enumerate(sorted(values, reverse=True))}
            for jurisdiction, values in years.items()}
    groups = defaultdict(list)
    for r in previous:
        regional = regions[(r.jurisdiction, r.year, r.region)]
        # C++ adds elasticity * raw regional swing to transformed seat share.
        # Match that existing interface while measuring residuals in z units.
        groups[(r.jurisdiction, r.seat, r.region)].append(dict(
            year=r.year, x=sum(regional)/len(regional), y=r.transformed_swing,
            weight=.5 ** (ages[r.jurisdiction][r.year] / HALF_LIFE)))
    return groups


def fit_seat(rows):
    """Measure errors around the shrunken elasticity actually used in forecasts."""
    square = sum(r['x'] ** 2 for r in rows)
    elasticity = 1.0
    if len(rows) >= 3 and square > 0:
        fitted = sum(r['x'] * r['y'] for r in rows) / square
        elasticity += ELASTICITY_SHRINKAGE * (fitted - 1)
    # Tiny historical samples can produce a reversed response to the region.
    # Such a fit is not a reliable elasticity; retain the ordinary unit response
    # and include the unexplained movement in volatility instead.
    if elasticity <= 0:
        elasticity = 1.0
    residuals = [dict(**r, residual=r['y'] - elasticity * r['x']) for r in rows]
    return elasticity, residuals


def pooled_moments(groups):
    """Give each election equal total weight, then apply the recency weights."""
    counts = defaultdict(int)
    for key, (_, rows) in groups.items():
        for r in rows:
            counts[(key[0], r['year'])] += 1
    total = second = fourth = 0.0
    for key, (_, rows) in groups.items():
        for r in rows:
            weight = r['weight'] / counts[(key[0], r['year'])]
            total += weight
            second += weight * r['residual'] ** 2
            fourth += weight * r['residual'] ** 4
    if total == 0:
        raise ValueError('No earlier election evidence for the generic seat volatility.')
    second /= total
    generic = max(FLOOR, math.sqrt(second))
    # Preserve heavier historical tails, with a normal minimum. These moments
    # also obey the target cutoff; no future election sets the distribution shape.
    kurtosis = max(3.0, fourth / total / second ** 2) if second > 0 else 3.0
    return generic, kurtosis


def regularised_sd(rows, generic):
    """Fill actual observation count to four; add one error of magnitude 2G."""
    padding = max(0, MINIMUM_HISTORY - len(rows))
    weight = sum(r['weight'] for r in rows)
    squared_error = sum(r['weight'] * r['residual'] ** 2 for r in rows)
    variance = (squared_error + padding * generic ** 2 + (2 * generic) ** 2) / (weight + padding + 1)
    return max(FLOOR, math.sqrt(variance))


def select_pool(fits, jurisdiction):
    """Usually pool within the jurisdiction; sparse state history borrows states."""
    own = {key: value for key, value in fits.items() if key[0] == jurisdiction}
    years = {r['year'] for _, rows in own.values() for r in rows}
    if jurisdiction == 'fed' or len(years) >= MINIMUM_HISTORY:
        return own, jurisdiction
    return {key: value for key, value in fits.items() if key[0] != 'fed'}, 'pooled states'


def seat_parameters(fits, jurisdiction, generic):
    """Apply the shared regularisation rule and retain each seat's sample audit."""
    seats = []
    for (region, name, area), (elasticity, rows) in sorted(fits.items()):
        if region != jurisdiction:
            continue
        seats.append(dict(name=name, region=area, elasticity=elasticity,
                          sd=regularised_sd(rows, generic), observations=len(rows),
                          weight=sum(r['weight'] for r in rows),
                          years=sorted(r['year'] for r in rows)))
    return seats


def calculate(observations, election):
    """Outline one chronological calibration; every working row is past-only."""
    year, jurisdiction = int(election[:4]), election[4:]
    comparisons = historical_comparisons(observations, year)
    fits = {key: fit_seat(rows) for key, rows in comparisons.items()}
    pool, source = select_pool(fits, jurisdiction)
    generic, kurtosis = pooled_moments(pool)
    seats = seat_parameters(fits, jurisdiction, generic)
    return dict(election=election, generic_sd=generic, fallback_sd=regularised_sd([], generic),
                kurtosis=kurtosis, floor=FLOOR, half_life_elections=HALF_LIFE,
                minimum_history=MINIMUM_HISTORY, disruption_sd_multiplier=2,
                generic_source=source,
                training_years=sorted({r['year'] for _, rows in pool.values() for r in rows}), seats=seats)

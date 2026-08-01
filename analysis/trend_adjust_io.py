"""Serialize, validate and publish trend-adjustment outputs.

Parent: trend_adjust.py owns the workflow; this module writes and validates
the fundamentals and adjustment files produced by its calculation stages.
"""

import math
import os
from pathlib import Path

from scipy.interpolate import UnivariateSpline

from poll_transform import clamp
from trend_adjust_cutoffs import triangular_root
from trend_adjust_data import (
    ADJUSTMENT_PARAMETER_COUNT,
    TREND_ADJUSTMENT_LEVELS,
    TrendAdjustmentDataError,
)


def write_smoothed_series(
    config,
    label,
    values_by_day,
    output_file,
    force_monotone=False,
    bounds=(-math.inf, math.inf),
    prefix=None,
):
    """Spline sparse triangular-day values onto every intervening day."""

    if len(values_by_day) < 4:
        raise ValueError(f'{label} requires at least four smoothing points')
    x_orig, y = zip(*sorted(values_by_day.items()))
    x = range(0, len(x_orig))
    total_days = x_orig[-1]
    w = [100 if a == 0 else 1 for a in x]
    spline = UnivariateSpline(x=x, y=y, w=w, s=100)
    full_spline = spline(x)
    full_spline = {x_orig[a]: b for a, b in enumerate(full_spline)}
    if config.show_parameters:
        joined = '\n'.join([f'{a}: {b:.4f}' for a, b in full_spline.items()])
        print(f'{label} smoothed: {joined}\n')
    # Spline coordinates are triangular-sequence indexes, so convert each
    # calendar-day horizon back into that continuous coordinate system.
    daily_x = [triangular_root(day) for day in range(total_days + 1)]
    daily_spline = list(spline(daily_x))
    if not all(math.isfinite(value) for value in daily_spline):
        level_description = (
            '' if prefix is None else f' at target trend {prefix:g}')
        raise ValueError(
            f'{label} produced a non-finite adjustment'
            f'{level_description}')
    if force_monotone:
        if daily_spline[len(daily_spline) - 1] > daily_spline[0]:
            for day in range(0, len(daily_spline) - 1):
                new_val = max(daily_spline[day + 1], daily_spline[day])
                daily_spline[day + 1] = new_val
        else:
            for day in range(0, len(daily_spline) - 1):
                new_val = min(daily_spline[day + 1], daily_spline[day])
                daily_spline[day + 1] = new_val
    for day in range(0, len(daily_spline)):
        daily_spline[day] = clamp(daily_spline[day], bounds[0], bounds[1])
    output = [f'{a:.4f}' for a in daily_spline]
    if prefix is not None:
        output.insert(0, f'{prefix:g}')
    output_file.write(','.join(output) + '\n')


def save_fundamentals(results, output_directory='./Fundamentals'):
    """Write one compact party/prediction file for each target election."""

    output_directory = Path(output_directory)
    output_directory.mkdir(parents=True, exist_ok=True)
    output_paths = {}
    for election, election_data in results.items():
        non_finite = [
            party for party, prediction in election_data.items()
            if not math.isfinite(prediction)
        ]
        if non_finite:
            raise TrendAdjustmentDataError(
                f'{election.short()} produced non-finite fundamentals for '
                f'{", ".join(non_finite)}'
            )
        filename = output_directory / (
            f'fundamentals_{election.year()}{election.region()}.csv'
        )
        with open(filename, 'w', encoding='utf-8', newline='') as f:
            for party, prediction in election_data.items():
                f.write(f'{party},{prediction}\n')
        output_paths[election] = str(filename)
    return output_paths


def save_party_data(
    config,
    party_data_by_level,
    exclude,
    party_group,
    output_directory='./Adjustments',
):
    output_directory = Path(output_directory)
    output_directory.mkdir(parents=True, exist_ok=True)
    filename = output_directory / (
        f'adjust_{exclude.year()}{exclude.region()}_{party_group}.csv'
    )
    with open(filename, 'w', encoding='utf-8', newline='') as f:
        # Each block has one row per adjustment parameter. The transformed
        # support anchor is repeated in column one so files remain readable
        # and each row can be inspected independently.
        for target_trend in TREND_ADJUSTMENT_LEVELS:
            party_data = party_data_by_level[target_trend]
            write_smoothed_series(config, 'Poll Bias',
                                  party_data.poll_biases, f,
                                  prefix=target_trend)
            write_smoothed_series(config, 'Fundamentals Bias',
                                  party_data.fundamentals_biases, f,
                                  prefix=target_trend)
            write_smoothed_series(config, 'Mixed Bias',
                                  party_data.mixed_biases, f,
                                  prefix=target_trend)
            write_smoothed_series(
                config, 'Lower Error', party_data.lower_rmses, f,
                force_monotone=True, bounds=(0, math.inf),
                prefix=target_trend)
            write_smoothed_series(
                config, 'Upper Error', party_data.upper_rmses, f,
                force_monotone=True, bounds=(0, math.inf),
                prefix=target_trend)
            write_smoothed_series(
                config, 'Lower Kurtosis', party_data.lower_kurtoses, f,
                bounds=(3, math.inf), prefix=target_trend)
            write_smoothed_series(
                config, 'Upper Kurtosis', party_data.upper_kurtoses, f,
                bounds=(3, math.inf), prefix=target_trend)
            write_smoothed_series(
                config, 'Mix factor', party_data.final_mix_factors, f,
                force_monotone=True, bounds=(0, 1),
                prefix=target_trend)
        if config.show_written_files:
            print(f'Wrote parameter data to: {filename}')
    return str(filename)


def load_adjustment_data(filename):
    """Load either a legacy single grid or support-level parameter grids."""

    with open(filename, 'r') as f:
        rows = [
            [float(value) for value in line.strip().split(',')]
            for line in f if line.strip()]
    if any(not math.isfinite(value) for row in rows for value in row):
        raise ValueError(f'{filename} contains a non-finite value')
    if len(rows) == ADJUSTMENT_PARAMETER_COUNT:
        if any(len(row) != len(rows[0]) for row in rows):
            raise ValueError(
                f'{filename} has inconsistent daily value counts')
        return [(None, rows)]
    if not rows or len(rows) % ADJUSTMENT_PARAMETER_COUNT:
        raise ValueError(
            f'{filename} has {len(rows)} rows; expected 8 rows or '
            'a multiple of 8')
    grids = []
    for start in range(0, len(rows), ADJUSTMENT_PARAMETER_COUNT):
        block = rows[start:start + ADJUSTMENT_PARAMETER_COUNT]
        target_trend = block[0][0]
        if any(row[0] != target_trend for row in block):
            raise ValueError(
                f'{filename} has inconsistent trend levels in rows '
                f'{start + 1}-{start + ADJUSTMENT_PARAMETER_COUNT}')
        daily_values = [row[1:] for row in block]
        if any(len(row) != len(daily_values[0])
               for row in daily_values):
            raise ValueError(
                f'{filename} has inconsistent daily value counts')
        grids.append((target_trend, daily_values))
    if any(grids[index][0] >= grids[index + 1][0]
           for index in range(len(grids) - 1)):
        raise ValueError(
            f'{filename} trend levels are not strictly increasing')
    daily_value_count = len(grids[0][1][0])
    if any(
        len(row) != daily_value_count
        for _, daily_rows in grids
        for row in daily_rows
    ):
        raise ValueError(
            f'{filename} has inconsistent daily value counts between '
            'trend levels'
        )
    return grids


def adjustment_parameters_at(grids, transformed_trend, day):
    """Interpolate one day's parameters between transformed support levels."""

    if not grids:
        raise ValueError('adjustment parameter grid is empty')
    if not isinstance(day, int) or day < 0:
        raise ValueError(f'adjustment day must be a nonnegative integer: {day}')
    available_days = len(grids[0][1][0])
    if day >= available_days:
        raise ValueError(
            f'adjustment day {day} is outside the available range '
            f'0-{available_days - 1}'
        )
    if len(grids) == 1:
        return [row[day] for row in grids[0][1]]
    if transformed_trend <= grids[0][0]:
        return [row[day] for row in grids[0][1]]
    if transformed_trend >= grids[-1][0]:
        return [row[day] for row in grids[-1][1]]
    upper_index = next(
        index for index, (target, _) in enumerate(grids)
        if target >= transformed_trend)
    lower_level, lower_rows = grids[upper_index - 1]
    upper_level, upper_rows = grids[upper_index]
    upper_weight = (
        (transformed_trend - lower_level)
        / (upper_level - lower_level))
    return [
        lower_rows[index][day] * (1 - upper_weight)
        + upper_rows[index][day] * upper_weight
        for index in range(len(lower_rows))
    ]


def validate_generated_fundamentals(filename):
    """Validate one newly generated fundamentals file before promotion."""

    if filename is None:
        raise TrendAdjustmentDataError(
            'Trend adjustment did not produce a fundamentals output'
        )
    path = Path(filename)
    if not path.is_file():
        raise TrendAdjustmentDataError(
            f'Expected staged fundamentals output was not created: {path}'
        )
    seen_parties = set()
    with path.open('r', encoding='utf-8') as input_file:
        rows = [line.strip().split(',') for line in input_file if line.strip()]
    if not rows:
        raise TrendAdjustmentDataError(
            f'Staged fundamentals output is empty: {path}'
        )
    for line_number, row in enumerate(rows, start=1):
        if len(row) != 2 or not row[0]:
            raise TrendAdjustmentDataError(
                f'{path} line {line_number} is not a party,value pair'
            )
        if row[0] in seen_parties:
            raise TrendAdjustmentDataError(
                f'{path} contains duplicate party {row[0]}'
            )
        seen_parties.add(row[0])
        try:
            value = float(row[1])
        except ValueError as error:
            raise TrendAdjustmentDataError(
                f'{path} line {line_number} has a non-numeric prediction'
            ) from error
        if not math.isfinite(value):
            raise TrendAdjustmentDataError(
                f'{path} line {line_number} has a non-finite prediction'
            )


def validate_generated_adjustment(config, filename):
    """Validate the shape and support levels of one staged adjustment grid."""

    path = Path(filename)
    if not path.is_file():
        raise TrendAdjustmentDataError(
            f'Expected staged adjustment output was not created: {path}'
        )
    try:
        grids = load_adjustment_data(path)
    except (OSError, ValueError) as error:
        raise TrendAdjustmentDataError(str(error)) from error
    levels = [level for level, _ in grids]
    if tuple(levels) != tuple(TREND_ADJUSTMENT_LEVELS):
        raise TrendAdjustmentDataError(
            f'{path} has support levels {levels}; expected '
            f'{TREND_ADJUSTMENT_LEVELS}'
        )
    expected_days = max(config.days) + 1
    if any(
        len(row) != expected_days
        for _, parameter_rows in grids
        for row in parameter_rows
    ):
        raise TrendAdjustmentDataError(
            f'{path} does not contain {expected_days} daily values per row'
        )


def promote_staged_outputs(
    fundamentals_output,
    adjustment_outputs,
    party_groups,
    fundamentals_directory='./Fundamentals',
    adjustments_directory='./Adjustments',
):
    """Replace canonical files only after every staged output is available."""

    if fundamentals_output is None:
        raise TrendAdjustmentDataError(
            'Trend adjustment did not produce a fundamentals output'
        )
    if set(adjustment_outputs) != set(party_groups.groups):
        missing = sorted(set(party_groups.groups) - set(adjustment_outputs))
        raise TrendAdjustmentDataError(
            'Trend adjustment did not produce every party group: '
            + ', '.join(missing)
        )

    staged_paths = [Path(fundamentals_output)] + [
        Path(adjustment_outputs[group]) for group in party_groups.groups
    ]
    missing_paths = [str(path) for path in staged_paths if not path.is_file()]
    if missing_paths:
        raise TrendAdjustmentDataError(
            'Staged outputs disappeared before promotion: '
            + ', '.join(missing_paths)
        )

    fundamentals_directory = Path(fundamentals_directory)
    adjustments_directory = Path(adjustments_directory)
    fundamentals_directory.mkdir(parents=True, exist_ok=True)
    adjustments_directory.mkdir(parents=True, exist_ok=True)
    canonical_fundamentals = fundamentals_directory / Path(
        fundamentals_output
    ).name
    canonical_adjustments = {
        group: adjustments_directory / Path(path).name
        for group, path in adjustment_outputs.items()
    }

    # os.replace is atomic for each file on the same filesystem. Staging all
    # files first keeps lengthy calculation failures away from canonical data;
    # the final promotion window consists only of these quick replacements.
    os.replace(fundamentals_output, canonical_fundamentals)
    for group in party_groups.groups:
        os.replace(adjustment_outputs[group], canonical_adjustments[group])
    return (
        str(canonical_fundamentals),
        {group: str(path) for group, path in canonical_adjustments.items()},
    )

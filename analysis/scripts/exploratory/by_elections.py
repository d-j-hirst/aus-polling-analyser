"""Exploratory analysis of historical by-election swings.

This report is not part of the production forecast pipeline. The production
by-election modifier is estimated by election_analysis_seats.py as one term in
a multivariate seat-swing regression.
"""

import sys
from pathlib import Path

import numpy as np
import pandas as pd
import statsmodels.api as sm
import statsmodels.formula.api as smf


DATA_FILE = Path('./Data/by-elections.csv')
REQUIRED_COLUMNS = (
    'Government',
    'By-elec swing',
    'Eventual swing',
    'Statewide swing',
    'Party change',
)
NUMERIC_COLUMNS = ('By-elec swing', 'Eventual swing', 'Statewide swing')


class ByElectionDataError(ValueError):
    """Raised when the exploratory report cannot be fitted safely."""


def load_by_elections(filename=DATA_FILE):
    """Load the small input table and validate fields used by this report."""

    try:
        data = pd.read_csv(filename)
    except OSError as error:
        raise ByElectionDataError(
            f'Could not read by-election data from {filename}: {error}'
        ) from error

    missing_columns = [
        column for column in REQUIRED_COLUMNS if column not in data.columns
    ]
    if missing_columns:
        raise ByElectionDataError(
            'By-election data is missing required column(s): ' +
            ', '.join(missing_columns)
        )

    data = data.copy()
    for column in NUMERIC_COLUMNS:
        values = pd.to_numeric(data[column], errors='coerce')
        if values.isna().any() or not np.isfinite(values).all():
            raise ByElectionDataError(
                f'By-election data has a missing or non-finite {column} value.'
            )
        data[column] = values

    party_change = data['Party change']
    if not pd.api.types.is_bool_dtype(party_change):
        normalized = party_change.astype(str).str.strip().str.upper()
        if not normalized.isin(('TRUE', 'FALSE')).all():
            raise ByElectionDataError(
                'Party change values must be TRUE or FALSE.'
            )
        data['Party change'] = normalized.eq('TRUE')

    if data['Government'].isna().any() or (
        data['Government'].astype(str).str.strip() == ''
    ).any():
        raise ByElectionDataError('By-election data has a missing Government value.')

    data['swingdev'] = data['Eventual swing'] - data['Statewide swing']
    data['byelecswing'] = data['By-elec swing']
    return data


def report(data, label):
    """Fit matching OLS and median regressions for one validated cohort."""

    if len(data) < 3:
        raise ByElectionDataError(
            f'{label} has fewer than three by-election observations.'
        )
    if data['byelecswing'].nunique() < 2:
        raise ByElectionDataError(
            f'{label} has no variation in by-election swing.'
        )

    predictors = sm.add_constant(data['byelecswing'], has_constant='add')
    ols_results = sm.OLS(data['swingdev'], predictors).fit()
    print(ols_results.summary())

    quantile_model = smf.quantreg('swingdev ~ byelecswing', data)
    quantile_results = quantile_model.fit(q=0.5)
    print(quantile_results.summary())
    return ols_results, quantile_results


def print_report(data, label):
    print('*' * 60)
    print(f'The following reports are for: {label}')
    print('*' * 60)
    return report(data, label)


def main(filename=DATA_FILE):
    data = load_by_elections(filename)
    print('Assessing: by-election swing relationships')

    print_report(data, 'all by-elections')
    print_report(
        data[data['Party change']],
        'by-elections with a party change',
    )
    print_report(
        data[data['Government'] == 'ALP'],
        'by-elections with ALP in government',
    )
    print_report(
        data[data['Government'] == 'LNP'],
        'by-elections with Coalition in government',
    )
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except ByElectionDataError as error:
        print(f'Could not analyse by-election data: {error}', file=sys.stderr)
        sys.exit(2)

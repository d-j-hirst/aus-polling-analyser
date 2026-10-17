"""Estimate category relationships from final counts and saved poll adjustments.

This is a diagnostic of election-wide movements, not a fit of the live model.
It gives each election one observation and tests predictions by withholding
one whole election at a time. No simulation or Bayesian fitting is launched.
"""

import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path

import numpy as np

from lib.shared.poll_transform import transform_vote_share as z, detransform_vote_share as unz
from lib.trend_adjustments.trend_adjust_io import load_adjustment_data, adjustment_parameters_at
from scripts.diagnostics.category_sources import CACHE, ROOT

ANALYSIS = ROOT / 'analysis'
OUTPUT = CACHE / 'relationships.json'
CATEGORIES = ('early', 'postal', 'absent', 'provisional', 'ppvc', 'early_declaration')
SEQUENCES = {
    'fed': [2004, 2007, 2010, 2013, 2016, 2019, 2022, 2025],
    'vic': [2006, 2010, 2014, 2018, 2022],
    'nsw': [2015, 2019, 2023],
    'qld': [2006, 2009, 2012, 2015, 2017, 2020, 2024],
    'wa': [2005, 2008, 2013, 2017, 2021, 2025],
}


def adjusted_central_forecast(code):
    """Apply the saved final-day median-level TPP adjustment, without fitting.

    This reproduces the existing Python trend diagnostic's central estimate.
    It is not a claim to recreate the GUI's stochastic FP/TPP sample mixture.
    """
    trend_path = ANALYSIS / 'Outputs' / f'fp_trend_{code}_@TPP.csv'
    with trend_path.open(newline='') as stream:
        rows = list(csv.reader(stream))
    raw = float(rows[-1][rows[2].index('50%')])
    fundamentals_path = ANALYSIS / 'Fundamentals' / f'fundamentals_{code}.csv'
    with fundamentals_path.open(newline='') as stream:
        fundamentals = next(float(r[1]) for r in csv.reader(stream) if r[0] == '@TPP')
    adjustment_path = ANALYSIS / 'Adjustments' / f'adjust_{code}_TPP.csv'
    if not adjustment_path.exists():
        adjustment_path = ANALYSIS / 'Adjustments' / 'adjust_0none_TPP.csv'
    parameters = adjustment_parameters_at(load_adjustment_data(adjustment_path), z(raw), 0)
    poll_bias, fundamental_bias, mixed_bias, _, _, _, _, mix = parameters
    adjusted = (z(raw) - poll_bias) * mix + (z(fundamentals) - fundamental_bias) * (1-mix) - mixed_bias
    return dict(raw=raw, adjusted=unz(adjusted), transformed=adjusted,
                trend=str(trend_path.relative_to(ROOT)), adjustments=str(adjustment_path.relative_to(ROOT)),
                fundamentals=str(fundamentals_path.relative_to(ROOT)))


def final_results():
    with (ANALYSIS / 'Data' / 'eventual-results.csv').open(newline='') as stream:
        return {r[0]+r[1]: float(r[3]) for r in csv.reader(stream) if len(r)>3 and r[2]=='@TPP'}


def seat_accounts(records):
    accounts = defaultdict(lambda: defaultdict(dict))
    for r in records:
        seat = re.sub(r'\s+', ' ', r['seat'].replace('-', ' ').replace('.', '')).strip().title()
        accounts[r['election']][seat][r['category']] = np.array([r['alp'], r['coalition']], dtype=float)
    return accounts


def category_count(account, category):
    # Combine all early votes for the cross-election comparison. The separate
    # PPVC diagnostic starts only after AEC introduced ordinary pre-poll voting.
    if category == 'early':
        values = [account[c] for c in ('early', 'early_declaration', 'declaration_early') if c in account]
        return sum(values) if values else None
    if category == 'ppvc':
        return account.get('early')
    return account.get(category)


def share_z(count):
    # Half a vote avoids infinite transformed values in sparse provisional
    # categories. At election scale its effect is negligible.
    if count is None or sum(count) == 0:
        return None
    return 25 * math.log((count[0] + .5) / (count[1] + .5))


def aggregate(accounts, seats, category=None):
    values = [(sum(accounts[s].values()) if category is None else category_count(accounts[s], category)) for s in seats]
    return sum((v for v in values if v is not None), np.zeros(2))


def comparison(previous, current, category, final, forecast):
    # Hold the district set fixed across both elections. Centre each category
    # on the TPP total of that same set, so changing coverage of non-classic
    # seats cannot itself become a common election forecast error.
    seats = sorted(s for s in previous.keys() & current.keys()
        if all(share_z(category_count(a[s], c)) is not None
               for a in (previous, current) for c in ('ordinary', category)))
    if len(seats) < 30:
        return None
    total = [share_z(aggregate(a, seats)) for a in (previous, current)]
    ordinary = [share_z(aggregate(a, seats, 'ordinary')) for a in (previous, current)]
    other = [share_z(aggregate(a, seats, category)) for a in (previous, current)]
    overall_error = z(final) - forecast['transformed']
    x = overall_error + (ordinary[1]-total[1]) - (ordinary[0]-total[0])
    y = overall_error + (other[1]-total[1]) - (other[0]-total[0])
    local = []
    for seat in seats:
        p, c = previous[seat], current[seat]
        change = ((share_z(category_count(c, category)) - share_z(c['ordinary'])) -
                  (share_z(category_count(p, category)) - share_z(p['ordinary'])))
        local.append(dict(seat=seat, change=change, votes=float(sum(category_count(c, category)))))
    local_weights = np.array([r['votes'] for r in local])
    local_changes = np.array([r['change'] for r in local])
    local_mean = float(np.average(local_changes, weights=local_weights))
    return dict(seats=len(seats), ordinary_error=x, category_error=y, contrast_change=y-x,
        overall_error=overall_error, current_ordinary=unz(ordinary[1]), current_category=unz(other[1]),
        previous_ordinary=unz(ordinary[0]), previous_category=unz(other[0]),
        previous_sample_tpp=unz(total[0]), current_sample_tpp=unz(total[1]),
        local_change_mean=local_mean,
        local_change_rms=float(np.sqrt(np.average((local_changes-local_mean)**2, weights=local_weights))),
        local=local)


def election_comparisons(accounts):
    actual = final_results()
    results, unavailable, forecasts = [], [], {}
    for region, years in SEQUENCES.items():
        for old, new in zip(years, years[1:]):
            previous_code, code = f'{old}{region}', f'{new}{region}'
            if previous_code not in accounts or code not in accounts:
                unavailable.append(dict(election=code, previous=previous_code, reason='Missing category preference dataset'))
                continue
            forecast = adjusted_central_forecast(code)
            forecasts[code] = forecast
            added = 0
            for category in CATEGORIES:
                if category in ('ppvc', 'early_declaration') and region != 'fed':
                    continue
                # Pre-2010 federal pre-poll was a single declaration category.
                # Its later declaration-only subset is not a comparable series.
                if region == 'fed' and category in ('ppvc', 'early_declaration') and old < 2010:
                    continue
                row = comparison(accounts[previous_code], accounts[code], category, actual[code], forecast)
                if row is not None:
                    row.update(election=code, previous=previous_code, category=category)
                    results.append(row)
                    added += 1
            if not added:
                unavailable.append(dict(election=code, previous=previous_code, reason='Fewer than 30 complete common districts'))
    return results, unavailable, forecasts


def regression(x, y):
    intercept, slope = np.linalg.lstsq(np.column_stack((np.ones(len(x)), x)), y, rcond=None)[0]
    return float(intercept), float(slope)


def relationship_summary(rows):
    x = np.array([r['ordinary_error'] for r in rows])
    y = np.array([r['category_error'] for r in rows])
    intercept, slope = regression(x, y)
    predictions = defaultdict(list)
    slopes = []
    for i in range(len(rows)):
        other = np.arange(len(rows)) != i
        a, b = regression(x[other], y[other])
        slopes.append(b)
        predictions['fitted'].append(a+b*x[i])
        # Compare fixed rules against a trained mean category-contrast rule.
        predictions['carry_ordinary'].append(x[i])
        predictions['sixty_percent_reversion'].append(.4*x[i])
        predictions['independent_category'].append(0.)
        predictions['mean_contrast'].append(x[i]+float(np.mean(y[other]-x[other])))
    residual = y-(intercept+slope*x)
    return dict(elections=len(rows), intercept=intercept, slope=slope, contrast_slope=slope-1,
        correlation=float(np.corrcoef(x,y)[0,1]),
        contrast_mean=float(np.mean(y-x)), contrast_sd=float(np.std(y-x,ddof=1)),
        residual_sd=float(np.sqrt(np.sum(residual**2)/(len(rows)-2))),
        leave_one_out_slope_range=[min(slopes),max(slopes)],
        predictive_rmse={name:float(np.sqrt(np.mean((y-values)**2))) for name,values in predictions.items()},
        local_change_rms=float(np.sqrt(np.mean([r['local_change_rms']**2 for r in rows]))))


def summarize(comparisons):
    summaries = {}
    for category in CATEGORIES:
        rows = [r for r in comparisons if r['category']==category]
        if len(rows)<4:
            continue
        summary = relationship_summary(rows)
        summary['sensitivities'] = {}
        subsets = {'federal_only':lambda r:r['election'].endswith('fed'),
            'state_only':lambda r:not r['election'].endswith('fed'),
            'since_2016':lambda r:int(r['election'][:4])>=2016,
            'without_2025fed':lambda r:r['election']!='2025fed',
            'without_examples':lambda r:r['election'] not in ('2022vic','2023nsw','2024qld','2022fed')}
        for name, keep in subsets.items():
            subset = [r for r in rows if keep(r)]
            if len(subset)>=4:
                summary['sensitivities'][name] = relationship_summary(subset)
        summaries[category] = summary
    return summaries


def common_covariance(comparisons):
    categories = ('early', 'postal', 'absent')
    by_election = defaultdict(dict)
    for row in comparisons:
        by_election[row['election']][row['category']] = row
    complete = {code: rows for code, rows in by_election.items() if all(c in rows for c in categories)}
    values = np.array([[rows[c]['contrast_change'] for c in categories] for rows in complete.values()])
    return dict(categories=categories, elections=list(complete),
        covariance=np.cov(values, rowvar=False).tolist(),
        correlation=np.corrcoef(values, rowvar=False).tolist())


def bootstrap_intervals(comparisons):
    # Resample entire elections, never individual districts. Thousands of
    # booths in one election are not thousands of independent national shocks.
    generator = np.random.default_rng(20261016)
    results = {}
    for category in CATEGORIES:
        rows = [r for r in comparisons if r['category'] == category]
        if len(rows) < 4:
            continue
        x = np.array([r['ordinary_error'] for r in rows])
        y = np.array([r['category_error'] for r in rows])
        slopes = []
        for _ in range(4000):
            sample = generator.integers(0, len(rows), len(rows))
            if np.std(x[sample]) > .05:
                slopes.append(regression(x[sample], y[sample])[1])
        results[category] = np.quantile(slopes, [.025, .975]).tolist()
    return results


def main():
    normalized = json.loads((CACHE / 'normalized.json').read_text(encoding='utf-8'))
    comparisons, unavailable, forecasts = election_comparisons(seat_accounts(normalized['records']))
    summaries = summarize(comparisons)
    result = dict(comparisons=comparisons, unavailable=unavailable, forecasts=forecasts, summaries=summaries,
                  contrast_covariance=common_covariance(comparisons), bootstrap_slope_intervals=bootstrap_intervals(comparisons))
    OUTPUT.write_text(json.dumps(result, indent=2), encoding='utf-8')
    for category, summary in summaries.items():
        print(category, json.dumps({k:v for k,v in summary.items() if k!='sensitivities'}))
    for r in comparisons:
        if r['category'] in ('early','postal','absent'):
            print(r['election'],r['category'],r['seats'],*[round(r[k],3) for k in
                ('overall_error','ordinary_error','category_error','contrast_change')])


if __name__ == '__main__':
    main()

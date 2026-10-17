"""Compare separate declaration pre-polls with booths and election-day absents.

Use only complete same-name district samples with both categories available
in both elections. Retain integer count vectors until aggregation: a merged
share is calculated from summed ballots, never an average of transformed shares.
This diagnostic does not alter live inputs or model parameters.
"""

import json
import numpy as np

from scripts.diagnostics.category_sources import CACHE, ROOT
from scripts.diagnostics.category_tpp_relationships import seat_accounts, share_z
from scripts.diagnostics.audit_category_tpp import centre_at_total, share, table

DESTINATION = ROOT / 'analysis' / 'reports' / 'live-category-tpp'
PAIRS = [('2010fed', '2013fed'), ('2013fed', '2016fed'), ('2016fed', '2019fed'),
         ('2019fed', '2022fed'), ('2022fed', '2025fed'), ('2020qld', '2024qld')]
CATEGORIES = ('ordinary', 'ppvc', 'early_absent', 'day_absent', 'postal')
LABELS = dict(ordinary='Ordinary', ppvc='PPVC / early booths', early_absent='Early absent',
              day_absent='Day absent', postal='Postal')


def split_account(account, code):
    # These are the raw normalized keys. Do not use category_count('early'),
    # which intentionally merged the two early subtypes in the original study.
    early_absent = 'declaration_early' if code.endswith('qld') else 'early_declaration'
    keys = dict(ordinary='ordinary', ppvc='early', early_absent=early_absent,
                day_absent='absent', postal='postal')
    return {name: account.get(key) for name, key in keys.items()}


def complete(account):
    return all(share_z(account[c]) is not None for c in CATEGORIES)


def matched_accounts(accounts, old, new):
    previous = {s: split_account(a, old) for s, a in accounts[old].items()}
    current = {s: split_account(a, new) for s, a in accounts[new].items()}
    seats = sorted(s for s in previous.keys() & current.keys()
                   if complete(previous[s]) and complete(current[s]))
    return seats, [previous, current]


def departures(previous, current):
    changes = {c: share_z(current[c]) - share_z(previous[c]) for c in CATEGORIES}
    return {c: changes[c] - changes['ordinary'] for c in CATEGORIES}


def correlation_matrix(values, weights=None):
    values = np.asarray(values)
    if weights is None:
        weights = np.ones(len(values))
    centred = values - np.average(values, axis=0, weights=weights)
    covariance = (centred.T * weights) @ centred / sum(weights)
    scale = np.sqrt(np.outer(np.diag(covariance), np.diag(covariance)))
    # A constant fictional category has no measurable correlation. Real source
    # rows here vary; returning null avoids presenting zero variance as evidence.
    return [[float(covariance[i, j]/scale[i, j]) if scale[i, j] > 0 else None
             for j in range(len(scale))] for i in range(len(scale))]


def pooling_loss(previous, current, partner):
    """Give a pooled model its final total and measure hidden split errors.

    The model knows both previous shares and the current component sizes, but
    assumes their historical log-odds gap persists. Any remaining discrepancy
    therefore measures the behavioural distinction hidden by pooling, even
    under this generous information assumption. It is not a live backtest.
    """
    names = ('early_absent', partner)
    combined = sum(current[c] for c in names)
    weights = {c: float(sum(current[c])/sum(combined)) for c in names}
    expected = centre_at_total({c: share(previous[c]) for c in names}, weights, share(combined))
    errors = {c: share(current[c])-expected[c] for c in names}
    contrast = (share_z(combined)-share_z(current['ordinary'])
                -share_z(sum(previous[c] for c in names))+share_z(previous['ordinary']))
    return dict(early_absent_fraction=weights['early_absent'], departure=contrast,
                component_errors_pp=errors,
                weighted_error_pp=sum(weights[c]*errors[c] for c in names))


def analyse_pair(accounts, old, new):
    seats, partitions = matched_accounts(accounts, old, new)
    if len(seats) < 30:
        raise ValueError(f'Insufficient complete districts for {new}')
    totals = [{c: sum((a[s][c] for s in seats), np.zeros(2)) for c in CATEGORIES}
              for a in partitions]
    changes = departures(*totals)
    local = [departures(*(a[s] for a in partitions)) for s in seats]
    weights = [float(sum(partitions[1][s]['early_absent'])) for s in seats]
    # These correlations describe differences between districts within one
    # election. They do not turn districts into independent election shocks.
    local_corr = correlation_matrix([[row[c] for c in CATEGORIES[1:]] for row in local], weights)
    return dict(election=new, previous=old, districts=len(seats), departures=changes,
                previous_shares={c: share(totals[0][c]) for c in CATEGORIES},
                current_shares={c: share(totals[1][c]) for c in CATEGORIES},
                local_correlations=local_corr,
                pooling={partner: pooling_loss(*totals, partner) for partner in ('ppvc', 'day_absent')})


def group_statistics(rows):
    values = np.array([[r['departures'][c] for c in CATEGORIES[1:]] for r in rows])
    pair_errors = {}
    for c in ('ppvc', 'day_absent', 'postal'):
        # Subtracting two ordinary-relative departures removes the shared
        # ordinary reference. This is a useful check against spurious positive
        # correlations induced by that common reference.
        differences = np.array([r['departures']['early_absent']-r['departures'][c] for r in rows])
        pair_errors[c] = dict(rms=float(np.sqrt(np.mean(differences**2))),
                             mean=float(np.mean(differences)))
    return dict(elections=len(rows), correlations=correlation_matrix(values), difference_errors=pair_errors)


def build_analysis(normalized):
    accounts = seat_accounts(normalized['records'])
    rows = [analyse_pair(accounts, old, new) for old, new in PAIRS]
    federal = [r for r in rows if r['election'].endswith('fed')]
    return dict(category_order=list(CATEGORIES[1:]), rows=rows,
                federal=group_statistics(federal), all=group_statistics(rows))


def report(data):
    text = '''# Declaration pre-poll relationships

This analysis tests whether declaration pre-polls should share information
with ordinary PPVCs, election-day absent votes, or both. It also measures what
is hidden when early-absent and day-absent results are available only together.
It informs the agreed approach of uncertain category composition with offsetting
effects; no production forecast calculation is changed here.

## Categories and comparison units

The original research's broad early series combined ordinary PPVCs and
declaration pre-polls. That was an analysis grouping, not the live TPP model's
category definition. `LiveTppPreparation.cpp::categoryCode` keeps ordinary booth
types and declaration vote types separate. The broad grouping is inappropriate
as a substitute for the separate category relationships examined here.

Here PPVC means the raw federal ordinary pre-poll booth category. Early absent
means federal declaration pre-poll, or Queensland's separately reported AB2
(Absent Early Voting). Day absent is federal absent or Queensland AB1. These
are analogous operational categories, not a claim of identical voter populations.
Postal and election-day ordinary counts remain separate throughout.

There are five federal election transitions beginning with 2010-to-2013 and
one Queensland transition, 2020-to-2024. Earlier federal pre-poll definitions
do not supply a comparable split. Both elections in each comparison use the
same districts with positive counts in all five categories. District names
are matched; redistributed geography is not reconstructed. Some older federal
sources exclude non-classic seats without a separate TPP table.

Shares use Labor divided by Labor plus Coalition votes. A category departure
is its transformed swing minus the ordinary transformed swing, using
Z(p)=25 log(p/(100-p)). Thus +1 means a category moved about one percentage
point more favourably than ordinary near 50%. Common forecast error cancels;
the accepted trend-adjusted benchmarks do not need changing for this comparison.

## Election-wide departures

This table asks whether the separately observed categories move together.
Every cell uses the same district sample within its row.

'''
    text += table(['Election', 'Districts', 'PPVC / early booths', 'Early absent', 'Day absent', 'Postal'],
                  [[r['election'], r['districts']] + [f"{r['departures'][c]:+.2f}" for c in CATEGORIES[1:]]
                   for r in data['rows']])
    text += '''
## Relationships across elections

Correlations below compare early-absent departures with another category's
departures. The RMS difference instead asks how wrong it would be to carry
that other category's departure directly into early absent. It subtracts away
the shared ordinary reference. Both are descriptive: five federal transitions
and one Queensland transition cannot establish precise universal coefficients,
and adjacent transitions share historical elections.

'''
    text += table(['Sample', 'Comparator', 'Correlation', 'RMS difference (transformed points)'],
        [[label, LABELS[c], f"{data[key]['correlations'][1][CATEGORIES[1:].index(c)]:+.2f}",
          f"{data[key]['difference_errors'][c]['rms']:.2f}"]
         for key, label in [('federal', 'Five federal'), ('all', 'Federal + QLD')]
         for c in ('ppvc', 'day_absent', 'postal')])
    text += '''
Federal early-absent changes are substantially closer to day-absent changes
than to PPVC changes. Queensland 2024 shows a different pattern: early absent
moves with early booths, while day absent moves strongly the other way.
Adding that single election materially changes the correlations; it should
not be discarded, nor should the resulting six-election matrix be treated as
a stable universal covariance estimate. Common-reference effects can inflate
correlations; the difference metric and within-election check help assess this.

## Differences between districts

These correlations compare district-level category departures within each
election, weighting districts by their current early-absent two-party count.
Each election's weighted mean is removed before correlation. Shared ordinary
noise, redistribution and local political movement remain potential influences.
These are descriptive correlations, not tests with hundreds of independent
observations of an election-wide effect.

'''
    text += table(['Election', 'With PPVC', 'With day absent', 'With postal'],
        [[r['election']] + [f"{r['local_correlations'][1][i]:+.2f}" for i in (0, 2, 3)]
         for r in data['rows']])
    text += '''
## What pooling hides

For this calculation, a hypothetical pooled model knows the final combined
TPP and the current component sizes, and retains the previous log-odds gap
between the two components. It adjusts them together to reproduce the known
combined result. The errors below are actual minus this inferred component
share, in percentage points. Their vote-weighted sum is zero: the group total
is correct even when the component estimates are wrong. This is an information
loss diagnostic with final counts, not a test of an election-night forecast.

'''
    text += table(['Election', 'Combine early absent with', 'Early-absent error (pp)', 'Other component error (pp)'],
        [[r['election'], LABELS[c], f"{r['pooling'][c]['component_errors_pp']['early_absent']:+.2f}",
          f"{r['pooling'][c]['component_errors_pp'][c]:+.2f}"]
         for r in data['rows'] for c in ('ppvc', 'day_absent')])
    text += '''
In federal 2019, early-absent and PPVC departures have opposite signs. Pooling
them hides an early-absent discrepancy of about 2.26 points even with the
correct final pooled total. Pooling with day absent loses much less in these
federal examples. In Queensland 2024, however, the absent combination hides
about -1.82 points in early absent and +4.43 points in day absent.

## Implications for combined NSW and WA counts

Use the available observation at its reported resolution. A combined absent
count measures the sum of early-absent and day-absent counts; it does not prove
that their swings or uncertainties are equal. Do not assign the combined
share to both components or infer an exact split from the result.

A joint model can retain ordinary, PPVC, early absent, day absent and postal
components, with an observation mapping for each jurisdiction. Where separate
counts exist, they inform the separate components and their relationships.
Where only combined absent exists, its likelihood constrains their weighted
sum. Unobserved split uncertainty remains. If the split sizes are not known,
they also require uncertainty rather than an invented fixed allocation.
Aggregation must occur on the count/share scale, not by averaging log odds.

An equivalent parameterisation has a common away-vote component and an
early-versus-day contrast. Correlation between that contrast and PPVC can then
represent the Queensland pattern without forcing the federal pattern to match.
The total/offset accounting must preserve each overall TPP draw as already
agreed. With so few transitions, these correlations need substantial shrinkage
and jurisdiction sensitivity; they are not ready-made forecast coefficients.

NSW observations can still inform ordinary, reported early-booth, postal and
combined-absent relationships. They need not be excluded wholesale because the
absent split is unavailable. They cannot independently identify early-absent
versus day-absent covariance. The same principle applies to a confirmed combined
WA category. The current 2021 WA preference data remains too incomplete to use
2025 as a successive-election observation; a 2025 baseline for 2029 is a
separate operational use.

## Reproduction and sources

The integer counts and source URLs are preserved in the existing normalized
cache. Federal sources are AEC booth TPP and category TPP/TCP downloads;
Queensland uses the final official XML preference distribution. Examples:

- [AEC 2022 TPP by vote type](https://results.aec.gov.au/27966/website/HouseTppByDivisionByVoteType-27966.htm)
- [ECQ 2024 final XML](https://resultsdata.elections.qld.gov.au/XMLData/publicResults_SGE2024_ICCDiv4_Final.zip)

From `analysis/`:

```powershell
.\\.venv-win\\Scripts\\python.exe -B -m scripts.diagnostics.declaration_prepoll_relationships
.\\.venv-win\\Scripts\\python.exe -B -m unittest discover -s tests -p test_category_tpp_relationships.py
```

`declaration-prepoll.json` retains all calculated values, including previous
and current shares, full correlation matrices and aggregation checks. No
downloads, Stan fitting or C++ builds/runs are performed by this diagnostic.
'''
    return text


def main():
    normalized = json.loads((CACHE / 'normalized.json').read_text(encoding='utf-8'))
    result = build_analysis(normalized)
    (DESTINATION / 'declaration-prepoll.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    (DESTINATION / 'declaration-prepoll.md').write_text(report(result), encoding='utf-8')
    for group in ('federal', 'all'):
        print(group, json.dumps(result[group]))


if __name__ == '__main__':
    main()

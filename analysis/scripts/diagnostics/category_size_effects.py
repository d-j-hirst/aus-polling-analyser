"""Check whether category growth predicts changes in matched category swings.

This is a small diagnostic, not a causal migration estimate or a live-model
change. It compares all available election pairs and withholds whole elections
when assessing a one-variable growth predictor.
"""

import json
import math

import numpy as np

from scripts.diagnostics.category_sources import CACHE, ROOT
from scripts.diagnostics.category_tpp_relationships import aggregate, seat_accounts, share_z
from scripts.diagnostics.audit_category_tpp import common_accounts, table

DESTINATION = ROOT / 'analysis' / 'reports' / 'live-category-tpp'


def growth_observation(previous, current, merge_remote):
    seats, partitions = common_accounts(previous, current, merge_remote)
    if len(seats) < 30:
        return None
    counts = [{c: aggregate(a, seats, c) for c in ('ordinary', 'early', 'postal')}
              for a in partitions]
    totals = [float(sum(aggregate(a, seats))) for a in partitions]
    fractions = [{c: float(sum(n)) / total for c, n in row.items()}
                 for row, total in zip(counts, totals)]
    # A log ratio respects the bounded early share within the ordinary+early
    # parent. Postal growth is retained separately: early growth need not be
    # supplied by ordinary voters, particularly after Covid-era postal voting.
    ratios = [math.log(sum(row['early']) / sum(row['ordinary'])) for row in counts]
    contrasts = [share_z(row['early']) - share_z(row['ordinary']) for row in counts]
    return dict(seats=len(seats), previous_fractions=fractions[0], current_fractions=fractions[1],
                log_ratio_growth=ratios[1]-ratios[0], contrast_change=contrasts[1]-contrasts[0])


def collect_observations(normalized, relationships):
    accounts = seat_accounts(normalized['records'])
    result = []
    for row in relationships['comparisons']:
        if row['category'] != 'early':
            continue
        code, previous = row['election'], row['previous']
        observation = growth_observation(accounts[previous], accounts[code], code == '2023nsw')
        if observation is not None:
            observation.update(election=code, previous=previous)
            result.append(observation)
    return result


def compare_predictors(rows):
    x = np.array([row['log_ratio_growth'] for row in rows])
    y = np.array([row['contrast_change'] for row in rows])
    design = np.column_stack((np.ones(len(x)), x))
    intercept, slope = np.linalg.lstsq(design, y, rcond=None)[0]
    predictions = {'no_change': np.zeros(len(y)), 'historical_mean': [], 'size_growth': []}
    for i in range(len(rows)):
        # Entire elections are test observations; treating their districts as
        # independent training examples would overstate the evidence for a
        # shared election-level effect.
        keep = np.arange(len(rows)) != i
        coefficients = np.linalg.lstsq(design[keep], y[keep], rcond=None)[0]
        predictions['historical_mean'].append(float(np.mean(y[keep])))
        predictions['size_growth'].append(float(design[i] @ coefficients))
    return dict(elections=len(rows), intercept=float(intercept), slope=float(slope),
                correlation=float(np.corrcoef(x, y)[0, 1]),
                prediction_rmse={name: float(np.sqrt(np.mean((y-values)**2)))
                                 for name, values in predictions.items()})


def transfer_votes(ordinary_size, early_size, ordinary_share, early_share, moved, mover_share):
    """Move a specified group without changing anyone's political preference.

    Sizes are counts and shares are proportions. Linear vote accounting here
    is intentional: it conserves each party's ballots exactly. Statistical
    distributions for shares would be specified on a bounded transform.
    """
    if not (0 <= moved < ordinary_size and early_size > 0):
        raise ValueError('Transfer must leave a positive ordinary category')
    if not all(0 <= p <= 1 for p in (ordinary_share, early_share, mover_share)):
        raise ValueError('Vote shares must be proportions')
    labor_left = ordinary_size*ordinary_share - moved*mover_share
    coalition_left = ordinary_size*(1-ordinary_share) - moved*(1-mover_share)
    if min(labor_left, coalition_left) < 0:
        raise ValueError('Transferred voters exceed the source party count')
    return dict(ordinary=(ordinary_size-moved, labor_left/(ordinary_size-moved)),
                early=(early_size+moved, (early_size*early_share+moved*mover_share)/(early_size+moved)))


def example_rows():
    result = []
    for label, preference in [('More Coalition-leaning than existing early voters', .30),
                              ('Same as existing early voters', .40),
                              ('Representative of ordinary voters', .55),
                              ('More Labor-leaning than ordinary voters', .65)]:
        after = transfer_votes(600, 400, .55, .40, 100, preference)
        result.append(dict(description=label, mover_share=100*preference,
                           ordinary=100*after['ordinary'][1], early=100*after['early'][1],
                           overall=sum(n*p for n, p in after.values())/10))
    return result


def report(data):
    text = '''# Category size changes and matched swings

This diagnostic asks whether changes in voting-channel sizes help explain
ordinary-versus-early swings, and what they can identify about voters switching
channels. The relevant quantities are changes from previous category results,
not raw early-versus-ordinary vote gaps. The accepted trend-adjusted forecast
benchmarks remain unchanged. No production model is modified.

The broad early series here includes both ordinary PPVC and declaration
pre-poll counts. The [separate-category analysis](declaration-prepoll.md)
shows why this exploratory grouping must not replace the distinct categories
when estimating relationships for the live model.

## What category sizes can and cannot establish

Suppose 600 ordinary voters are 55% Labor and 400 early voters are 40% Labor:
49% overall. Move 100 voters from ordinary to early without changing anyone's
preference. The table shows how the same size change has different effects
depending on the transferred voters. All numbers are Labor percentages.

'''
    text += table(['Transferred group', 'Group TPP', 'New ordinary TPP', 'New early TPP', 'Overall TPP'],
                  [[r['description']] + [f"{r[k]:.1f}" for k in ('mover_share', 'ordinary', 'early', 'overall')]
                   for r in data['examples']])
    text += '''
The original overall 49% is preserved. In the first row, the apparent swings
are +5 pp ordinary and -2 pp early, despite no voter changing preference.
Movers who exactly resemble existing early voters leave early TPP unchanged;
ordinary TPP rises. Opposite raw swings require a more Coalition-leaning
transferred group. General election swing and re-centring can additionally
alter residuals relative to the forecast.

Category totals reveal net changes, not who moved. They cannot distinguish
two-way switching, new electors, changing turnout, exhaustion or genuine
political movement within a channel. Treating all net early growth as former
ordinary voters is an assumption, especially when postal voting also changes.

## Observed size and swing changes

Every row uses the same available named districts in both elections; district
boundaries are not matched. Size changes are percentage points of the complete
Labor-plus-Coalition preference count. They are not percentages of enrolment
or counts of people moving. In optional-preferential NSW, changes in exhaustion
also affect this denominator. A physical migration analysis should use all
formal ballots for category sizes, with exhaustion modelled separately.
For 2023 NSW only, the postal column compares 2023 postals with 2019 postals
plus iVote, so its small net change is for the combined remote channel.

The final column is the change in early-minus-ordinary TPP on the scale
Z(p)=25 log(p/(100-p)). Near 50%, a transformed point is about a percentage
point. It is a difference of category swings, so the common polling benchmark
cancels. A negative value means early voting swung less favourably for Labor.

'''
    text += table(['Election', 'Districts', 'Ordinary size change (pp)', 'Early size change (pp)',
                   'Postal size change (pp)', 'Early-minus-ordinary swing change'],
        [[r['election'], r['seats']] +
         [f"{100*(r['current_fractions'][c]-r['previous_fractions'][c]):+.2f}"
          for c in ('ordinary', 'early', 'postal')] + [f"{r['contrast_change']:+.2f}"]
         for r in data['observations']])
    fit = data['prediction']
    text += f'''
The simple size predictor regresses the contrast change on the change in
log(early count / ordinary count), with an intercept. This is the log odds
of early voting within the ordinary-plus-early parent. Each of the
{fit['elections']} elections receives equal weight and is predicted by training
on the other elections. Errors below are root mean squared prediction errors
in transformed points; smaller is better.

'''
    text += table(['Predictor', 'Prediction error'],
                  [[name, f'{value:.3f}'] for name, value in fit['prediction_rmse'].items()])
    text += f'''
The fitted slope is {fit['slope']:+.2f} per unit of log-ratio growth, with
correlation {fit['correlation']:+.2f}. Its positive direction contradicts a
universal rule that greater early growth causes a larger adverse early swing.
The prediction error is essentially unchanged from predicting no departure.
This does not establish that switching is unimportant: it establishes that
size alone does not identify its political composition in this small sample.
Adjacent comparisons also share elections, so this is a descriptive test,
not an independent validation sample. All available comparisons are retained;
Covid elections were not removed to obtain a preferred sign.

Queensland 2024 illustrates the need for more than ordinary-to-early transfers:
ordinary voting's fraction barely changed while postals fell substantially and
early voting grew. The net totals are consistent with much of the expansion
coming from postals, but cannot prove individual voter movements.

## Channel-definition check

- Federal: ordinary election-day booths are separated from ordinary PPVCs.
  The broad early channel combines PPVC and declaration pre-poll. This avoids
  confusing the 2010 introduction of pre-poll ordinary voting with behavioural
  migration. PPVC-only comparisons should start after that transition.
  [AEC account of early voting](https://www.aec.gov.au/About_AEC/research/files/apsa-2014-early-voting-in-australian-federal-elections-causes-and-consequences.pdf).
- Victoria: published early totals include the early channel; ordinary,
  postal and absent totals remain separate. Category results can be compared
  within the jurisdiction. Available non-classic TPP coverage still limits
  the district sample; matching a district name is not matching its booths.
- NSW: the 2019 postal-plus-iVote bridge addresses the major discontinuity for
  2023. Absent includes votes cast outside the district during early voting,
  as well as on election day. It cannot be equated to federal day-only absent
  voting or silently added to early totals. The NSWEC report documents absent
  declarations during both periods and overseas/interstate early voting.
  [NSWEC 2023 report, pp. 46–48](https://elections.nsw.gov.au/getmedia/e2dbefc9-6977-466d-bf6e-b1010ead66fc/2023-state-election-report-part-1.pdf).
- Queensland: inspected 2020/2024 XML distinguishes AB1 (Absent Election Day)
  and AB2 (Absent Early Voting). The adapter already combines AB2 with early
  centres, and keeps AB1 as absent. In-person declarations with an unspecified
  voting day remain separate. [2024 final XML archive](https://resultsdata.elections.qld.gov.au/XMLData/publicResults_SGE2024_ICCDiv4_Final.zip).
- WA: 2025 cannot yet supply a comparable successive-election preference
  observation because only one 2021 district passes the existing completion
  check. It is not evidence for or against a particular growth effect. The
  2020 Queensland level is available, but no usable 2017 preference-category
  baseline is presently loaded for a 2017-to-2020 comparison.

Thus the main within-jurisdiction comparisons are usable with the documented
NSW bridge, but identical labels do not establish identical cross-jurisdiction
populations. There is no new demonstrated large double-counting error in the
examined mappings. NSW exhaustion and unresolved early/absent splits must be
kept explicit in any subsequent migration fit.

## Implications of the candidate approaches

Re-centring previous category shares under new category weights provides an
accounting-consistent baseline. It does not estimate the political composition
of switchers and must not be described as removing their effect.

A fixed-composition comparison is also useful descriptively. Writing f for
category fractions and p for Labor shares, the exact total change can be split
as sum(mean(f_old,f_new) * change(p)) plus
sum(mean(p_old,p_new) * change(f)). The first term holds category sizes at their
midpoint and measures changes in category shares; the second measures the
different weighting of categories. Neither is a pure causal effect: switching
voters changes both f and p. In the no-political-change examples above the two
terms cancel. Subtracting the second term and calling the remainder genuine
political swing would therefore be incorrect.

A regression of swing contrasts on size changes is easy to evaluate and can
include postal changes, but the one-variable experiment provides no predictive
gain. More coefficients require stronger evidence than fourteen observations.
District information helps only if election-level common shocks and geographic
changes are separated; thousands of districts are not thousands of independent
election-wide shocks.

An explicit transfer model conserves party counts when voters change channel.
For a transfer of m voters with Labor share s from ordinary to early,
O_new=O-m, E_new=E+m, Labor_O_new=Labor_O-m*s and
Labor_E_new=Labor_E+m*s. It gives transparent scenarios but net margins alone
cannot identify m or s, especially with several channels changing together.

A less demanding alternative represents uncertain category composition while
preserving the overall TPP draw. Draw overall TPP from the accepted adjusted
forecast, draw category sizes and category contrasts, and solve one common
log-odds offset so their weighted shares equal that TPP. Count evidence then
updates both overall TPP and category contrasts. A possible ordinary/early
composition effect must have offsetting weighted contributions; it should not
be copied unchanged to all uncounted votes. Other common forecast uncertainty
remains distinct. Category size changes can inform the scale or mean of this
effect only to the extent supported by calibration; the growth test does not
justify a fixed signed correction or eliminating uncertainty when net growth
is zero (two-way switching can still occur).

## Reproduction

From `analysis/`, using the existing normalized count cache:

```powershell
.\\.venv-win\\Scripts\\python.exe -B -m scripts.diagnostics.category_size_effects
.\\.venv-win\\Scripts\\python.exe -B -m unittest discover -s tests -p test_category_tpp_relationships.py
```

`category-size-effects.json` contains the observations, predictor comparison
and synthetic transfer examples. No Stan fitting or C++ application runs occur.
'''
    return text


def main():
    normalized = json.loads((CACHE / 'normalized.json').read_text(encoding='utf-8'))
    relationships = json.loads((CACHE / 'relationships.json').read_text(encoding='utf-8'))
    rows = collect_observations(normalized, relationships)
    result = dict(observations=rows, prediction=compare_predictors(rows), examples=example_rows())
    (DESTINATION / 'category-size-effects.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    (DESTINATION / 'category-size-effects.md').write_text(report(result), encoding='utf-8')
    print(json.dumps(result['prediction'], indent=2))


if __name__ == '__main__':
    main()

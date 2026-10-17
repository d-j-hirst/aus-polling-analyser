# Category size changes and matched swings

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

| Transferred group | Group TPP | New ordinary TPP | New early TPP | Overall TPP |
| --- | --- | --- | --- | --- |
| More Coalition-leaning than existing early voters | 30.0 | 60.0 | 38.0 | 49.0 |
| Same as existing early voters | 40.0 | 58.0 | 40.0 | 49.0 |
| Representative of ordinary voters | 55.0 | 55.0 | 43.0 | 49.0 |
| More Labor-leaning than ordinary voters | 65.0 | 53.0 | 45.0 | 49.0 |

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

| Election | Districts | Ordinary size change (pp) | Early size change (pp) | Postal size change (pp) | Early-minus-ordinary swing change |
| --- | --- | --- | --- | --- | --- |
| 2007fed | 144 | -1.97 | +2.39 | +0.42 | +0.36 |
| 2010fed | 139 | -3.71 | +3.15 | +0.78 | -1.31 |
| 2013fed | 137 | -8.39 | +6.72 | +2.16 | -0.62 |
| 2016fed | 136 | -4.79 | +4.51 | +0.40 | +0.40 |
| 2019fed | 143 | -8.78 | +9.64 | -0.29 | -0.90 |
| 2022fed | 150 | -8.83 | +4.22 | +6.10 | +1.36 |
| 2025fed | 149 | -4.39 | +4.92 | -0.98 | -1.94 |
| 2010vic | 76 | -9.31 | +7.62 | +1.02 | +1.69 |
| 2014vic | 65 | -10.08 | +8.96 | +1.13 | +0.33 |
| 2018vic | 76 | -10.34 | +10.97 | -1.03 | -1.71 |
| 2022vic | 60 | -13.33 | +12.73 | +3.19 | +2.69 |
| 2019nsw | 93 | -5.76 | +7.40 | -1.62 | -1.25 |
| 2023nsw | 86 | -10.58 | +10.45 | +0.29 | -1.06 |
| 2024qld | 84 | -0.32 | +7.19 | -8.03 | -3.23 |

The simple size predictor regresses the contrast change on the change in
log(early count / ordinary count), with an intercept. This is the log odds
of early voting within the ordinary-plus-early parent. Each of the
14 elections receives equal weight and is predicted by training
on the other elections. Errors below are root mean squared prediction errors
in transformed points; smaller is better.

| Predictor | Prediction error |
| --- | --- |
| no_change | 1.583 |
| historical_mean | 1.657 |
| size_growth | 1.584 |

The fitted slope is +4.61 per unit of log-ratio growth, with
correlation +0.49. Its positive direction contradicts a
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
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.category_size_effects
.\.venv-win\Scripts\python.exe -B -m unittest discover -s tests -p test_category_tpp_relationships.py
```

`category-size-effects.json` contains the observations, predictor comparison
and synthetic transfer examples. No Stan fitting or C++ application runs occur.

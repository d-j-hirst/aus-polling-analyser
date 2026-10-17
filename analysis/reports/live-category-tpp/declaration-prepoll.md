# Declaration pre-poll relationships

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

| Election | Districts | PPVC / early booths | Early absent | Day absent | Postal |
| --- | --- | --- | --- | --- | --- |
| 2013fed | 137 | -0.76 | +0.39 | +1.35 | -0.43 |
| 2016fed | 136 | +0.78 | -0.89 | -0.12 | -1.53 |
| 2019fed | 143 | -1.20 | +1.39 | +0.31 | -0.86 |
| 2022fed | 150 | +1.56 | +0.64 | +0.48 | +3.42 |
| 2025fed | 149 | -1.95 | -1.33 | -1.60 | -1.88 |
| 2024qld | 84 | -3.48 | -2.36 | +3.91 | -4.48 |

## Relationships across elections

Correlations below compare early-absent departures with another category's
departures. The RMS difference instead asks how wrong it would be to carry
that other category's departure directly into early absent. It subtracts away
the shared ordinary reference. Both are descriptive: five federal transitions
and one Queensland transition cannot establish precise universal coefficients,
and adjacent transitions share historical elections.

| Sample | Comparator | Correlation | RMS difference (transformed points) |
| --- | --- | --- | --- |
| Five federal | PPVC / early booths | +0.15 | 1.56 |
| Five federal | Day absent | +0.72 | 0.75 |
| Five federal | Postal | +0.49 | 1.69 |
| Federal + QLD | PPVC / early booths | +0.57 | 1.49 |
| Federal + QLD | Day absent | -0.32 | 2.65 |
| Federal + QLD | Postal | +0.73 | 1.77 |

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

| Election | With PPVC | With day absent | With postal |
| --- | --- | --- | --- |
| 2013fed | -0.03 | +0.39 | +0.32 |
| 2016fed | +0.10 | +0.56 | +0.32 |
| 2019fed | +0.18 | +0.30 | +0.54 |
| 2022fed | -0.01 | +0.48 | +0.14 |
| 2025fed | +0.04 | +0.37 | +0.14 |
| 2024qld | -0.04 | +0.15 | +0.06 |

## What pooling hides

For this calculation, a hypothetical pooled model knows the final combined
TPP and the current component sizes, and retains the previous log-odds gap
between the two components. It adjusts them together to reproduce the known
combined result. The errors below are actual minus this inferred component
share, in percentage points. Their vote-weighted sum is zero: the group total
is correct even when the component estimates are wrong. This is an information
loss diagnostic with final counts, not a test of an election-night forecast.

| Election | Combine early absent with | Early-absent error (pp) | Other component error (pp) |
| --- | --- | --- | --- |
| 2013fed | PPVC / early booths | +0.90 | -0.24 |
| 2013fed | Day absent | -0.53 | +0.43 |
| 2016fed | PPVC / early booths | -1.40 | +0.27 |
| 2016fed | Day absent | -0.43 | +0.34 |
| 2019fed | PPVC / early booths | +2.26 | -0.33 |
| 2019fed | Day absent | +0.53 | -0.55 |
| 2022fed | PPVC / early booths | -0.82 | +0.09 |
| 2022fed | Day absent | +0.07 | -0.08 |
| 2025fed | PPVC / early booths | +0.56 | -0.05 |
| 2025fed | Day absent | +0.13 | -0.14 |
| 2024qld | PPVC / early booths | +0.86 | -0.25 |
| 2024qld | Day absent | -1.82 | +4.43 |

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
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.declaration_prepoll_relationships
.\.venv-win\Scripts\python.exe -B -m unittest discover -s tests -p test_category_tpp_relationships.py
```

`declaration-prepoll.json` retains all calculated values, including previous
and current shares, full correlation matrices and aggregation checks. No
downloads, Stan fitting or C++ builds/runs are performed by this diagnostic.

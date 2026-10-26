# Live TPP category composition

This document explains how the live forecast learns from ordinary, early and
declaration votes without treating their different counting schedules as extra
independent uncertainty about the overall result. It also describes the
historical calibration and its limitations.

The pre-election simulation supplies a correlated distribution of seat TPP:
uncertainty shared across the election and regions, as well as local uncertainty.
The live update allocates that existing distribution between voting categories.
It conditions on received counts jointly, allowing the still-uncounted categories
to differ. It does not add a second national TPP error distribution.

## Candidate corrections before TCP catches up

This adjustment keeps a candidate-level FP recheck from being combined with an
unchanged TCP as though they described the same batch. It applies to both classic
TPP and non-classic TCP under compulsory preferences. Ordinary matched counts,
optional-preferential exhaustion, and the existing protection against incomplete
FP recheck resets retain their usual handling.

The forecast retains the FP account belonging to a published TCP. Queensland's
`indicativeCountDetails` supplies that original account and candidate transfers
directly. Other sources can use a compatible previously matched account. The
compact association survives repeated snapshots and recovery of a withdrawn
TCP; a new TCP vector or later authoritative count supersedes it.

The central estimate is original TCP plus the estimated allocation of the FP
changes. Finalists keep their own revised primaries exactly. Other candidates'
changes use their original measured transfers where available, or the existing
preference assumptions calibrated to the original matched batch. Unchanged
ballots cancel, so there is no discrepancy threshold and no switch to estimating
the whole booth from FP. Preference training continues to use the original
matched pair, never the corrected estimate as a new measurement.

Removed transfers cannot exceed those originally counted. The estimate also
respects each finalist's revised primaries. Uncertainty concerns only changed
preference votes and scales with the affected batch, allowing unrepresentative
revisions rather than assuming independent random ballots. Scenario draws vary
within the feasible transfer bounds; joint TPP comparisons additionally allow
an SD of half that feasible vote range, transformed to log odds. Count constraints
use the guaranteed minimum for each finalist, rather than obsolete TCP totals
that could otherwise override the correction.

For Currimundi Early Voting Centre in Kawana on 27 October 2024, revised FP moved
300 votes from LNP to Labor, removed one ON vote and added two Greens votes.
The original TCP was Labor 1,557 of 4,749. Its original transfers imply an updated
estimate of 1,858.31 of 4,750 (39.1223%), with only three uncertain transfer votes.
The later official TCP was 1,850 of 4,750 (38.9474%); that replaces the estimate.
This example checks the accounting, rather than calibrating the uncertainty rule.

Analysis archives from `joint_category_composition_v18` include `tcp_fp_revision`
for affected booths: original FP/TCP, estimated TCP, guaranteed lower bounds,
uncertain transfer votes, and whether candidate-level transfers were available.
Published integer count fields remain unchanged. Accepted-count caches add an
optional `tcp_basis` field; older caches still load without inventing transfer
details. No project-file format change is required.

## Category baseline provenance

A category supplies swing evidence only when there is a usable comparison for
that category. Actual historical counts, including category totals when current
booth identities have changed, provide the usual comparison. Historical FP used
to estimate a non-classic TPP is still observed category composition; uncertainty
in converting those primaries is handled separately.

An explicitly reconstructed category, such as the split of SA's combined
historical declaration account, is labelled **synthetic** by its reader. Its
comparison retains a working error of five transformed points: four district-local
and three shared across districts using that synthetic category (variances add,
so `sqrt(4² + 3²) = 5`). Near 50% these are approximately share percentage points.
This is a conservative initial allowance, not an empirically calibrated accuracy.
The local error is shared between the measured and FP-derived information groups;
the shared error is an external factor that seat-prior disagreement cannot learn
away. Neither disappears when the current category finishes counting.

A seat-wide fallback with no historical category counts is **unavailable** as a
swing comparison. Zero-only historical records also qualify as unavailable.
Such a category supplies no observation to the joint update. Its received ballots
instead replace their own portion of each resulting seat draw, without changing
other categories or districts. For example, 100 known ballots with ten Labor
votes in a 10,000-vote seat change a 50% estimate to 49.6%, rather than implying a
40-point swing in the remaining votes. Pending preferences use the existing
FP-derived estimate and retain preference-pool conversion uncertainty. Unreported
ballots retain the conditioned seat estimate; missing data is never a zero count.

Completion may improve coverage and within-category comparison weighting, but
cannot create a missing historical baseline or remove synthetic-baseline error.
The export records `category_baselines`, synthetic-error loadings and an
`accounted_posterior_mean` alongside the Gaussian `posterior_mean`. The latter
remains the centre before the local received-vote replacement; the displayed
joint centre and simulation draws include that replacement. Joint SD diagnostics
propagate the replacement's derivative and remaining conversion uncertainty.

## Composition and category growth

Each category starts with its previous-election booth/category lean. A common
bounded shift recentres those leans so their current expected vote counts sum
to the seat's pre-election TPP. For example, if ordinary and early voting
account for 600 and 400 votes, a composition change that adds 12 Labor votes
to ordinary must remove 12 elsewhere if the seat total is unchanged. The
model allows such offsetting changes in either direction.

Reference alignment applies only to the historical comparison baseline. Both
recorded TCP and TPP inferred from received FP remain absolute vote estimates.
The FP conversion still uses configured preference flows and corrections from
matched FP/TCP evidence; it receives no shift to agree with the seat prior.
The prior influences the joint posterior through its covariance and the evidence
uncertainty, rather than by changing the observations themselves.

For example, if a booth's historical estimate is 40% but its comparison baseline
is recentered to 50%, a current FP-derived estimate of 40% stays at 40%. It is
negative evidence against that expectation. In a partial TCP count, counted
candidate totals and the estimated allocation of pending preferences likewise
retain their original values. Feed records, accepted-count history and raw-swing
diagnostics are unchanged.

The internal projected-vote account also uses received FP for physical booths
(including PPVC) with no measured or FP-inferred historical TPP comparison.
Counted major-party primaries stay with those parties, and only other candidates'
preferences are estimated. Any genuinely unreported voters retain the surrounding
baseline-and-swing projection. At full FP coverage the received account replaces
that surrounding estimate completely. Thus an unmatched booth cannot project fewer
Labor TPP votes than its counted Labor primaries. Preference uncertainty remains;
this does not label inferred TPP as a measured count. Historically comparable
booths retain their existing swing-based path, declaration accounts retain their
existing received/future split, and the optional-preferential path is unchanged.

Growing early voting supplies **no deterministic directional adjustment** to
Labor's vote. Its size affects accounting and uncertainty, but the historical
data do not identify which voters moved or their political preferences.

Shares are bounded using `z(p) = 25 log(p/(100-p))`, where `p` is a percentage.
Near 50%, one unit is approximately one percentage point. Recentring and
combining received ballots use the vote/share scale, not averages of log odds.
The Gaussian conditioning uses the derivatives of that count-scale account
around its prepared centre. This is a local linear approximation, not an exact
nonlinear posterior. Existing counted-vote bounds still constrain final draws.

## Shared and district-level differences

A category contrast means its change from the previous election **relative to
ordinary voting**, after the historical category lean is removed. It is not its
absolute Labor share. There are two kinds of contrast uncertainty:

- **Shared:** an election-wide difference, such as postals swinging less towards
  Labor than ordinary booths.
- **Local:** a district's departure from that shared pattern. Completing a
  district's ordinary count does not make its relationship to the national
  postal pattern exact.

Both calibrated covariance matrices are multiplied by `1.21`, giving 10% larger
standard deviations for shared and local category departures while preserving
all their correlations. This conservative tuning is applied after mapping any
combined channels, including uncertainty about their hidden split. The separate
broad allowance for unstudied categories remains unchanged. The original seat
prior covariance is still retained; the covariance-budget rule below can limit
the shared scale where necessary. This does not guarantee a 10% increase in
national posterior interval widths. The analysis sidecar records
`category_departure_sd_multiplier` (1.1) alongside the historical parameter version.

Local effects are integrated out as a correlated observation block for each
district. Their weighted sum cancels when all categories are observed. This
retains genuine knowledge of a complete count, unlike independent variance
floors on every category.

The shared effects use one election-wide reference mixture. Districts' deviations
from that mixture share covariance with their seat prior. This prevents the
update from treating differences in districts' postal fractions as a direct
regression explaining regional forecast misses. Ordinary-only results can still
inform an overall ordinary-versus-other departure; paired category counts supply
direct evidence about specific contrasts.

Writing `T` for the vector of seat totals, `D` for shared category contrasts and
`A` for district mixture departures, the joint prior uses
`Cov(T,D) = A Cov(D)`. The original `Cov(T,T)` is retained exactly. If that
connection would exceed its covariance budget, the shared category covariance
is reduced uniformly until the remaining covariance is positive semidefinite,
reserving 10% for other forecast errors. This is a numerical/model budget rule,
not a fitted coefficient. Actual baseline simulation draws are retained, and
category draws are generated conditional on them with this same covariance.

## Jurisdiction-specific observations

Underlying early-booth, early-absent, day-absent and postal effects remain
distinct. The observation follows the commission's actual published category:

| Feed category | Modelled observation |
| --- | --- |
| Federal ordinary PPVC | Early booth |
| Federal declaration PrePoll | Early absent |
| Federal Absent | Day absent |
| Queensland early booth / early absent / day absent | Separate effects |
| Victorian Early | Combined early booth and early absent |
| Victorian Absent | Day absent |
| NSW and WA (2025 onward) Absent | Combined early absent and day absent |
| NSW early booth; WA (2025 onward) in-person Early/PrePoll | Early booth |
| WA before 2025 in-person Early/PrePoll | Combined early booth and early absent |
| WA before 2025 Absent | Day absent |

When a combined feed does not identify the split, its covariance marginalises
an unknown mixture weight, uniformly distributed from zero to one. The mean
exposure is therefore one half of each component, **without asserting equal
component counts**. The additional variance of the unknown split is retained.
No hidden FP/TCP records are created. This is a conservative working assumption,
not an empirical estimate of the split. Published group shares and ballot totals
remain the observed quantities.

The existing partial-count and pending-preference allowances remain in the
observation error. In particular, a partly counted Victorian Early total is not
assumed representative of its final hidden mix. Optional-preferential TPP uses
continuing preferences, not all formal ballots, for its category weights.
Mobile, provisional and other uncalibrated channels retain separate broad
contrast priors (SD 5 on the transformed scale).

## Uncertainty in FP-derived TPP

The existing conversion of first preferences into TPP remains the point estimate.
It already adjusts configured preference flows using measured FP/TCP comparisons
where those are available. The joint update also allows shared uncertainty about
transferring those relationships to Greens and independent voter pools. Conditional
shared-error means can reflect disagreement with the seat priors; they are not
measured preference-flow corrections and are never fed back into booth conversion.
Such disagreement can also reflect reporting order or genuine regional movements.

FP-derived comparisons retain the existing averaging weight
`1 - 0.5 f`, where `f` is the fraction inferred. Their reporting-location and
sampling variance receives no additional generic FP penalty: the explicit
conversion allowance below accounts for preference uncertainty. This avoids
discounting a broad FP-reported sample twice relative to a small TCP-ready sample.
It changes the influence of evidence, not the conversion's vote estimate.

Conversion uncertainty has a local part (one district's preference correction)
and a shared part (the common preference correction). The unresolved preference
pool determines their exposure. Both Coalition partners count as Coalition for
this hypothetical TPP. A booth's non-major FP fraction is multiplied by the
fraction awaiting measured TPP, then averaged with the observation's comparison
weights. This fraction `q` is zero for measured TPP and large in FP-only
independent/Greens contests. For partial TCP, the pending cohort is assumed to
have the current FP pool's composition.

Each eligible matched FP/TCP location contributes
`reliability * P / (P + 500)` effective matches, where `P` is the number of
non-finalist first preferences whose transfers can be measured. Historical-flow
reliability and the existing preference-flow eligibility checks apply. Thus 500
transferred votes contribute half a fully reliable location, and a very large
booth contributes at most one. Large vote totals do not replace location breadth.

Local variance in percentage-points-squared is
`(4*qG + 35*(q-qG)) * (0.2 + 0.8 / (1 + 2 * E))`, with `qG` the unresolved
Greens fraction and `E` effective matches in the same district, pooled across
categories. Variance scales linearly with each pool, SD with its square root.
A 30% Greens pool has local SD 1.10 points without matches; the same other-party
pool has SD 3.24. Greens and other local errors have separate within-district
covariance components so categories with unlike voter pools do not inherit
each other's error. The residual fraction 0.2 protects against systematic
differences between matched and unmeasured locations. It does not prevent
convergence when measured TPP removes the unresolved pool.

The scale 35 is a conservative working setting: aligned final conversion errors
in 27 federal 2022 non-classic seats suggested approximately 29 per unit pool.
Prospective booth estimates checked against later complete TCP with unchanged
FP vectors support a substantial evidence reduction with a residual allowance.
These are diagnostic calibration results, not multi-election coverage guarantees.
The Greens coefficient 4 is a conservative working reduction supported by
roughly .5--.6 points RMS error in historical strength/history predictions of
the Greens contribution to non-classic seat TPP in two later federal elections.
It retains substantial uncertainty alongside the shared allowance below.

General shared variance is `35 * q / (1 + G)`, with `G` effective matches across the
election, pooled across categories. It has no local residual floor: measured
districts can establish the common correction even where an individual district
has no matches. Ordinary-booth matches therefore also support PPVC and
declaration conversions, consistent with the existing preference-flow estimate.
The local residual floor retains uncertainty about differences between measured
and unmeasured locations; cross-category evidence does not imply identical flows.
Convert both variances to the
`25 * log odds` scale using derivative `2500 / (p * (100 - p))`, where `p` is the
aligned observation percentage, using the same endpoint clamp as the transform.

There are also separate shared population-transfer allowances for Greens and
independent preferences. Even many classic booths cannot establish how well
their corrections transfer to different voter groups in non-classic contests.
For unresolved Greens and independent primary fractions `qG` and `qI`, their
initial SDs in seat TPP points are `2*qG` and `5*qI`. These are conservative working
flow-error scales, not precisely fitted cross-election correlations. For example,
an unresolved independent share of 30% has a 1.5-point shared TPP allowance.
These components start with zero means. Matched FP/TPP accounts can update their
means and covariance, while retaining half the initial variance for transfer to
different campaigns. They do not
impose a tactical-voting adjustment or assume all independents have equal mean
flows. [Historical party conversion](live-party-conversion.md) describes the
evidence, mean adjustments and limitations. Measured TPP removes all exposure.

The local allowance is correlated within a district. The general correction
alone retains the conservative precision budget `a`: expected district votes
divided by all expected election votes, including unreported services. Added
within-district residual covariance is
`local_greens_i*local_greens_j + local_other_i*local_other_j + general_i*general_j/a`.
All SDs here are transformed, including the unresolved pool and share derivative;
do not multiply by the inferred fraction again. The general bound preserves the
existing sparse-count protection and shrinks with pooled matching evidence.

Greens and independent transfer errors instead enter through two shared
states. Their observation loadings are `greens_i` and `independent_i`.
Writing these two loadings as a vector `F_i`, their calibrated covariance `U`
produces `F_i * U * F_j'` across any two observations,
including different districts. This avoids magnifying a modest shared error
into an enormous local error. The states are appended to the existing prior
without changing its seat covariance and sampled through the same conditional
draw path. Small within-seat blocks still suffice for numerical decorrelation.

Only matched conversion evidence can move these states away from zero. Source
predictions include the Greens strength relationship, historical corrections
and broad observed corrections with the tested district held out. A joint fit
allows for other-party error rather than attributing every mixed-pool residual
to Greens. See the calibration details in [historical party conversion](live-party-conversion.md).
Exported state means retain their original scale: multiply by 2 or 5 to obtain
equivalent flow percentage points. Positive means the conversion prediction was
too high for Labor. These means never modify the underlying booth flow
estimates or the next snapshot's prior. All allowances enter before conditioning;
there is no extra noise afterwards and no additional snapshot history.

Within each district/category, measured-TCP and FP-derived information form
separate observations of the same category movement. Let `f` be the fraction of
received FP votes awaiting preferences. A booth allocates information weight
`1 / (1 + (19*f/(1-f))²)` to the measured group, and the remainder to the
FP-derived group. At `f=1` the measured weight is zero. This smooth curve splits
information evenly at a 5% gap. Seven outstanding preferences out of 2,004 retain
99.56% measured weight, while a 25% gap retains only 2.43%: a substantial added
batch may have a very different primary composition from the earlier TCP batch.
The 5% midpoint is a working setting, not a fitted optimum. There is no one-vote
switch or hard cutoff. This curve changes information weights, not the actual
unresolved fraction used for vote accounting and preference-conversion error.

Both contributions use the existing measured-plus-pending-FP estimate. Its
unresolved preference fraction retains explicit conversion uncertainty in both
groups, with correlated conversion errors; a measured-group label does not erase
that uncertainty. Fully measured booths and FP-only booths still contribute
only to their respective groups. New FP-only booths therefore do not dilute
the measured group's estimate.

This partitions information rather than duplicating a booth. For original
comparison weight `w` and allocated fraction `a`, its mean weight is `a*w`,
while its contribution to the location-variance numerator is `a*w²`, rather
than `(a*w)²`. Thus two group contributions cannot each claim the original
booth's precision. Received-vote and location coverage are also multiplied by `a`;
both groups retain the full expected category population as their denominator.
They share the category error and retain scatter across the combined sample.
This conservative partition is continuous, but does not impose monotonic
precision when substantive new counts reveal heterogeneity or revise results.

All uncertainty enters before conditioning, so the same calculation determines
the mean's response and remaining covariance. No independent noise is added
afterwards. Direct TCP receives no conversion allowance and a fully measured
count retains its existing completion behaviour. Existing measured
preference-flow corrections and snapshot-history requirements are retained;
the historical party-flow priors are applied before those corrections.

## Extrapolating small received samples

A completed small booth is a reliable count of that booth, but only a weak
sample of its district. Without a separate allowance, its small averaging
weight cancels when calculating the variance of a one-booth average. That
average can then move the whole district and, through baseline correlations,
the national forecast too strongly.

Each district/category observation therefore receives extra sampling variance
`V * [5000 * (1 - c) / max(1, N)]^2`. Here `V` is the existing location/sampling
variance (including the combined sample's scatter), `N` is received votes capped
at each location's expected total, and `c` is `N` divided by the category's
expected total. Measured and conversion-dependent observation groups each use
their own received sample and the same full-category denominator. Votes are
counted before the FP comparison-weight discount; preference uncertainty remains
in its separate allowance.

The 5,000-vote scale is a smooth working setting, not a minimum count for using
evidence. When coverage is low, 1,000 votes multiply the location/sampling
variance by approximately 26, 5,000 by 2, and 20,000 by 1.0625. The extra
variance vanishes for a complete category. The update still uses every valid observation and adds no noise
after conditioning. Uncertainty about category differences and preference
conversion is not multiplied again.

The setting was selected by comparing early-response behaviour with the earlier
2022 federal checkpoint while retaining the newer update's later response.
It is not a multi-election calibration guarantee. Vote-size and location-count
alternatives were compared: a vote-size allowance was more effective at limiting
the influence of tiny completed booths without unnecessarily delaying broader
FP samples. It introduces no timestamps, election-specific branches, new replay
memory, or signed corrections to the evidence.

## Comparison quality near category completion

Early in counting, a booth with a reliable previous-election comparison has
more influence than a genuinely new or redistributed booth. As most votes in
a category become known, retaining that discount can instead distort the
category average. For example, a large new early-voting centre should receive
its actual vote weight once the early category is fully received, even if only
a smaller centre has a direct historical match.

The update therefore puts a smooth floor under each location's comparison
quality. Coverage is received FP votes divided by expected FP votes across the
whole district/category, before historical or preference-conversion discounts.
Each location's received votes are capped at its own expected total for this
calculation. Unreported locations retain their expected size in the denominator;
explicitly closed services have no expected votes. Coverage uses formal FP
votes even under optional preferential voting, where some later exhaust.
It is weighted by vote size, so a tiny outstanding hospital booth cannot hold
back a nearly complete category as much as a large unreported booth.

For coverage `c`, set `t = clamp((c - 0.75) / 0.25, 0, 1)`. The minimum
quality is `t²(3 - 2t)`: zero through 75% coverage, 0.104 at 80%, 0.648 at 90%,
0.896 at 95%, and 1 at 100%. Existing quality is retained whenever higher.
The curve flattens at both ends, avoiding a jump at its start or when the last
small location reports. The 75% start is a working setting: Queensland saved
evidence trials found little difference between starts at 70%, 75% and 80%.
Those trials assess reweighting of the joint update, not full simulation replay
or multi-election calibration.

The same whole-category floor applies to measured and FP-derived location
groups. Their weighted observed shares, historical comparison shares, scatter
and preference-pool exposures are rebuilt together. Increasing precision alone
would give extra authority to an average still dominated by the old matches.
Uncertainty about uncounted preferences remains separate, including when FP
coverage reaches 100%.

A category can also finish with very few votes. Its exact count is then known,
but it remains weak evidence for a general district or election-wide swing.
To retain that distinction, location/sampling variance after reweighting is
bounded below by `V_old × (5000 / (5000 + N))²`, where `V_old` is the variance
under the original comparison weights, including its sparse-sample allowance,
and `N` is whole-category received FP votes capped as above. The revised
variance is used if larger. At 100 votes the bound retains approximately 96%
of the original variance; at 20,000 it retains 4%. This reuses the existing
5,000-vote scale without a new exclusion rule or category-specific exception.
It concerns extrapolation from a small sample, not errors in counted integers:
the forecast's counted-vote bounds remain in force. Category and conversion
covariances are not multiplied by this bound.

## Shared ordinary reporting-order uncertainty

Early ordinary booths need not represent the full category. The smaller set with
completed TCP can also differ from the larger set with FP reported. Independent
district errors would average away too quickly if the same reporting pattern
affects many districts, such as small rural booths finishing first.

Two correlated, zero-mean error terms represent these samples: one for rows still
allocated to the FP-derived group and one to the measured-TCP group. They enter the
observation model before conditioning. They do not change the seat/category
prior covariance or add noise to the final simulation afterwards. Their
correlation is 0.5, allowing shared geography without treating the two samples
as identical. This first implementation applies only to ordinary-day booths.

For each sample, coverage `c` is the smaller of its election-wide vote coverage
and location coverage. Vote coverage divides reported votes by expected ordinary
votes. Location coverage averages each booth's reported fraction, including
unreported booths as zero. For TCP coverage, only the measured fraction counts.
Using the smaller coverage prevents either many tiny booths or one large booth
from prematurely exhausting the allowance. These measures use current counts,
without the FP comparison-weight discount and without new snapshot memory.

Each error's base SD on the `25 * log odds` scale is
`1.5 * (1 - c) / (1 + c / 0.3)`. It starts at 1.5 (approximately percentage
points near 50% TPP), falls to 1.0125 at 10% coverage and reaches zero at full
coverage. The scale, decay and correlation are conservative first-pass settings,
not fitted optimal parameters. They allow the broader FP sample to carry useful
information while limiting extrapolation from the smaller TCP-ready sample.
This reporting-order allowance does not itself alter booth comparison weights;
the separate completion floor above lifts them as a category becomes complete.

A district's completed ordinary category can justify lifting its local comparison
weight before it represents the election as a whole. For each ordinary observation,
let `u = max(0, 1 - original weight / restored weight)` be the fraction of its
comparison weight supplied by the completion floor. Both weights come from the
current snapshot. An additional allowance is `a = 5 * u * (1 - c_FP)`, where
`c_FP` is election-wide ordinary FP coverage as defined above. The observation's
loading on its existing shared reporting-order factor becomes
`sqrt(base SD² + a²)`. For example, restoring weight from 200 to 1,000 gives
`u = 0.8`; at 50% ordinary FP coverage, `a = 2` transformed points.

This increases exposure to the same shared factor, rather than adding another
independent error. Booth estimates and local comparison weights are retained,
while the collective estimate responds more cautiously to restored weight in
early-reporting districts. With no weight restored, only the base allowance
applies. At full ordinary FP coverage the extra allowance vanishes, including for
the measured TCP subset. PPVC and declaration observations are unaffected by this
extra allowance. The decay scale 0.3 and compensation strength 5 are working
settings checked in retained-evidence sensitivity trials, not fitted optima.

## Historical calibration

The calibration uses all 14 usable successive-election comparisons in the
collected data: seven federal, four Victorian, two NSW and one Queensland.
It requires at least 30 common named districts with the needed categories in
both elections. WA lacks a sufficiently complete comparable 2021 TPP sample;
historical SA batching is unsuitable. The
[source research](../analysis/reports/live-category-tpp/README.md) and
[separate-category analysis](../analysis/reports/live-category-tpp/declaration-prepoll.md)
describe the source definitions and coverage.

Each election receives equal total weight. Election-wide squared contrasts
estimate shared covariance; within-election centred district contrasts estimate
local covariance. Both are regularised towards independent zero-mean contrasts,
with eight prior-election equivalents, starting SDs of 2 shared and 4 local.
These regularisation choices are conservative working settings, not claimed
optimal estimates. The fitted means remain zero. No category-growth slope,
election-specific directional offset or retrospective final-result target is
applied by the live forecast.

| Contrast relative to ordinary | Shared SD | Local SD |
| --- | ---: | ---: |
| Early booth | 1.83 | 3.38 |
| Early absent | 1.76 | 3.55 |
| Day absent | 1.83 | 3.48 |
| Postal | 2.50 | 3.35 |

These standard deviations use `25 × log odds`; they are not percentage-point
errors for every share. The full matrices also retain regularised correlations.
Withholding one election at a time gives shared SD ranges of 1.70–1.89,
1.71–1.81, 1.68–1.89 and 2.24–2.58 respectively. That is a parameter-stability
check, not a calibration assessment of forecast interval coverage.

Federal early subtypes are combined only for historical comparisons spanning
their pre-2010 reporting change. Subsequent federal comparisons keep them
separate. For NSW 2019–2023, the old postal-plus-iVote channel is compared with
postals after iVote disappeared. Named districts are not boundary-matched, and
adjacent comparisons share elections. These limitations support regularisation
and further replay checks rather than precise interpretation of small correlations.

From `analysis/`, reproduce the fixed C++ parameter header and calibration report:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.calibrate_tpp_composition
```

This uses the already collected `downloads/category-tpp-research/normalized.json`;
it makes no downloads and runs no Stan model. The report records the source
checksum, included/excluded comparisons and leave-one-election-out SDs.

## Verification and diagnostics

`LiveTppCompositionTests` checks count-scale accounting, grouped observations,
unknown splits, covariance budgets, correlated draws, local cancellation,
fully counted limits and a fictional regional-miss regression. It also checks
removal of the overlapping FP variance penalty, cross-category matched-location
precision, reporting coverage and shared reporting-order covariance, sparse-sample
attenuation and its complete-count limit, completion-quality curves and
vote-weighted full-category accounting, a shared completion floor across
measured/inferred groups, small completed-category precision, preservation
of measured information when FP-only locations arrive, the general shared covariance bound,
no artificial precision from splitting observations, smooth replacement by TCP,
party-sensitive local precision, exact shared-factor versus dense covariance,
order-independent conditioning, and scenario moments. Reference-alignment
regressions keep absolute received FP/TCP estimates unchanged when the prior
changes, including Baringa's FP-to-TCP handover. `LiveDataTests` checks the
unmatched-booth allocation, exact primary-vote lower bounds under preference
variation, and separation of received voters from future voters. These suites
are included in the portable C++ test runners. Python calibration tests use
fictional data.

Private analysis exports identify this implementation as
`joint_category_composition_v17`, record its covariance-budget scale and each
seat's booth-reference shift, and retain
readable observations before numerical decorrelation. Their residual variances
include local category variation and local/general conversion allowances. The
party-transfer contribution is represented by its shared state loadings and is
excluded from residual variance to avoid counting it twice. `conversion_variance`
includes all conversion components on the marginal observation diagonal. Each raw row
also reports its measured/inferred location group, inferred fraction, local
and shared effective matched-location counts, both conversion SDs, and the
resulting conversion variance. Each observation also records its received sample
votes, category coverage and added sparse-sample variance; the export records
the 5,000-vote setting as `sparse_sample_vote_scale`. Whole-category FP coverage
and the comparison floor are `category_fp_coverage` and
`comparison_quality_floor`. `unfaded_location_variance` records the original
location/sampling variance, while `retained_small_sample_variance` is the extra
variance needed to meet the small-category bound after reweighting. The export
also records `comparison_completion_start` and `comparison_completion_curve`.
`restored_comparison_fraction` records `u`, and `compensation_reporting_order_sd`
records `a`. The exported `reporting_order_loadings` include their combined effect;
the election-wide ordinary FP/TCP order SD fields retain the base allowance alone.
`reporting_order_compensation_strength` records the multiplier 5.
The conversion method is
`matched_conversion_with_external_transfer_states`: row precision uses pooled matches, while
category summaries retain separate match counts for inspection. The export also
includes the full prior covariance and separate reporting-order and party-transfer loadings, states,
coverage and parameter settings for reproducible numerical comparisons. These
large arrays stay in the analysis sidecar, outside compact viewer history.
The two party-transfer states have kind `party_transfer`; their means and
covariance come only from the matched-account calibration and remain unchanged
by the seat update. `matched_transfer_calibration` records the contributing
districts, expected/observed preference allocations, group sizes, total mismatches
and working uncertainty settings. Simulated
turnout and rechecking responses
remain separate; the old additive TPP composition noise remains disabled.

Live Booths' final dropdown entry, **Internal 2PP Metrics**, also shows these
compact diagnostics as columns for each snapshot. Table headings are abbreviated;
hover over a heading for its full name and an explanation. Metric columns use
84 pixels; the snapshot timestamp retains its wider column. Prior SD, the legacy
conversion SD and the constant preference-pool K remain in exported diagnostics
but are not displayed as columns:

| Column | Interpretation |
| --- | --- |
| Joint centre 2PP (%) | Weighted seat centres after joint conditioning, converted to shares before aggregation. This precedes simulation count bounds and turnout/recheck draws, so it is not the final simulation median or mean. |
| Joint SD (z) | Standard deviation of the weighted seat totals after conditioning, including cross-seat covariance. `z` means `25 × log odds`, approximately percentage points near 50%. |
| Variance retained (%) | `100 × (Joint SD / Prior SD)²`; lower values mean more uncertainty removed by the observations. Undefined for a zero-variance prior. |
| FP-derived (%) | Inferred fraction of received, quality-weighted TPP evidence. Its denominator is the evidence used by the update, not all expected election ballots. |
| Unresolved pref. % | Comparison-weighted unresolved preference percentage, including measured observations with zero pool. |
| Local conv. SD (z) | Comparison-weighted RMS local conversion SD across observations; not a national aggregate SD. |
| Shared conv. SD (z) | Comparison-weighted RMS shared conversion SD including pool exposure. General correction is shown before its budget scaling; from v10 the party-transfer factors use direct cross-seat covariance without budget scaling. Older archives retain their original meanings. |
| Matched pref. units | Sum of effective matched locations across all categories. Category-specific and district-specific counts are in the analysis sidecar. |
| GRN/IND err. | Shared residual flow error from held-out-district FP/TPP comparisons. Positive means the conversion predicted too many Labor preferences. Units are percentage points of that party's preference flow, not seat TPP. |
| GRN/IND err. SD | Remaining uncertainty in those flow errors, including the retained classic-to-non-classic transfer allowance. |
| Ordinary FP/TCP order SD (z) | Current input SDs for shared ordinary reporting-order errors, before conditioning; not posterior SDs. |
| Ordinary FP/TCP coverage (%) | The smaller of election-wide vote and location coverage for each reporting sample; denominators include unreported ordinary booths. |
| Category cov. scale (%) | Percentage of the historical shared category covariance retained within the baseline covariance budget; 100 means no reduction. It can fall below 100 when the full category covariance does not fit that budget, so it remains visible. |

The centre and aggregate SDs use the same historical seat vote weights as the
national simulation histogram. These columns are stored in the small snapshot
summary and append through the existing asynchronous viewer. They require no
full analysis-file reads. Older snapshots without these fields show blank cells;
they are not retrospectively calculated. The existing graph series remain
unchanged because the new columns mix shares, transformed units and ratios.

Conditioning at a fixed conversion-error value reduces covariance for a fixed
prior and compatible cumulative evidence. Propagating uncertainty over the
externally calibrated errors does not guarantee monotonic marginal covariance.
Nor does this guarantee every reported percentile width decreases:
turnout/rechecks, changed input accounts, bounded transforms and simulation
sampling also affect the displayed distribution. A full replay is needed to
assess the centre and interval coverage in actual election sequences.

The conversion allowance is intentionally conservative and needs replay checks
for practical information loss as well as interval coverage. From v11, its
distribution is learned from matched accounts separately; the scenario update
keeps each shared error draw fixed while conditioning seat/category draws.
Diagnostics propagate that same sensitivity and covariance. This avoids learning
a conversion error from seat-prior disagreement, but is a deliberately modular
calculation rather than an unrestricted joint Bayesian posterior.
Shared early reporting-order uncertainty is handled separately from the
historical final-category calibration, using the working settings above.

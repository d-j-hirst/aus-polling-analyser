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

### Matched historical notional TPP

When the previous contest's actual TCP involved Greens or an independent, its
Labor/Coalition TPP can still have been counted separately. The live forecast
uses these **notional TPP counts** to fill a missing historical preference
comparison. It keeps the original TCP pairing and counts separate. A current
measured TCP remains fixed; historical TPP neither adds current votes nor marks
the current count complete.

The ordinary path is unchanged: an existing usable historical classic TCP keeps
its established pooled preference calculation. The additional path requires an
exact match to the old district, polling-place ID or declaration type, and the
complete positive candidate FP vector **before** model parties are grouped.
An omitted zero candidate is harmless; equal totals with different candidate
counts do not qualify. Synthetic historical allocations cannot supply this match.

Starting rates already include available historical Greens and exact-name
independent information. The new calculation fits only their remaining error:
it holds major primaries fixed and finds one common shift in the other
candidates' preference log odds that reconstructs the old TPP. Log odds is a
different scale for probabilities; shifting there and converting back keeps
every flow between zero and 100%, while preserving different parties' starting
rates. The actual old party mix determines the shift. This cannot identify which
candidate caused a discrepancy or guarantee its persistence at the next election.
The existing half-vote endpoint smoothing is retained. The historical fit runs
once during preparation and is shared unchanged by scenario draws.

An explicit discount for an exceptional previous Greens campaign, such as
Melbourne in 2019, also applies to this additional route. Its excluded component
is explained by the observed historical Greens flow before fitting the aggregate
residual. It therefore cannot reappear indirectly as a common preference shift.
The historical count/comparison stays unchanged, and normal source districts
retain the usual historical party rates.

Current preference movement on this added path compares equivalent mixture fits
on aligned old and current accounts. Mixing an exact old fit with a pooled
current approximation could report movement from identical counts. Existing
classic comparisons retain their pooled calculations. Partial or revised current
counts keep their normal cohort/recheck handling and cannot train this new exact
fit without an aligned FP/TPP population. Current booth/seat/regional preference
corrections then describe movement **after** the historical offset, rather than
adding the historical effect again. Existing uncertainty about current preferences
and shared conversion errors remains in place.

The same actual old TPP is used in booth and whole-category comparison accounts.
Leaving a generic old estimate in the reference after fitting preferences to
the actual old count would make seat reference centering compensate for the
same old conversion error twice. This does not copy a category/district preference
offset to unmatched current locations: the additional rate correction requires
the direct population match described above.

Eligibility depends on the missing historical comparison, rather than the
current contest's eventual pairing. It therefore also covers a current classic
seat with a matched non-classic predecessor and stays stable when today's TCP
pairing changes. It affects automatic live analysis only; pre-election simulations
do not read this input.

The maintained [count table](../analysis/Data/historical-notional-tpp.csv) contains
7,116 accounts from the 2010, 2013, 2016, 2019, 2022 and 2025 federal elections.
Only previous non-classic districts are included. Dual Liberal/National source
districts are outside this addition; their existing comparisons retain their
normal handling. Separate non-classic declaration TPP is unavailable before
2016 and remains missing. Other jurisdictions without these additional counts
retain their existing conversion. Optional-preferential elections require a
separate exhaustion treatment and do not use this table.

The [source manifest](../analysis/Data/historical-notional-tpp-sources.json) records
commission URLs, response/archive hashes, coverage and exclusions. Reproduce from
the stored final commission feed ZIPs and the cached AEC downloads, from the
repository root, without fitting a model:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\scripts\diagnostics\collect_historical_preference_counts.py --check
```

Omit `--check` to regenerate the two maintained files; `--output-dir` writes a
review copy. Missing public downloads require explicit `--download`. Runtime
loading selects only the configured previous election, strictly earlier and in
the same jurisdiction. Missing optional coverage retains the usual fallback.
Analysis sidecars record each added account under `historical_notional_tpp`,
including the source district, mapped old votes and the prepared offset in
`25 × log odds` units. Raw old/current TCP maps remain unchanged.

Controlled FP-to-TPP tests across five federal elections used complete FP while
withholding each tested seat's current TPP and local correction. Adding matched
history while preserving existing arithmetic reduced mean absolute error in 95
non-classic seat results from approximately 3.28 to 2.58 Labor TPP percentage
points. Classic-seat error was essentially unchanged across 583 results. These
are preference-conversion checks, not full live forecasts or interval calibration.
They motivated the coverage choice; production reference alignment and current
preference learning also require replay checks.
Candidates and seats recur, and significant changes such as Mayo 2022 can still
defeat their historical correction. Current measured counts and uncertainty
remain necessary; the historical counts do not determine today's preferences.

### Estimated and synthetic comparisons

Where historical TPP must be estimated, whole-category comparisons use the same
available historical preference information as matched booths: district Greens
strength/history and exact-name independent candidate history. The information
is read before either account is prepared, and the original district supplies
it when a seat is renamed or booths transfer. This assumes the district rates
are useful for its categories; it does not treat them as measured category flows.
Actual historical classic TCP retains precedence. Parties/candidates with no
usable history keep their configured preference estimates, and existing
synthetic-baseline uncertainty remains in place. This prevents an unmatched
early-voting centre from losing preference information already used for matched
locations and thereby creating an artificial seat-wide reference adjustment.

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

Historical comparisons and their current FP-derived counterparts receive the
same common seat reference offset. This reconciles two starting points: the
whole-seat forecast anchored to the configured previous result, and booth TPP
estimates reconstructed using preference assumptions. If both historical and
current conversion estimates share an error, shifting only the historical
comparison would incorrectly interpret that error as new swing. For example,
an unchanged FP-derived estimate of 40% should show no movement when its
historical comparison is recentered from 40% to 50%: its comparison estimate
also becomes 50%. Actual measured TCP remains an absolute count and never moves.

This reference calculation uses historical comparisons and expected final vote
sizes, not current TCP reporting confidence. It supplies one offset per seat,
shared across vote categories. Ordinary and early categories can have different
historical levels without receiving independently fitted corrections. The
separate preference-flow correction still pools measured FP/TCP evidence across
categories through the booth, seat, region and election hierarchy.

Only a detached inferred comparison is translated. For a mixed FP/TCP account,
the translation applies to the pending allocation while measured candidate
counts stay fixed. The stored raw FP estimate, feed records, accepted-count
history, raw-swing diagnostics and direct received-vote replacement are unchanged.
The offset assumes a historical conversion discrepancy carries into the current
estimate; changed candidate composition or preference behaviour can weaken that
assumption. Existing preference-flow learning and conversion uncertainty remain
in use. See `LiveTppIntegration::alignInferredReference` for the comparison rule.

Historical category classification must survive venue replacements. The
Queensland reader applies the same physical-venue rules to matched, unmatched
and ambiguity-detached historical records: Returning Officer and Early Voting
Centre venues are early voting; Mobile Polling is mobile voting. Booth identity
matching does not change those channels. In the 2020 Caloundra records, the
10,658-vote Returning Officer count and 32-vote CBD count together give 49.58%
Labor early TPP. Dropping the former into ordinary voting would instead make
62.5% from just 32 votes the comparison for large replacement early centres,
distorting the common reference offset. Source candidate counts are retained.

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

The booth's raw TPP estimate retains the existing conversion of first preferences.
It already adjusts configured preference flows using measured FP/TCP comparisons
where those are available. Its detached joint-model comparison also receives the
reference alignment described above. The joint update allows shared uncertainty about
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

Each district/category supplies **one observation**, assembled from measured
TCP, received FP awaiting preferences, and genuinely uncounted votes. Measured
TCP and known major-party FP receive their full vote weight. Only the unresolved
preferences and uncounted population need an allocation estimate. For example,
Caloundra's early category at 22:41 on 26 October 2024 has 4,894 measured Labor
TCP votes, 3,519 Labor FP awaiting TCP, and an estimated 902 preferences from
1,486 non-major ballots. Their sum is approximately 47.32% of 19,686 early votes.
A high-Labor centre's TCP arrival does not separately extrapolate its own share
over those already received ballots.

The uncounted population starts with its own expected historical booth/category
lean, aligned to the seat prior as described above. Raw current comparisons
supply its swing estimate, before any further joint prior shrinkage; prior-shrunk
future projections are not reused as new evidence. Measured and FP-derived
comparisons retain separate precision privately when estimating that swing.
Their covariance includes shared preference and reporting-order errors. A
nonnegative minimum-variance average prevents new FP-only information from
weakening an already available measured comparison's precision. It also avoids
extrapolating beyond both comparisons when their errors are nearly identical.
This is a modelling safeguard, not an empirically fitted source-selection rule.

Let `f` be the fraction of received FP awaiting preferences. A booth allocates
private comparison information weight `1 / (1 + (19*f/(1-f))^2)` to the measured
source and the remainder to the FP-derived source. The 5% midpoint remains a
working setting. Small rechecks therefore change information continuously;
large unmatched batches do not inherit the precision of an earlier TCP batch.
The partition applies to the existing measured-plus-pending-FP estimate and
retains scatter across both sources. It is not a partition of physical votes
into two competing whole-category results.

Received preferences retain the existing party-pool, matched-evidence and shared
conversion allowances. Their effect is scaled by the actual affected population.
For fixed category size `N`, pending FP population `F` and uncounted population
`U`, a share error on pending FP contributes `F/N` times that error to category
TPP; a transformed swing error on future places also uses the derivative for
their partisan lean. The local conversion and shared transfer errors affecting
both parts are added before their covariance is calculated. They are correlated,
not two independent new errors. Received major-party primaries never acquire
preference-conversion error. Unreconciled candidate revisions retain the existing
batch-sized transfer-interval allowance.

These category accounts enter the existing whole-seat joint update. The final
whole-seat drawing, count bounds, turnout/recheck scenario adjustments and
classic/non-classic result routing remain unchanged. This is not a replacement
which assembles the final seat result directly from sampled category totals.
Categories without a usable historical baseline retain their separate direct
received-vote replacement, without teaching an unsupported swing.

## Extrapolating small received samples

A completed small booth is a reliable count of that booth, but only a weak
sample of its district. Without a separate allowance, its small averaging
weight cancels when calculating the variance of a one-booth average. That
average can then move the whole district and, through baseline correlations,
the national forecast too strongly.

Each district/category observation therefore receives extra sampling variance
`V * [5000 * (1 - c) / max(1, N)]^2`. Here `V` is the existing location/sampling
variance (including the combined sample's scatter), `N` is usable received votes
across both source groups, capped at each location's expected total, and `c` is
`N` divided by the category's expected total. Both groups use this same combined
count, before historical-quality or FP comparison-weight discounts. A record
without a usable TPP comparison contributes no received votes to this sample;
its expected size remains in the denominator. Counts are combined only within
one district and voting category, keeping ordinary, early and postal evidence
separate. Under optional preferences the comparison account uses continuing
votes; exhausted ballots are outside that account.

For example, dividing a 3,000-vote sample equally between measured and FP-derived
observations leaves 3,000 votes for both sparse multipliers. Each observation
still receives only its share of the comparison information. Its mean,
location scatter and preference uncertainty remain separate. Treating the two
groups as independent 1,500-vote samples would penalise the same evidence merely
because some preferences had been counted and others had not.

The underlying location variance decreases as more of the expected category
supplies a usable comparison. Its counting-completion multiplier is one minus
combined comparison weight divided by expected votes. Comparison weight retains
the existing quality discounts, unlike the undiscounted physical coverage `c`
above. A low-quality historical match therefore still slows the completion
adjustment, but splitting its information cannot slow it again.

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

## Received votes and category completion

Full FP coverage establishes which voters are represented by the category,
not the missing preferences or the representativeness of its TCP-ready subset.
The update therefore uses actual vote weights in the received account, without
raising an unmatched booth's historical comparison quality because other
locations have reported FP. A new booth can contribute all its counted votes
while supplying only weak evidence about the swing at still-unreported places.
At complete FP coverage there are no such places to extrapolate over; uncertainty
in the category allocation remains only in preferences and potential revisions.
For OPV, received and expected quantities use continuing TPP ballots. Actual
TCP remains exact; FP-only continuing totals retain the existing projected
exhaustion allowance rather than treating exhausted FP as outstanding TPP.

A precisely counted small category can still be weak evidence about the wider
seat or other seats. The existing comparison floor is retained for that transfer.
For private source comparison variance `C` and combined usable sample `N`, it is
`C * (5000 / (5000 + N))^2`. The two sources' floors are combined with squared
future-comparison weights; the unified observation uses the larger of that
comparison floor and its remaining-location variance. This does not change its
received-vote mean or invent missing ballots. It preserves the protection for
sparse mobile, hospital, prison and remote categories after they complete.
The 5,000-vote scale is unchanged and has not been recalibrated by this change.

The source variances keep the preceding sparse-sample adjustments before they
are combined. Remaining-location variance and reporting-order exposure are
then scaled by the uncounted part of the final account. A numerical residual
variance of at least `1e-8` squared transformed points permits the existing
matrix solver to handle fully known categories whose comparison errors cancel.
This corresponds to SD 0.0001 percentage points near 50:50, not a substantive
vote-count uncertainty allowance. Published final results retain their exact
override path.

## Shared ordinary reporting-order uncertainty

Early ordinary booths need not represent the full category. The smaller set with
completed TCP can also differ from the larger set with FP reported. Independent
district errors would average away too quickly if the same reporting pattern
affects many districts, such as small rural booths finishing first.

Two correlated, zero-mean error terms represent these samples: one for rows still
allocated to the FP-derived group and one to the measured-TCP group. The private comparisons retain these errors when predicting uncounted
locations. Their weighted loadings are scaled to that remaining population in
the single category observation before conditioning. They do not change the seat/category
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
attenuation, a persistent sparse-comparison floor at category completion, legacy completion-curve arithmetic and unified received-vote accounting,
Caloundra's source handover, Ryan-style measured-precision retention,
partial/revised counts, small completed-category transfer precision, preservation
of measured information when FP-only locations arrive, the general shared covariance bound,
no artificial precision from splitting observations, smooth replacement by TCP,
party-sensitive local precision, exact shared-factor versus dense covariance,
order-independent conditioning, and scenario moments. Reference-alignment
regressions check that shared historical/current conversion gaps do not create
spurious swing, including heterogeneous partial reporting, unchanged measured
TCP and preservation of measured votes inside mixed accounts. Caloundra/Baringa
checks distinguish the faulty and corrected historical offsets. Parser-enabled
`LiveInputRecoveryTests` retain early/mobile categories for replaced historical
venues and preserve their candidate totals. `LiveDataTests` checks the
unmatched-booth allocation, exact primary-vote lower bounds under preference
variation, and separation of received voters from future voters. These suites
are included in the portable C++ test runners. Python calibration tests use
fictional data.

Private analysis exports identify this implementation as
`joint_category_composition_v20`, record its covariance-budget scale and each
seat's booth-reference shift, and retain
readable observations before numerical decorrelation. Their residual variances
include local category variation and local/general conversion allowances. The
party-transfer contribution is represented by its shared state loadings and is
excluded from residual variance to avoid counting it twice. `conversion_variance`
includes all conversion components on the marginal observation diagonal. Each raw row
also reports its measured/inferred location group, inferred fraction, local
and shared effective matched-location counts, both conversion SDs, and the
resulting conversion variance. Each observation retains its source-group sample
votes and coverage as `sample_votes` and `sample_coverage`. The combined usable
sample actually used for the sparse multiplier and comparison floor is
`category_sample_votes` and `category_sample_coverage`;
`category_comparison_coverage` includes quality discounts and controls counting
completion. These combined values repeat on both source rows and must not be
added together. `sparse_sample_variance` records the resulting extra variance.
Older exports without the `category_sample_*` fields used source-group counts
for these adjustments. The export records the unchanged 5,000-vote setting as
`sparse_sample_vote_scale`. Whole-category FP coverage is recorded as `category_fp_coverage`. Method
`joint_category_composition_v21` replaces the former FP-completion comparison
boost with `comparison_completion_curve=received_vote_account`; the old
`comparison_quality_floor`, `restored_comparison_fraction` and compensation
fields remain zero for new observations. Older archives retain their original
meaning. The location group is `unified` and each observation additionally records
`received_votes`, `received_first_votes`, `pending_preference_votes`,
`remaining_votes`, `category_estimate` and `remaining_estimate`. The latter two
are absolute Labor percentages within that category or its remaining population,
not whole-seat TPP. `measured_future_weight` and `inferred_future_weight` describe
the private comparison average for uncounted places; they are not vote weights.
`unfaded_location_variance` is the allocation variance before the persistent
comparison floor. `comparison_variance_floor` and `retained_comparison_variance`
describe the floor and its contribution in squared transformed units. Shared
reporting-order loadings refer only to the uncounted component. Existing
whole-seat `posterior_mean` and `accounted_posterior_mean` diagnostics keep their
previous meanings; a category estimate is an input to those quantities.
`pending_preference_votes` counts all received FP awaiting TCP, including known
major primaries; the unresolved non-major pool can be smaller. In unified rows,
`inferred_fraction` and `unresolved_preference_fraction` divide those pending FP
and received non-major pool quantities by the expected category total. Their
conversion scales additionally include the separate future-prediction exposure.
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

The centre and aggregate SDs use the same live aggregation phase-in as the
national simulation histogram, using prepared mean sizes rather than sampled
scenario sizes. The phase starts at historical weights and reaches current
turnout weights at 50% FP coverage; [the turnout documentation](live-turnout.md#combining-seat-forecasts-into-election-totals)
defines its formula, practical rationale and scope. This changes only aggregate
reporting, not the joint conditioning or seat forecasts. These columns are stored in the small snapshot
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

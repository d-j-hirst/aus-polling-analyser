# Historical party preference conversion

This component improves the conversion of first preferences to a hypothetical
Labor-versus-Coalition TPP where that count is unavailable. It uses previous
candidate preference destinations to distinguish different voter groups, while
retaining uncertainty about how well those relationships transfer. It does not
change counted TCP, select the final pair, or change the pre-election forecast.

A candidate's **flow** is the proportion of their primary ballots that rank
Labor ahead of the leading Coalition candidate. A five-percentage-point error
in that flow, for a candidate with 30% FP, produces a 1.5-point seat TPP error.
The flow denominator is that candidate's FP; the TPP denominator is formal votes.
The historical mean adjustment therefore applies only to compulsory-preferential
elections. Optional exhaustion needs a separate continuing-vote denominator.

## Greens and returning independents

Greens voters in districts with strong Greens support have historically been
more likely to prefer Labor over the Coalition. The model applies this modest
district-level relationship on log odds, `logit(p) = log(p/(1-p))`:

`logit(flow) = logit(configured flow) + 0.45*(district strength - reference strength) + 0.5*historical residual`.

Strength is the log odds of district Greens FP. The reference is the mean
strength across classic districts in the comparison election. The historical
residual is the old district's measured log-odds flow minus its classic-election
mean flow and the strength effect. Half retention guards against treating one
old result as permanent. Observed historical flows use half-vote smoothing:
`(Labor transfers + 0.5)/(candidate FP + 1)`.

The same district strength applies to every booth. It begins at the baseline
Greens FP median, then blends towards the received district FP with weight
`received formal votes/(received formal votes + 5000)`, on log odds. Thus a single
80-vote booth changes strength very little. This is a conversion-specific
stabiliser; the existing sparse-sample protection for district/national TPP
movement still applies. No target-election final TPP enters this calculation.

Booth matches retain their previous district name. A redistributed district's
historical residual averages source-district residuals by matched previous
formal votes. Missing source history contributes no residual. This is a modest
transfer of district information, not a reconstruction of individual voters.

For independents, only a unique match of previous district and complete
normalised candidate name supplies a historical mean. Name tokens are sorted
to accommodate surname-first commission displays; surname alone is insufficient.
Current candidates can match source districts of redistributed booths. The
previous measured flow is carried forward, shifted by any configured change in
the generic preference rate. Existing current observed corrections then apply.
New candidates retain the existing generic mean. No final-pair classification,
electorate-wide Labor/Greens FP decline, or inferred tactical-voting flag is used.

The same party-flow method is used when measuring old booth residuals and
when estimating current TPP. This prevents adding the historical Greens effect
twice. An explicit forward-reuse discount, described below, applies only to the
current estimate; the historical account retains its original residual.
Existing seat/region/election observed preference corrections still learn
current movement. Partial preferences and future declaration votes use the same
party priors. Fully counted TPP remains the actual count.

## Evidence and uncertainty

Actual candidate destinations from 2019, 2022 and 2025 federal elections support
the strength relationship. Training on 2019 and predicting 2022 gives a strength
coefficient of 0.432; training on 2019 and 2022 and predicting 2025 gives 0.469.
Training on classic districts alone gives similar coefficients. A rounded 0.45
is a working setting, not a precise universal constant.

With each test election's classic seats supplying its national level, equal-seat
RMS errors in the Greens contribution to non-classic seat TPP were:

| Conversion method | 2022, 27 seats | 2025, 34 seats |
|---|---:|---:|
| Pooled classic flow | 1.468 | 1.108 |
| Historical strength relationship | 0.615 | 0.558 |
| Strength plus half historical residual | 0.521 | 0.480 |

Units are percentage points of **whole-seat TPP**, not flow points. These are
completed-count information tests, not election-night replay coverage tests.
The live implementation starts from configured flow levels and existing
observed booth corrections; it does not have that completed national calibration
at the start of counting.

Independents are less interchangeable. Their vote-weighted flows in substantial
non-classic campaigns exceeded those of substantial classic campaigns in all
three studied elections. Small classic independents had still lower flows.
Former major-party candidates and different campaigns within one district can
have very different flows. Same-candidate history helps, but changes such as
Fowler's between 2022 and 2025 show that it is not exact.

The joint update distinguishes local Greens uncertainty from the other non-major
pool. Its local variance is `(4*qG + 35*(q-qG))*r(E)` in squared TPP percentage
points: `qG` is the unresolved Greens share, `q` the whole unresolved non-major
share, and `r(E)` the remaining fraction of variance after local matched evidence.
A 30% Greens pool with no local matches therefore has 1.10 points local SD,
compared with 3.24 for a 30% other-party pool. The smaller Greens coefficient
reflects the historical tests above; it is conservative rather than a fitted
optimum. Independents and other parties retain the existing allowance until
party-specific evidence supports further distinctions.

Separate shared flow-error scales remain **2 points for Greens** and **5 for
independents**. They enter as two common observation-error states, with actual
cross-seat covariance rather than enlarged per-district variances. Thus the
30% Greens pool also has .60 points of shared TPP uncertainty, without multiplying
it by the square root of the number of seats. These working settings are not
multi-election coverage guarantees. Direct TPP removes conversion exposure.

The shared states are now calibrated only from comparable current FP/TPP
accounts. A surprising seat swing cannot itself teach a conversion error.
Compare a source booth's actual preference allocation with its expected
allocation using the district-strength relationship, historical corrections
and current broad preference corrections. When assessing a district, omit it
from both the election and region corrections, and omit its seat/booth correction.
This tests a prediction without allowing the source to validate itself.

For example, if a lower-Greens district is expected to transfer 83% to Labor
and does so, its residual error is zero. A strong-Greens destination expected
at 88% retains that different expectation. Calibration learns departures from
the relationship, not a national absolute flow. Existing observed corrections
are included in the prediction, so their full effect is not applied a second time.

An FP/TPP match identifies the combined allocation of all eliminated candidates,
not a separate Greens flow. Fit Greens and independent errors together with an
other-party error, using each group's share of the preference pool. The other
error is a nuisance variable: it prevents automatically blaming Greens for a
mixed-pool discrepancy, and is not an additional forecast adjustment. Residuals
use `25*log odds` of the combined preference flow, with a local linear conversion
from each group's flow points. The fit retains covariance between group errors.

Each district contributes one pooled observation. A working 5-flow-point
district SD prevents thousands of voters or many booths in one district from
becoming independent examples of transferability. Small pools and FP/TCP total
mismatches add uncertainty. Partial or non-classic TCP cannot train this fit;
the existing historical-flow reliability rules also apply. Only the current
effective account is used; no additional snapshot memory is required.

Half of the original shared variance remains unlearnable from these classic
matches, allowing for different non-classic campaigns. Thus the initial SDs are
still 2 Greens and 5 independent flow points; even abundant evidence cannot
reduce them below approximately 1.41 and 3.54 points. These are conservative
working allowances, not newly fitted coverage guarantees.

The TPP update conditions seats/categories on each possible calibrated error,
then propagates uncertainty across those possibilities. It never updates the
calibration from disagreement with seat priors. Scenario draws retain the same
shared errors across districts, and exported moments describe those same draws.
This is a deliberately separated calibration, rather than one unrestricted
joint Bayesian update. Its means are not fed back into the underlying booth
estimator or later snapshots. Reporting-order,
category and seat-prior uncertainties remain separate. See
[the covariance calculation](live-tpp-composition.md).

## Data, provenance and checks

`analysis/Data/historical-party-tpp-flows.csv` contains 769 candidate records.
Its columns are comparison election code, district, party, normalised full name,
candidate FP, district formal votes, that candidate's ballots reaching Labor in
TPP, whether the district's actual TCP was classic (`1` or `0`), and a
`forward_residual_weight`. Candidate
counts are integers. Greens coverage is 151 districts in 2019, 151 in 2022 and
149 in 2025; the latter excludes Bradfield's uncompleted notional TPP.

Sources are AEC candidate FP tables and classic preference-flow downloads for
[2019](https://results.aec.gov.au/24310/Website/Downloads/HouseTcpFlowByDivisionDownload-24310.csv)
and [2022](https://results.aec.gov.au/27966/Website/Downloads/HouseTcpFlowByDivisionDownload-27966.csv),
plus AEC alternative TPP tables published by Antony Green:
[2019](https://antonygreen.com.au/docs/fed/FED2019-Alt2PP-PreferenceFlows.pdf),
[2022](https://antonygreen.com.au/docs/fed/FED2022-Alt2PP-PreferenceFlows.pdf),
and the [complete 2025 report](https://antonygreen.com.au/docs/fed/FED2025_PreferenceFlowReport.pdf).
PDF candidates were joined uniquely to AEC FP by district, party, surname and
vote count (at most ten votes difference); printed table totals and official
TPP were reconciled. AEC full given names supply the runtime identity keys.
The alternative tables were published retrospectively in 2026.

The forward residual weight controls how much of an old Greens district's
exceptional preference pattern is suitable for the next election. Normally it
is `1`, retaining half the measured residual through the formula above. It can
range from `0` to `1`; `0` removes that residual from the current estimate while
retaining the district-strength relationship. Older eight-column files default
to `1`. Independent rows must retain `1`: their same-candidate flow is a separate
method, not a Greens district residual.

Melbourne's 2019 Greens record has weight `0` because of the exceptional Labor
campaign, including [Luke Creasey's withdrawal](https://www.abc.net.au/news/2019-05-03/federal-election-labor-candidate-luke-creasey-withdraws/11076194).
This excludes the unusually low historical flow's residual from the 2022 prior;
it does not claim to measure how much of that flow the withdrawal caused.
All other records currently have weight `1`. Historical candidate counts,
measured flows, national reference levels and reconstruction of previous booths
remain unchanged. A discounted source district also keeps its vote weight in a
redistributed district's denominator, so exclusion does not amplify the other
source districts. Current observed preference corrections still apply normally.

Only the configured previous election in the same electoral system is loaded,
and it must precede the current election. A 2022 replay cannot use the 2022 or
2025 rows. Elections without compatible data retain the existing mean conversion.
The history is discarded after preparation; scenarios retain only prepared
rates. No extra snapshot memory, `.pol2` fields or accepted-count history is added.

`LivePartyConversionTests` checks chronological exclusion, identity matching,
small-sample blending, old-residual reconciliation, forward-only exclusions,
weight validation, strength-adjusted matched residuals, mixed-party ambiguity,
missing/partial evidence, uncertainty floors and shipped data accounts.
`LiveTppCompositionTests` checks shared group bounds, distinct groups, partial
TCP tapering, exact counted TPP, external-factor invariance to seat priors,
observation ordering, and matching scenario/diagnostic moments.
Both run through `run-portable-tests.sh`; they do not run an election simulation.

Analysis sidecars export `party_conversion_priors` and received-row
`greens_preference_fraction`, `independent_preference_fraction`,
`greens_transfer_sd`, and `independent_transfer_sd`. The Internal 2PP Metrics
Shared SD includes the calibrated covariance of both party components; the
general correction is shown before district-budget scaling. It is a
comparison-weighted RMS observation scale, **not** national TPP SD. Older
archives retain their original values and load without requiring these fields.
The Internal 2PP Metrics view also shows `GRN err.`, `IND err.` and their SDs.
Positive error means the conversion prediction was too high for Labor; units
are preference-flow percentage points, not seat or national TPP points. Detailed
matched district accounts and calibration covariance remain in the sidecar.

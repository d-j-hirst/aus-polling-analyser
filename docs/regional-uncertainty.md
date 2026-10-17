# Regional forecast uncertainty

This guide explains how the federal forecast allows states to depart from the
national swing. A national polling error can move every state together; a
regional error means one state performs differently even after accounting for
that shared movement. The live forecast inherits these pre-election regional
uncertainties through its baseline draws.

## Mean forecast

The ordinary simulation combines a state's historical relationship with the
national swing and its regional polling signal. The polling relationship uses
an equal blend of the state's fitted slope/intercept and the pooled
slope/intercept from all states. Python calibration and the C++ simulation use
this same rule. The separate national forecast supplies the common swing.

For example, state slope 0.8 and pooled slope 0.4 give a combined slope 0.6.
State intercept 1.0 and pooled intercept −0.2 give a combined intercept 0.4.
A regional polling departure of +3 therefore predicts `0.6 × 3 + 0.4 = +2.2`.
The additional median-residual bias adjustment formerly estimated on training
elections is no longer fitted; the existing CSV bias field is exported as zero.

The polling weight follows `A × exp(−B × quarters_to_election)`, bounded to
0–1. Calibration excludes one election at a time when choosing this weight,
instead of choosing the mixture that best explains the fitting elections.

## Prediction errors and uncertainty

To measure the error on a test election, exclude its entire polling history
and regional results. Fit relationships using the remaining elections. Choose
the polling weight by excluding each of those remaining elections in turn.
Then predict the original test election. Thus its result cannot influence its
coefficients or mixture selection. Repeat for every eligible election.

These errors already include uncertainty from estimating the relationships;
no separate coefficient-error term is added on top. The historical fallback
model without regional polling also uses excluded-election errors for its SD.

Regional calibration conditions on the actual common swing to isolate regional
prediction error. The polling source contains the five mainland states, so its
reference is their population-weighted swing, not precisely the official
all-Australia swing. The simulation instead uses its sampled national swing
and the configured regions' historical turnout weights.

State vote shares use `z = 25 × log(p / (100 − p))`, where `p` is a percentage.
One z unit is approximately one percentage point near 50%. The simulation adds
regional deviations/noise in z, converts back to vote share, then adjusts all
state swings by a common amount to agree with the national forecast. Calibration
measures errors after the same transform and reconciliation on the available
five-state account. A derivative calculation translates independent input
variances into reconciled percentage-point variances. This avoids feeding a
post-reconciliation SD into the simulation as though it were an independent
input SD. It does not reproduce subsequent seat-level adjustments.

For each polling horizon, the pooled input variance is the observed average
squared prediction error divided by the average variance transferred through
reconciliation. State variance ratios borrow ten election-equivalents from the
pooled ratio of one. With five elections, a state's own ratio has weight 5/15;
the pooled value has weight 10/15. This moderates unusually low and unusually
high state-specific estimates. The ten-election strength is a regularisation
choice, not an estimate of future structural political change.

Each election contributes once per horizon. For state modifiers, polling-weight
selection and tail shape, its available quarters share a fixed total weight:
many captures of one election are not many independent outcomes. Horizons need
at least three elections. The election-day SD stays anchored to its measured
error budget; earlier-horizon SDs follow a nondecreasing bounded exponential.
Tail shape is estimated from election-balanced standardized errors and shrunk
towards normal kurtosis. There is no added allowance for future regime changes.

The emitted diagnostics show errors excluded from mean fitting and the SDs
calibrated using those errors. Their apparent interval coverage is a calibration
check, **not** an independent validation of the uncertainty parameters themselves.
There are currently only five or six independent polling elections. The source
tables end in 2022; a forecast filename such as `2028fed` does not imply that
2025 regional polling evidence has been added.

## Regeneration and verification

From `analysis/`, regenerate only the regional parameters on native Windows:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.pipeline.regional_analysis --election 2022fed
```

Omit `--election` to regenerate all supported federal targets. No Stan model,
seat-analysis bundle, or application simulation runs. The generator updates only
the selected regional records in generated provenance. For a scratch comparison:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.pipeline.regional_analysis --election 2022fed --output-directory ../cli-build/regional-check --report-directory ../cli-build/regional-check
```

The four existing CSV layouts remain compatible. Regional polling residual
columns are retained as unused legacy fields; use the mix files for operational
uncertainty and the optional JSON report for test-election errors. After updating
the application and parameters, recompute a live baseline before judging the
effect on a replay. Saved reports and already-recorded baseline covariances do
not change retroactively.

Portable checks use fictional elections to verify nested exclusion, shared mean
fixtures, reconciliation variance, bounded curves and election-based weighting.
Run from the repository root:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\tests\run_tests.py test_regional_calibration test_election_analysis_regions
```

`tests/RegionalSwingMathTests.cpp` supplies matching C++ mean and mixture checks
and is included in both portable test runners.

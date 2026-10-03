# Operational turnout calibration

This report assesses whether published early-voting and postal counts can help estimate how many formal votes will eventually be counted. Its purpose is to show which counts are informative, how accurately they predict final voting-category sizes, and where substantial errors remain.

The analysis compares historical published counts with final results, tests predictions on elections excluded from estimation, and describes conversion multipliers and their variation. A separate investigation tests federal pre-poll counts whose electorate definitions differ from those of the final results. These offline analyses do not change the live forecast.

Generated 2026-10-03T00:24:02.448330+00:00. The main analysis contains 1,891 matched observations across 32 election/measurement combinations. Federal pre-poll comparisons appear separately.

## Main findings

These findings identify where a simple conversion helps and where substantial errors remain. The following sections define the comparisons and show their evidence.

- Postal applications, other elections: estimating one multiplier from federal elections and another from state elections reduces average error in the combined postal total from 8.87% to 3.61%, compared with one multiplier estimated from both groups together. The comparison uses identical observations across 12 elections.
- Postal applications, earlier elections only: estimating one multiplier from federal elections and another from state elections reduces average error in the combined postal total from 8.61% to 5.55%, compared with one multiplier estimated from both groups together. The comparison uses identical observations across 11 elections.
- For the 4 eligible state early-vote comparisons, the mean ratio of final formal early votes to reported early ballots is 0.9503. Applied to 10,000 reported ballots, this multiplier predicts about 9,503 formal early votes. It is shared across districts; it is a coefficient, not a correlation or a district-specific estimate.
- Federal pre-poll counts are informative despite being reported under the electorate administering the centre. Across 5 elections, a multiplier applied to the national count gives 2.59% average total error, compared with 21.70% for the previous election's early-vote count adjusted for enrolment. Electorate errors include severe exceptions.
- Small election-total errors can hide large district errors because overestimates and underestimates cancel. Both errors are reported separately.
- Trial intervals intended to contain 80% of outcomes miss that target in some comparisons. The reported error allowances describe historical evidence; their probability coverage has not been independently established.

## What is being counted and estimated?

The aim is to estimate the final size of a voting category before its count is complete. Published application or early-voting counts help only if their relationship with final formal votes is understood.

A **formal vote** is a valid ballot included in the lower-house first-preference count. **Enrolment** is the number of registered electors. **District**, **electorate** and **seat** refer to the same lower-house electoral area here; a federal electorate is also called a division.

Reviewed implausible category zeros are treated as missing counts in the analytical view. Their district splits cannot provide final targets for conversion estimation or accuracy scoring, since votes may have been classified elsewhere. Whole-district totals remain available. The published controls still inform initial predictions elsewhere in the analysis; their missing targets do not become zero votes.

An **operational count** is a published application, issue, return, acceptance or early-voting count at a stated date. It is not necessarily a final formal vote count. “Cumulative” in the source data means the total through that date, rather than the number on that day.

| Measurement | Published quantity | Final quantity being estimated |
| --- | --- | --- |
| Postal applications | Applications recorded by the stated date | Final formal postal votes |
| Postal ballots issued | Postal ballot packages sent by the stated date | Final formal postal votes |
| Postal ballots returned | Postal ballots received by the stated date | Final formal postal votes |
| Postal votes accepted so far | Postal votes accepted at the stated processing stage | Final formal postal votes |
| Early ballots reported cast | Reported in-person early ballots or voters recorded as having voted | Final formal early votes in the matched source categories |
| Early and postal ballots reported together | One published combined early/postal count | Final formal early and postal votes together |

The **conversion multiplier** is final matched formal votes divided by the published operational count. Prediction uses **predicted formal votes = multiplier × operational count**. A multiplier of 0.95 converts 10,000 reported ballots into an estimate of 9,500 formal votes. It is a coefficient, not a correlation. It is not simply an acceptance probability: timing, return rates, informal ballots, eligibility and differences in scope can all contribute. A value above 1 can occur when further returns or acceptance processing follow the published count.

An **observation** is one selected operational count paired with its final target: usually one district for one election and measurement, or one state/national total when only a total is usable. This is what the machine-readable output calls a “case”. The target adds the final categories matching that observation; it is not necessarily the total vote across all categories in the district.

For each measurement, the **single multiplier** averages the available election ratios. District counts are first added within each election to calculate its ratio, and each election gets equal weight in the mean. “Shared” or “pooled” means this multiplier applies to all relevant districts rather than being fitted separately for each one. No district coefficients are fitted.

One source series is selected per election and measurement, preferring records made during the election, then counts for electors enrolled in the named district, then official sources. A parent total and its district rows are not independent evidence. The [evidence audit](../turnout-evidence-audit/report.md) documents precision and category matching.

## Observed conversion ratios

This table checks whether a measurement consistently translates into the same final quantity. Large changes in the multiplier suggest that copying one historical relationship may be unreliable.

**Observations** counts matched district or total observations. **Reported count** and **Final formal** add those observations once. **Multiplier** is their ratio. **Count through** is the latest quantity date, not a verified publication time; full date ranges are retained in the local JSON when districts differ. **Input history** distinguishes records made during the election from later revised/reconstructed series. A later reconstruction may describe quantities at election time without showing those exact figures were publicly available then.

| Measurement | Election | Observations | Reported count | Final formal | Multiplier | Count through | Input history |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Early and postal ballots reported together | 2014vic | 85 | 1101436 | 1110104 | 1.0079 | 2014-11-28T18:00:00+11:00 | Recorded during the election |
| Postal applications | 2005wa | 1 | 50419 | 37651 | 0.7468 | 2005-02-24 | Revised or reconstructed afterward |
| Postal applications | 2007fed | 1 | 833178 | 692220 | 0.8308 | 2007-11-22 | Revised or reconstructed afterward |
| Postal applications | 2010fed | 150 | 957340 | 786138 | 0.8212 | 2010-08-19 | Revised or reconstructed afterward |
| Postal applications | 2013fed | 150 | 1329215 | 1097676 | 0.8258 | 2013-09-05 | Revised or reconstructed afterward |
| Postal applications | 2016fed | 150 | 1510607 | 1197738 | 0.7929 | 2016-07-01 | Revised or reconstructed afterward |
| Postal applications | 2017wa | 1 | 160513 | 111761 | 0.6963 | 2017-03-07T08:21:00+08:00 | Recorded during the election |
| Postal applications | 2019fed | 151 | 1538624 | 1213664 | 0.7888 | 2019-05-17 | Revised or reconstructed afterward |
| Postal applications | 2021wa | 59 | 328854 | 212872 | 0.6473 | 2021-03-12 | Recorded during the election |
| Postal applications | 2022fed | 1 | 2731060 | 2141327 | 0.7841 | 2022-05-20 | Recorded during the election |
| Postal applications | 2022vic | 87 | 579906 | 390004 | 0.6725 | 2022-11-24 | Recorded during the election |
| Postal applications | 2025fed | 150 | 2561474 | 2106649 | 0.8224 | 2025-05-03 | Recorded during the election |
| Postal applications | 2026sa | 38 | 140481 | 93990 | 0.6691 | 2026-03-16 | Recorded during the election |
| Postal ballots issued | 2007fed | 1 | 812826 | 692220 | 0.8516 | 2007-11-22 | Revised or reconstructed afterward |
| Postal ballots issued | 2008wa | 1 | 81219 | 54686 | 0.6733 | 2008-09-04 | Revised or reconstructed afterward |
| Postal ballots issued | 2015qld | 1 | 306064 | 242165 | 0.7912 | 2015-01-28 | Revised or reconstructed afterward |
| Postal ballots issued | 2020qld | 93 | 905806 | 695829 | 0.7682 | 2020-10-31T10:30:00+10:00 | Recorded during the election |
| Postal ballots issued | 2023nsw | 93 | 540160 | 401094 | 0.7425 | 2023-03-24 | Recorded during the election |
| Postal ballots issued | 2024qld | 1 | 692180 | 506544 | 0.7318 | 2024-10-26 | Recorded during the election |
| Postal votes accepted so far | 2020qld | 1 | 329334 | 695829 | 2.1128 | 2020-10-31T10:30:00+10:00 | Recorded during the election |
| Postal ballots returned | 2020qld | 93 | 571095 | 695829 | 1.2184 | 2020-10-31T10:30:00+10:00 | Recorded during the election |
| Postal ballots returned | 2021wa | 59 | 169280 | 212872 | 1.2575 | 2021-03-12 | Recorded during the election |
| Postal ballots returned | 2022fed | 1 | 1644061 | 2141327 | 1.3025 | 2022-05-20 | Recorded during the election |
| Postal ballots returned | 2023nsw | 1 | 92077 | 401094 | 4.3561 | 2023-03-24 | Recorded during the election |
| Postal ballots returned | 2024qld | 1 | 338476 | 506544 | 1.4965 | 2024-10-26 | Recorded during the election |
| Postal ballots returned | 2025fed | 150 | 1685203 | 2106649 | 1.2501 | 2025-05-03 | Recorded during the election |
| Early ballots reported cast | 2005wa | 1 | 35220 | 33098 | 0.9398 | 2005-02-25T18:00:00+08:00 | Revised or reconstructed afterward |
| Early ballots reported cast | 2015nsw | 93 | 642408 | 638909 | 0.9946 | 2015-03-27 | Recorded during the election |
| Early ballots reported cast | 2021wa | 59 | 585778 | 564510 | 0.9637 | 2021-03-12 | Recorded during the election |
| Early ballots reported cast | 2022vic | 87 | 1887521 | 1790672 | 0.9487 | 2022-11-24 | Recorded during the election |
| Early ballots reported cast | 2023nsw | 93 | 1566305 | 1531181 | 0.9776 | 2023-03-24 | Recorded during the election |
| Early ballots reported cast | 2026sa | 38 | 370620 | 351741 | 0.9491 | 2026-03-20 | Recorded during the election |

Election identifiers combine year and jurisdiction: fed = federal, vic = Victoria, sa = South Australia, wa = Western Australia, qld = Queensland and nsw = New South Wales. NSW observations are displayed here but excluded from estimation and prediction tests by default.

## How prediction comparisons work

An observed ratio can fit its own election perfectly without helping predict another election. These tests assess whether the relationship transfers by excluding every observation from the election being predicted when estimating its multiplier. The excluded election is the **test election**; the elections used to estimate the multiplier are the **training elections**.

Both test methods are shown because the sample is small. Neither is assumed to give the more reliable verdict.

| Test method | Training data | Purpose and limitation |
| --- | --- | --- |
| Other elections | All eligible elections except the test election | Uses more evidence, but can include elections later than the test election |
| Earlier elections only | Eligible elections before the test election | Tests learning from the past, but early tests can have very few training elections |

The models below test whether the operational count adds information and whether separating election types improves conversion. **Comparison model** replaces the old “alternative” label; each error row names the model being compared with the single multiplier.

| Model | Calculation | Question it addresses |
| --- | --- | --- |
| Single multiplier | Mean of election conversion ratios for the measurement | Can one shared conversion describe these elections? |
| Separate federal and state multipliers | One mean for federal elections, another for all state elections together | Does this distinction improve on combining federal and state elections? |
| Last comparable multiplier | Latest earlier eligible ratio in the same jurisdiction × new operational count | Is the latest comparable election a better guide than the wider historical mean? |
| Previous category count, enrolment-adjusted | Previous formal category count × current enrolment / previous enrolment | Does the new operational count improve on carrying the previous category forward? |

“Federal and state” means **federal elections versus state elections**, not separate parameters for individual Australian states within a federal election. The separate model needs at least two training elections of the appropriate type; otherwise it uses the single multiplier across both types. The last-comparable model always uses an earlier election under either test method. The previous-count model matches district names and categories where possible; it does not adjust for redistribution. Missing matches produce no prediction.

### Reading the error columns

These two errors answer different questions: is the overall category size right, and are its votes being assigned to the right districts?

- **Total error %** (formerly “Aggregate %”) = 100 × absolute value of (sum of predicted counts − sum of actual counts) / sum of actual counts, within an election. Overestimates and underestimates can cancel.
- **Observation error %** (formerly “Case %”) = 100 × sum of absolute (predicted count − actual count) for each observation / sum of actual counts, within an election. District errors cannot cancel. For one total observation, this equals total error.
- Tables average these election-level percentages equally across elections. Within an election, observations contribute according to vote count. Lower values mean more accurate predictions.

For example, two districts with 100 actual votes each and predictions of 110 and 90 have 0% total error but 10% observation error. The denominator is the matched voting category, not all votes in the seat.

## Prediction errors on identical observations

Each row compares the single multiplier with the named model on exactly the same observations. This prevents missing historical matches from favouring a model merely because it was tested on easier districts. **Elections** and **Observations** describe the shared sample and can differ between rows. **Single** and **Comparison** identify the two models whose errors are shown. Measurements are grouped first, then comparison model and test method.

### Postal applications

| Comparison model | Test method | Elections | Observations | Single: total error % | Comparison: total error % | Single: observation error % | Comparison: observation error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Separate federal and state multipliers | Other elections | 12 | 939 | 8.8711 | 3.6109 | 9.1152 | 4.6137 |
| Separate federal and state multipliers | Earlier elections only | 11 | 938 | 8.6091 | 5.5465 | 9.2941 | 6.5367 |
| Last comparable multiplier | Other elections | 8 | 812 | 8.3536 | 3.3114 | 8.7198 | 4.6056 |
| Last comparable multiplier | Earlier elections only | 8 | 812 | 6.9371 | 3.3114 | 7.8790 | 4.6056 |
| Previous category count, enrolment-adjusted | Other elections | 7 | 870 | 9.6522 | 16.9703 | 10.0650 | 20.9360 |
| Previous category count, enrolment-adjusted | Earlier elections only | 7 | 870 | 7.5436 | 16.9703 | 8.6028 | 20.9360 |

### Postal ballots issued

| Comparison model | Test method | Elections | Observations | Single: total error % | Comparison: total error % | Single: observation error % | Comparison: observation error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Separate federal and state multipliers | Other elections | 5 | 97 | 8.0525 | 8.2478 | 8.8349 | 8.6168 |
| Separate federal and state multipliers | Earlier elections only | 4 | 96 | 8.9966 | 9.1227 | 9.9706 | 9.5858 |
| Last comparable multiplier | Other elections | 2 | 94 | 3.0868 | 3.9848 | 5.0428 | 4.8250 |
| Last comparable multiplier | Earlier elections only | 2 | 94 | 2.9352 | 3.9848 | 4.8832 | 4.8250 |
| Previous category count, enrolment-adjusted | Other elections | 3 | 95 | 3.5320 | 37.6961 | 4.8359 | 37.6961 |
| Previous category count, enrolment-adjusted | Earlier elections only | 3 | 95 | 3.1682 | 37.6961 | 4.4669 | 37.6961 |

### Postal ballots returned

| Comparison model | Test method | Elections | Observations | Single: total error % | Comparison: total error % | Single: observation error % | Comparison: observation error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Separate federal and state multipliers | Other elections | 5 | 304 | 7.0676 | 8.7963 | 9.8456 | 11.0685 |
| Separate federal and state multipliers | Earlier elections only | 4 | 211 | 7.3487 | 7.7079 | 10.1597 | 10.5188 |
| Last comparable multiplier | Other elections | 2 | 151 | 10.7449 | 11.3873 | 13.8199 | 14.8126 |
| Last comparable multiplier | Earlier elections only | 2 | 151 | 10.6666 | 11.3873 | 13.7416 | 14.8126 |
| Previous category count, enrolment-adjusted | Other elections | 4 | 301 | 8.8062 | 39.2785 | 12.3009 | 40.8441 |
| Previous category count, enrolment-adjusted | Earlier elections only | 3 | 208 | 8.1250 | 33.2720 | 11.9201 | 35.3595 |

### Early ballots reported cast

| Comparison model | Test method | Elections | Observations | Single: total error % | Comparison: total error % | Single: observation error % | Comparison: observation error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Separate federal and state multipliers | Other elections | 4 | 185 | 0.9374 | 0.9374 | 1.7035 | 1.7035 |
| Separate federal and state multipliers | Earlier elections only | 3 | 184 | 0.9926 | 0.9926 | 1.9623 | 1.9623 |
| Last comparable multiplier | Other elections | 1 | 59 | 1.8532 | 2.4845 | 1.9331 | 2.5100 |
| Last comparable multiplier | Earlier elections only | 1 | 59 | 2.4845 | 2.4845 | 2.5100 | 2.5100 |
| Previous category count, enrolment-adjusted | Other elections | 2 | 134 | 1.0333 | 41.4193 | 1.7644 | 41.5184 |
| Previous category count, enrolment-adjusted | Earlier elections only | 2 | 134 | 1.3956 | 41.4193 | 2.0497 | 41.5184 |

## Counts recorded during the election versus revised series

This check shows whether accuracy depends heavily on counts revised or reconstructed afterward. Such data can inform conversion analysis without establishing what a live system could have predicted from the original figures.

**Test input history** refers to the test election's count. Training can use eligible elections of either history because their final records are available to later elections. The error columns use the definitions above; **Observations** shows how much evidence supports each result. Even records made during an election have unverified publication times, so these are not complete historical live replays.

| Measurement | Model | Test input history | Test method | Elections | Observations | Total error % | Observation error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Postal applications | Single multiplier | Recorded during the election | Other elections | 6 | 336 | 11.4875 | 11.7986 |
| Postal applications | Single multiplier | Recorded during the election | Earlier elections only | 6 | 336 | 12.5649 | 12.9369 |
| Postal applications | Single multiplier | Revised or reconstructed afterward | Other elections | 6 | 603 | 6.2546 | 6.4317 |
| Postal applications | Single multiplier | Revised or reconstructed afterward | Earlier elections only | 5 | 602 | 3.8621 | 4.9226 |
| Postal applications | Separate federal and state multipliers | Recorded during the election | Other elections | 6 | 336 | 3.4588 | 4.7476 |
| Postal applications | Separate federal and state multipliers | Recorded during the election | Earlier elections only | 6 | 336 | 6.5168 | 7.6774 |
| Postal applications | Separate federal and state multipliers | Revised or reconstructed afterward | Other elections | 6 | 603 | 3.7629 | 4.4798 |
| Postal applications | Separate federal and state multipliers | Revised or reconstructed afterward | Earlier elections only | 5 | 602 | 4.3822 | 5.1679 |
| Postal ballots issued | Single multiplier | Recorded during the election | Other elections | 2 | 94 | 3.0868 | 5.0428 |
| Postal ballots issued | Single multiplier | Recorded during the election | Earlier elections only | 2 | 94 | 2.9352 | 4.8832 |
| Postal ballots issued | Single multiplier | Revised or reconstructed afterward | Other elections | 3 | 3 | 11.3629 | 11.3629 |
| Postal ballots issued | Single multiplier | Revised or reconstructed afterward | Earlier elections only | 2 | 2 | 15.0580 | 15.0580 |
| Postal ballots issued | Separate federal and state multipliers | Recorded during the election | Other elections | 2 | 94 | 3.1973 | 4.1197 |
| Postal ballots issued | Separate federal and state multipliers | Recorded during the election | Earlier elections only | 2 | 94 | 3.1873 | 4.1135 |
| Postal ballots issued | Separate federal and state multipliers | Revised or reconstructed afterward | Other elections | 3 | 3 | 11.6148 | 11.6148 |
| Postal ballots issued | Separate federal and state multipliers | Revised or reconstructed afterward | Earlier elections only | 2 | 2 | 15.0580 | 15.0580 |
| Postal ballots returned | Single multiplier | Recorded during the election | Other elections | 5 | 304 | 7.0676 | 9.8456 |
| Postal ballots returned | Single multiplier | Recorded during the election | Earlier elections only | 4 | 211 | 7.3487 | 10.1597 |
| Postal ballots returned | Separate federal and state multipliers | Recorded during the election | Other elections | 5 | 304 | 8.7963 | 11.0685 |
| Postal ballots returned | Separate federal and state multipliers | Recorded during the election | Earlier elections only | 4 | 211 | 7.7079 | 10.5188 |
| Early ballots reported cast | Single multiplier | Recorded during the election | Other elections | 3 | 184 | 0.7510 | 1.7725 |
| Early ballots reported cast | Single multiplier | Recorded during the election | Earlier elections only | 3 | 184 | 0.9926 | 1.9623 |
| Early ballots reported cast | Single multiplier | Revised or reconstructed afterward | Other elections | 1 | 1 | 1.4966 | 1.4966 |
| Early ballots reported cast | Separate federal and state multipliers | Recorded during the election | Other elections | 3 | 184 | 0.7510 | 1.7725 |
| Early ballots reported cast | Separate federal and state multipliers | Recorded during the election | Earlier elections only | 3 | 184 | 0.9926 | 1.9623 |
| Early ballots reported cast | Separate federal and state multipliers | Revised or reconstructed afterward | Other elections | 1 | 1 | 1.4966 | 1.4966 |

## Training sample sizes and trial uncertainty intervals

This check prevents a good average error from hiding predictions based on very little evidence. It also asks whether simple uncertainty intervals contain the final counts as often as intended.

**Training elections: min / median / max** gives the smallest, middle and largest number of training elections used across the tests. **No training** counts tests with no multiplier and therefore no fitted prediction. **One training election** counts tests with a point prediction but insufficient evidence to estimate variation between elections.

**Error / all seat votes %** divides summed absolute observation errors by all formal votes in the matched districts, then averages elections equally. Unlike category error, this describes the size of error relative to the whole seat count. **Interval coverage %** is the share of actual counts inside the trial intervals, averaged equally across elections with intervals. **Elections with intervals** is its supporting sample; tests without intervals do not contribute.

The trial intervals aim for 80% coverage: about 80 out of 100 predictions should contain the final count if the assumptions are adequate. They use a bell-shaped (normal) approximation, variation between election ratios, uncertainty in their estimated mean, and extra district variation when district data exist. With only one training election, between-election variation cannot be estimated. Missing district variation also prevents a district interval. Coverage is an empirical check on a small sample, not a guarantee.

| Measurement | Model | Test method | Training elections: min / median / max | No training | One training election | Error / all seat votes % | Interval coverage % | Elections with intervals |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Early and postal ballots reported together | Single multiplier | Other elections | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Early and postal ballots reported together | Single multiplier | Earlier elections only | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Early and postal ballots reported together | Separate federal and state multipliers | Other elections | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Early and postal ballots reported together | Separate federal and state multipliers | Earlier elections only | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Postal applications | Single multiplier | Other elections | 11 / 11 / 11 | 0 | 0 | 0.9451 | 79.7735 | 12 |
| Postal applications | Single multiplier | Earlier elections only | 0 / 5.5 / 11 | 1 | 1 | 0.9942 | 60.7613 | 9 |
| Postal applications | Separate federal and state multipliers | Other elections | 4 / 6 / 6 | 0 | 0 | 0.4445 | 72.5259 | 12 |
| Postal applications | Separate federal and state multipliers | Earlier elections only | 0 / 3 / 6 | 1 | 1 | 0.6585 | 55.5495 | 8 |
| Postal ballots issued | Single multiplier | Other elections | 4 / 4 / 4 | 0 | 0 | 0.7979 | 50.0000 | 4 |
| Postal ballots issued | Single multiplier | Earlier elections only | 0 / 2 / 4 | 1 | 1 | 0.9019 | 100 | 2 |
| Postal ballots issued | Separate federal and state multipliers | Other elections | 3 / 3 / 4 | 0 | 0 | 0.8080 | 50.0000 | 4 |
| Postal ballots issued | Separate federal and state multipliers | Earlier elections only | 0 / 2 / 3 | 1 | 1 | 0.8814 | 100 | 2 |
| Postal votes accepted so far | Single multiplier | Other elections | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Postal votes accepted so far | Single multiplier | Earlier elections only | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Postal votes accepted so far | Separate federal and state multipliers | Other elections | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Postal votes accepted so far | Separate federal and state multipliers | Earlier elections only | 0 / 0 / 0 | 1 | 0 | — | — | 0 |
| Postal ballots returned | Single multiplier | Other elections | 4 / 4 / 4 | 0 | 0 | 1.7226 | 73.0363 | 5 |
| Postal ballots returned | Single multiplier | Earlier elections only | 0 / 2 / 4 | 1 | 1 | 1.5317 | 27.7778 | 3 |
| Postal ballots returned | Separate federal and state multipliers | Other elections | 2 / 2 / 4 | 0 | 0 | 1.9684 | 74.2683 | 5 |
| Postal ballots returned | Separate federal and state multipliers | Earlier elections only | 0 / 2 / 4 | 1 | 1 | 1.5903 | 27.7778 | 3 |
| Early ballots reported cast | Single multiplier | Other elections | 3 / 3 / 3 | 0 | 0 | 0.5788 | 68.0487 | 4 |
| Early ballots reported cast | Single multiplier | Earlier elections only | 0 / 1.5 / 3 | 1 | 1 | 0.8320 | 92.0296 | 2 |
| Early ballots reported cast | Separate federal and state multipliers | Other elections | 3 / 3 / 3 | 0 | 0 | 0.5788 | 68.0487 | 4 |
| Early ballots reported cast | Separate federal and state multipliers | Earlier elections only | 0 / 1.5 / 3 | 1 | 1 | 0.8320 | 92.0296 | 2 |

## Does publication rounding matter?

This check asks whether counts reconstructed from rounded percentages could materially change the conclusions. If rounding is much smaller than prediction error, more elaborate rounding treatment would add little.

Only exact counts and rates rounded to 0.1 percentage point or finer are admitted. For rounded inputs, the check moves training counts and test counts to their precision limits and conservatively adds the possible prediction changes. It is a sensitivity check, not a separate fitted model. Forecasts, one-sided bounds and vague approximations remain excluded.

**Rounding / prediction error %** = 100 × summed possible rounding effects / summed absolute prediction errors, among affected observations using the single multiplier. **Median** and **Maximum** describe the possible change in one prediction in votes. An observation can be a large state total, so the maximum need not describe a typical district.

| Test method | Rounding / prediction error % | Median possible change (votes) | Maximum possible change (votes) |
| --- | --- | --- | --- |
| Other elections | 1.7884 | 6.5258 | 3449.4242 |
| Earlier elections only | 2.3950 | 15.9758 | 5636.3691 |

## Largest district errors

This check identifies failures concealed by averages or election-total errors. The five largest errors relative to all district formal votes are shown for each test method. Postal applications use separate federal/state multipliers; other measurements use the single multiplier.

**Error votes** is prediction minus actual category count: positive means too many votes predicted, negative means too few. **Error / all district votes %** divides that signed error by the district's formal votes across all categories. These are observed errors, not fitted district adjustments.

| Measurement | Test method | Election | District | Error votes | Error / all district votes % |
| --- | --- | --- | --- | --- | --- |
| Postal ballots returned | Other elections | 2025fed | Calwell | 9918.2497 | 11.0410 |
| Postal ballots returned | Other elections | 2020qld | Gregory | -2143.9988 | -10.3710 |
| Postal ballots returned | Other elections | 2020qld | Stretton | 2180.8583 | 7.6521 |
| Early ballots reported cast | Other elections | 2021wa | Kalgoorlie | -1127.6730 | -7.6401 |
| Postal applications | Other elections | 2025fed | Calwell | 6673.9472 | 7.4294 |
| Postal ballots returned | Earlier elections only | 2025fed | Calwell | 9918.2497 | 11.0410 |
| Early ballots reported cast | Earlier elections only | 2021wa | Kalgoorlie | -1164.1376 | -7.8871 |
| Postal applications | Earlier elections only | 2025fed | Calwell | 6673.9472 | 7.4294 |
| Postal ballots returned | Earlier elections only | 2025fed | Aston | 7515.8938 | 6.8420 |
| Postal ballots returned | Earlier elections only | 2025fed | Dunkley | 6771.4569 | 6.2024 |

## Shared conversion multipliers and error allowances

This table describes coefficients for converting a reported count into a final category estimate and the evidence about their variation. All eligible elections contribute here. These full-sample coefficients are not used to predict an excluded election; those predictions use only their own training elections.

**Election group** identifies the contributors to the mean multiplier. State elections are combined rather than fitting each state separately. **Elections** counts contributors. A one-election multiplier is descriptive only: it cannot establish transferability or variation between elections.

- **Between-election spread** is the sample standard deviation of the election ratios, measuring how far they vary around their mean.
- **Extra district spread** measures district-ratio deviations from their own election ratio. Squared deviations are weighted by operational counts and averaged within each election, then averaged equally across elections and square-rooted. This shared root mean square (RMS) measure gives larger deviations more weight. **Elections with district data** counts the contributing elections.
- **Election error allowance** takes the largest of the between-election spread and the RMS election-total prediction error under each test method, expressed per operational count. This avoids choosing the most favourable check. It is derived from these errors, not independently tested as an uncertainty interval.

Spreads and allowances use multiplier units: 0.01 corresponds to 100 votes per 10,000 operational counts. They are not percentages of final votes or interval widths by themselves. Sparse federal/state training can use the combined multiplier fallback described above; the local JSON identifies those tests.

| Measurement | Election group | Elections | Multiplier | Between-election spread | Election error allowance | Extra district spread | Elections with district data |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Early and postal ballots reported together | Federal and state together | 1 | 1.0079 | — | — | 0.0464 | 1 |
| Early and postal ballots reported together | State elections together | 1 | 1.0079 | — | — | 0.0464 | 1 |
| Postal applications | Federal and state together | 12 | 0.7582 | 0.0689 | 0.0756 | 0.0357 | 8 |
| Postal applications | Federal elections | 7 | 0.8094 | 0.0199 | 0.0397 | 0.0399 | 5 |
| Postal applications | State elections together | 5 | 0.6864 | 0.0380 | 0.0672 | 0.0275 | 3 |
| Postal ballots issued | Federal and state together | 5 | 0.7632 | 0.0665 | 0.0924 | 0.0443 | 1 |
| Postal ballots issued | Federal elections | 1 | 0.8516 | — | — | — | 0 |
| Postal ballots issued | State elections together | 4 | 0.7411 | 0.0514 | 0.0923 | 0.0443 | 1 |
| Postal votes accepted so far | Federal and state together | 1 | 2.1128 | — | — | — | 0 |
| Postal votes accepted so far | State elections together | 1 | 2.1128 | — | — | — | 0 |
| Postal ballots returned | Federal and state together | 5 | 1.3050 | 0.1112 | 0.1290 | 0.1502 | 3 |
| Postal ballots returned | Federal elections | 2 | 1.2763 | 0.0370 | 0.0666 | 0.1665 | 1 |
| Postal ballots returned | State elections together | 3 | 1.3242 | 0.1506 | 0.1849 | 0.1413 | 2 |
| Early ballots reported cast | Federal and state together | 4 | 0.9503 | 0.0099 | 0.0140 | 0.0222 | 3 |
| Early ballots reported cast | State elections together | 4 | 0.9503 | 0.0099 | 0.0140 | 0.0222 | 3 |

## Federal pre-poll counts as an indicator

This investigation asks whether federal pre-poll counts help estimate final early votes despite being assigned to a different electorate in the operational data. Rejecting them solely because the definitions differ could discard useful information.

The reported count covers ballots issued by early-voting centres administered under the named electorate, including ballots for voters enrolled elsewhere. The final target covers formal early votes belonging to residents of the named electorate, including those who voted elsewhere. We test the administering-electorate count as an indicator of the home-electorate target and the national sum, where movements between electorates cancel.

The administering-electorate comparison is a diagnostic only. It is not used as a district input in the category allocation or initial count-distribution prototype, and its district errors do not set those models’ uncertainty. Those prototypes use the national early count, with district allocations and their uncertainty estimated from previous results for resident electors. This keeps useful national evidence while respecting the different district populations counted by issuing-centre and final-result records.

A separate data limitation was that final federal summaries combine ordinary early and election-day votes. The supplement now recovers the ordinary early component from AEC polling-place first-preference files and the official polling-place classification. It sums formal ordinary votes at PrePollVotingCentre places (AEC type 5), excludes informal rows, then adds final declaration pre-poll formal votes once. Declaration pre-polls are early ballots requiring a separate check of the voter's entitlement before admission to the count. This target covers those two published early-vote components; it does not infer when votes in separate mobile or hospital categories were cast. Summing all polling-place ordinary formal votes reproduces the existing ordinary category for every imported district. This is a published-count reconstruction, not an estimated split. The AEC supplies [final polling-place CSVs and classifications](https://results.aec.gov.au/31496/Website/HouseDownloadsMenu-31496-Csv.htm) and explains the [ordinary and declaration categories](https://results.aec.gov.au/31496/Website/HouseVotesCountedByDivision-31496-NAT.htm).

### Counts and electorate association

This checks the national conversion and whether electorates with more reported early voting also tend to have more final early votes. **Rate correlation** is the Pearson linear correlation between issued ballots / enrolment and final formal early votes / enrolment across electorates. Dividing by enrolment avoids treating electorate size alone as evidence of a relationship. Values range from −1 to +1; near +1 means a strong positive association. This is distinct from the multiplier and does not establish accurate electorate predictions.

**Used in tests** requires a retained count dated election eve or polling day. The 2010 series stops on Thursday and is shown for context, but excluded from estimating/testing a full-voting-period conversion. The remaining five elections supply the prediction tests. Dates do not verify historical publication availability: these selected district series were revised or reconstructed afterward.

| Election | Electorates | Count through | Reported issued | Final formal early | Multiplier | Rate correlation | Used in tests |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2010fed | 150 | 2010-08-19 | 1044597 | 1422090 | 1.3614 | 0.7147 | No |
| 2013fed | 150 | 2013-09-06 | 2317323 | 2366089 | 1.0210 | 0.9040 | Yes |
| 2016fed | 150 | 2016-07-01 | 3116248 | 3087900 | 0.9909 | 0.9456 | Yes |
| 2019fed | 151 | 2019-05-17 | 4778856 | 4660725 | 0.9753 | 0.9610 | Yes |
| 2022fed | 151 | 2022-05-21 | 5633857 | 5431136 | 0.9640 | 0.9680 | Yes |
| 2025fed | 150 | 2025-05-03 | 6842159 | 6505764 | 0.9508 | 0.9772 | Yes |

### Prediction errors

These tests assess predictive usefulness rather than relying on correlation alone. They use the same excluded-election training, error definitions and identical-observation comparisons as the main analysis. The single multiplier here is learned only from eligible federal pre-poll comparisons. National and electorate views use the same election ratios; they are not additional independent training elections.

The federal/state model would equal the single multiplier in this federal-only investigation and is omitted. Previous district counts match names without adjusting boundaries; missing names reduce their shared sample. With earlier-only training, the first eligible election has no previous eligible conversion and no fitted prediction. The next has one training election.

#### Federal early voting: national total

| Comparison model | Test method | Elections | Observations | Single: total error % | Comparison: total error % | Single: observation error % | Comparison: observation error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Last comparable multiplier | Other elections | 4 | 4 | 1.9991 | 1.7996 | 1.9991 | 1.7996 |
| Last comparable multiplier | Earlier elections only | 4 | 4 | 3.3422 | 1.7996 | 3.3422 | 1.7996 |
| Previous category count, enrolment-adjusted | Other elections | 5 | 5 | 2.5940 | 21.7003 | 2.5940 | 21.7003 |
| Previous category count, enrolment-adjusted | Earlier elections only | 4 | 4 | 3.3422 | 17.8300 | 3.3422 | 17.8300 |

#### Federal early voting: administering electorate

| Comparison model | Test method | Elections | Observations | Single: total error % | Comparison: total error % | Single: observation error % | Comparison: observation error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Last comparable multiplier | Other elections | 4 | 602 | 1.9991 | 1.7996 | 5.4176 | 5.1921 |
| Last comparable multiplier | Earlier elections only | 4 | 602 | 3.3422 | 1.7996 | 5.9064 | 5.1921 |
| Previous category count, enrolment-adjusted | Other elections | 5 | 739 | 2.5656 | 21.5905 | 6.9077 | 22.3018 |
| Previous category count, enrolment-adjusted | Earlier elections only | 4 | 589 | 3.3558 | 17.7102 | 5.8944 | 18.5971 |

### What the federal results establish

These comparisons distinguish the benefit of using new counts from the remaining errors in distributing those counts between electorates.

- Other elections: for national total, issued-ballot counts with a shared multiplier give 2.59% error, compared with 21.70% for the previous category count adjusted for enrolment, on 5 elections and 5 shared observations.
- Other elections: for individual electorate counts, issued-ballot counts with a shared multiplier give 6.91% error, compared with 22.30% for the previous category count adjusted for enrolment, on 5 elections and 739 shared observations.
- Earlier elections only: for national total, issued-ballot counts with a shared multiplier give 3.34% error, compared with 17.83% for the previous category count adjusted for enrolment, on 4 elections and 4 shared observations.
- Earlier elections only: for individual electorate counts, issued-ballot counts with a shared multiplier give 5.89% error, compared with 18.60% for the previous category count adjusted for enrolment, on 4 elections and 589 shared observations.

The last comparable multiplier is also competitive with, and on these samples more accurate than, the mean of all training multipliers. National ratios decline from 2013 to 2025; these results do not justify assuming one timeless coefficient removes all election-to-election change.

Strong rate correlations coexist with large electorate exceptions. These are the five largest errors relative to all electorate formal votes using the single multiplier trained on other elections. Columns have the same meaning as the main largest-error table.

| Election | Electorate | Reported issued | Actual formal early | Predicted formal early | Error votes | Error / all district votes % |
| --- | --- | --- | --- | --- | --- | --- |
| 2013fed | Melbourne | 34524 | 16621 | 33497.2215 | 16876.2215 | 19.9598 |
| 2019fed | Swan | 31852 | 17266 | 31269.1053 | 14003.1053 | 16.6432 |
| 2016fed | Lingiari | 19806 | 12438 | 19366.1926 | 6928.1926 | 16.1590 |
| 2022fed | Lingiari | 29093 | 22042 | 28642.5147 | 6600.5147 | 14.4078 |
| 2016fed | Melbourne | 37539 | 23574 | 36705.4177 | 13131.4177 | 13.8841 |

These counts are informative indicators, especially nationally. They do not provide an exact count for residents of the administering electorate. The outliers are consistent with geographic mismatch, but this analysis does not attribute each error to a particular cause or estimate transfers between centres. Federal tests remain separate from the main like-for-like category conversions.

## Exclusions and coverage

These exclusions prevent counts for different voters, categories or processing stages from being treated as directly comparable. Exclusion from the main conversion analysis does not show that evidence has no predictive value.

WA 2025 early voting has an unresolved early/absent split. Older SA declaration totals cannot supply separate early/postal targets. VIC 2022 uses the matched 87-district sample, excluding Narracan. NSW is outside training by default because its optional preferential ballot rules differ. Federal pre-poll counts with different electorate definitions are assessed separately above without changing the strict matching in the main audit.

**Controls** counts latest source observations rejected from the main selected comparison series. These include alternative sources for the same underlying count; they are not independent missing elections.

| Reason | Controls |
| --- | --- |
| Administering district differs from voters' home district; requires aggregate comparison | 995 |
| Duplicate source series or parent total excluded in favour of the selected series | 8 |
| Target exists only inside a combined final category | 105 |
| Published total and available final districts cover different areas | 3 |
| Measurement stage has no supported final-category comparison | 5 |
| Required final category is missing or cannot be matched | 27 |
| Category or geographic definition needs clarification | 3 |
| Final early votes remain inside combined ordinary votes in the main dataset | 1 |

## Reproduction and source updates

These commands reproduce the comparisons and recover the separate federal early-vote targets. Run from the analysis directory on native Windows:

~~~powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_federal_prepoll
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_operational_calibration --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_operational_calibration
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_operational_calibration --check
~~~

The federal adapter reads official polling-place classifications and first-preference counts for all states and territories in 2010–2025. Data/Turnout/FederalPrepoll/final.json retains reconstructed category counts, source URLs/hashes and the normalized dataset version they accompany. Raw CSVs are cached under downloads/turnout/federal-prepoll/. Add --refresh to download current AEC files after a correction; changed files receive separate content-hash directories so earlier revisions remain available. Without that option, retained files are reused.

After a source revision, refresh its normalized ingestion adapter, regenerate the federal supplement if its federal inputs changed, and rerun calibration. The generated local calibration.json retains observations, training election identities and coefficients for every prediction test, predictions, full-sample multipliers, source details and input/code fingerprints. A fingerprint is a content hash used to detect local changes. --check compares local evidence, code and options with the generated version; it does not poll remote sources. Add --include-nsw to include NSW in estimation and testing. An em dash means the quantity cannot be estimated from the available evidence.

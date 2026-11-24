# Historical preference flows for returning independents

This trial asks whether the first booths to report preferences give a better guide to the rest of a district when their previous preference flows are known. It compares archived forecasts with two alternatives that can be calculated from the information available at each capture. The trial operates offline.

## What changes at a booth

An independent's preference flow here is their share of the ballots that started with neither finalist. The earlier election must have had the same named independent against the same opponent party. Both elections must identify the ballot candidates in the relevant booth. This matters in redistributed seats such as Bradfield, where some old booth counts belonged to a different independent.

The control uses the observed seat-wide transfer rate as one component of the estimate at a booth still awaiting TCP. The prototype starts each eligible booth at its historical rate, then finds the common shift in preference odds that reproduces the transfers counted so far at matched booths. Current first votes for neither finalist determine how much each reporting booth contributes. A rise from 50% to 60% represents the same shift in odds as a rise from 75% to about 82%.

The trial gives small historical booths 50 extra preference ballots at the historical district average. It blends the historical estimate towards the control when few current transfers have been counted or few have comparable history. Its weight is the number of current transfers with history divided by all usable current transfers plus 100. The **full** version uses that weight. The **half** version moves halfway from the control to the full result. The 50- and 100-ballot allowances are modelling assumptions.

The change applies only to booths with no reported TCP for the active pairing. Booth turnout, FP projections, counted TCPs, the separate historical TCP-swing estimate and its weight stay fixed. This first test also leaves outstanding batches at a location with a partial TCP on their existing forecast. A later capture is only a scoring source for an earlier one.

The live forecast implements the half-strength version for compulsory preferential elections. It verifies the independent’s full name and the opposing party in both election rosters and uses measured transfers only where the FP and TCP totals agree. Once a booth reports TCP it leaves this correction. The local booth diagnostic `returning_independent_flow_percent` records the adjusted share of other candidates’ preferences going to the independent wherever the correction is available.

## Fowler on federal election night in 2025

At 19:56 Dai Le received 620 of 1,072 transfers in the seven reporting locations, or 57.84%. The fitted historical baseline for those locations was 60.86%, weighted to their current preference ballots. The new evidence still pointed to a fall in her flow. The geography mattered because many of the outstanding booths had historically favoured Le more strongly on preferences.

Each entry below lists the archived forecast, half strength and full strength, in that order. Times are Sydney time. The matched-booth count requires a usable historical and current FP/TCP comparison. By 21:59 there were 34 TCP locations and 29 qualified for the exact-match fit.

| Time | Matched booths | Median Le TCP (%) | Le win chance (%) |
|---|---:|---:|---:|
| 19:56 | 7 | 48.84 / 49.02 / 49.19 | 24.5 / 28.2 / 32.1 |
| 20:29 | 11 | 49.92 / 49.84 / 49.77 | 47.6 / 45.9 / 44.1 |
| 20:59 | 17 | 50.67 / 50.69 / 50.70 | 67.0 / 67.2 / 67.4 |
| 21:59 | 29 | 51.38 / 51.34 / 51.30 | 79.2 / 78.5 / 77.9 |

At 19:56 the full adjustment added about 617 projected TCP votes for Le across 36 unreported locations with comparable history. In the resulting booth projections, Le gained 1.11 pp at Cabramatta early voting, 0.89 pp at Liverpool early voting and 1.39 pp on postals. She lost 2.32 pp on absent votes. Those locations eventually gave her 60.05%, 47.16%, 53.73% and 46.17% TCP respectively. The change helped the first three and worsened the last. Across all 36 the mean absolute preference-flow error fell from 9.57 to 6.48 pp. At 20:29 the reporting locations included booths with historically stronger flows, so the adjustment slightly favoured Labor. Le ultimately won 52.68% TCP.

## How the alternatives scored

The first two measures use the locations that still had no TCP, had comparable history and had a later return for the same pairing. Preference-flow error is weighted by later ballots from other candidates. Booth TCP error is weighted by later TCP ballots, including where the later FP and TCP totals differed. District error compares the conditional TCP median with the official final pair and result. We average over distinct local transfer counts within each seat, then give each seat in each election equal weight. Repeated captures with unchanged local transfer evidence count once. The three columns below are average absolute errors in percentage points.

| Measure | Seat–elections | Control | Half | Full | Improved at half / full |
|---|---:|---:|---:|---:|---:|
| Preference flow at outstanding locations | 25 | 4.43 | 3.79 | 3.81 | 23 / 19 |
| TCP share at outstanding locations | 25 | 3.19 | 3.05 | 2.99 | 21 / 18 |
| District TCP median for the eventual pair | 24 | 1.37 | 1.35 | 1.34 | 17 / 16 |

Excluding Fowler, the average preference-flow error was 4.39 / 3.77 / 3.80 pp. The exact-match experiment produced an adjustment at 119 distinct local counts across 27 seat–elections. Narungga and Stuart eventually reported different pairs, and the last archive supplied no booth outcomes for the earlier pairs tested there. Shepparton supplied booth outcomes for Sheed–Liberal, although the eventual opponent was National. The Western Australian 2025 replay supplied no eligible comparison.

The district medians and win chances are **reconstructed sensitivities**. The calculation applies the production live-confidence blend to the change in the booth estimate and shifts the saved conditional TCP distribution on the log-odds scale. Win chances use the archived win frequency as the starting point and interpolate the crossing of 50% between saved percentiles. The trial holds pairing probabilities, simulation spread and turnout fixed. The Fowler pair appeared in every draw. Some comparison archives come from earlier model versions. Per-iteration turnout, variation in the new estimate and vote-account reconciliation are not reproduced here.

A second fit admitted nearly matching FP and TCP totals, gradually discounting them within the existing 2% compulsory-vote tolerance. Scoring actual flows still required equal totals. This changed the locations available for fitting and scoring. Within that sample the flow errors were 4.44 / 3.82 / 3.84 pp, booth TCP errors 3.06 / 2.93 / 2.90 pp, and district TCP errors 1.35 / 1.33 / 1.33 pp. In Mildura this materially increased coverage. At 21:00 the full version moved Cupper's estimated TCP from 48.70% to 48.10%, further from the final 48.82%.

The following breakdown gives the control, half and full mean absolute errors for each eligible seat in the exact-match trial. A lower figure indicates a closer estimate. In 2016 Indi the same two finalists faced each other again, but the new Nationals candidate won 17.22% of all FPs, about 46% of the ballots available for transfer. A single district-wide shift cannot represent changes in that party's vote mix that differ from booth to booth. This is a plausible contributor to the deterioration there. In 2025 Cowper and Curtin the booth preference estimates improved while the district median moved further from the result.

| Election | Seat | Booth flow error (pp) | Booth TCP error (pp) | District TCP error (pp) |
|---|---|---:|---:|---:|
| 2013fed | Denison | 4.70 / 3.22 / 2.37 | 3.26 / 2.53 / 1.87 | 0.29 / 0.26 / 0.22 |
| 2016fed | Denison | 3.68 / 3.36 / 4.16 | 2.32 / 2.27 / 2.35 | 0.60 / 0.64 / 0.69 |
| 2016fed | Indi | 6.49 / 8.12 / 9.77 | 2.65 / 3.26 / 3.88 | 0.94 / 1.03 / 1.11 |
| 2019fed | Clark | 3.27 / 2.32 / 1.86 | 2.13 / 2.03 / 1.95 | 0.52 / 0.52 / 0.52 |
| 2019fed | Cowper | 3.58 / 3.56 / 3.98 | 2.93 / 2.86 / 2.80 | 1.18 / 1.12 / 1.15 |
| 2022fed | Clark | 4.56 / 2.92 / 2.55 | 1.89 / 1.38 / 1.20 | 0.28 / 0.25 / 0.28 |
| 2022fed | Indi | 5.56 / 4.04 / 3.53 | 2.29 / 1.98 / 1.82 | 0.78 / 0.66 / 0.57 |
| 2022fed | Warringah | 2.86 / 2.57 / 3.50 | 2.22 / 2.03 / 1.88 | 0.33 / 0.27 / 0.27 |
| 2022vic | Mildura | 11.18 / 11.08 / 10.98 | 10.78 / 10.78 / 10.77 | 7.71 / 7.71 / 7.71 |
| 2022vic | Shepparton | 4.21 / 4.61 / 5.36 | 3.86 / 3.63 / 3.45 | Unavailable |
| 2024qld | Noosa | 3.94 / 2.49 / 3.18 | 2.75 / 2.20 / 1.72 | 2.65 / 2.64 / 2.69 |
| 2025fed | Bradfield | 3.11 / 2.61 / 2.30 | 1.20 / 1.27 / 1.39 | 0.68 / 0.59 / 0.60 |
| 2025fed | Clark | 4.17 / 3.39 / 3.05 | 1.53 / 1.23 / 1.14 | 0.48 / 0.39 / 0.31 |
| 2025fed | Cowper | 4.37 / 3.29 / 2.72 | 2.88 / 3.07 / 3.41 | 0.99 / 1.15 / 1.31 |
| 2025fed | Curtin | 3.78 / 2.20 / 1.85 | 2.71 / 2.58 / 2.55 | 0.31 / 0.49 / 0.68 |
| 2025fed | Fowler | 5.43 / 4.29 / 4.13 | 4.35 / 4.28 / 4.27 | 2.22 / 2.19 / 2.17 |
| 2025fed | Goldstein | 3.77 / 2.62 / 2.04 | 2.09 / 1.94 / 1.85 | 0.79 / 0.72 / 0.65 |
| 2025fed | Groom | 2.83 / 1.98 / 2.06 | 1.19 / 1.20 / 1.42 | 0.39 / 0.48 / 0.56 |
| 2025fed | Indi | 4.50 / 4.31 / 4.98 | 5.48 / 5.40 / 5.52 | 0.38 / 0.26 / 0.15 |
| 2025fed | Kooyong | 4.07 / 3.20 / 3.52 | 5.57 / 5.32 / 5.09 | 4.31 / 4.21 / 4.11 |
| 2025fed | Mackellar | 2.39 / 2.29 / 2.39 | 1.44 / 1.43 / 1.47 | 0.22 / 0.24 / 0.26 |
| 2025fed | Wannon | 6.36 / 5.86 / 5.39 | 4.14 / 4.03 / 3.92 | 1.71 / 1.67 / 1.63 |
| 2025fed | Warringah | 2.63 / 2.30 / 2.28 | 2.58 / 2.55 / 2.54 | 0.56 / 0.50 / 0.45 |
| 2025fed | Wentworth | 3.76 / 2.84 / 2.12 | 2.58 / 2.37 / 2.18 | 0.80 / 0.74 / 0.67 |
| 2026sa | Finniss | 5.61 / 5.27 / 5.17 | 4.81 / 4.61 / 4.42 | 3.81 / 3.57 / 3.34 |
| 2026sa | Narungga | Unavailable | Unavailable | Unavailable |
| 2026sa | Stuart | Unavailable | Unavailable | Unavailable |

## Reproduction

Run the following from the repository root with the archived replays available. The script writes one small JSON record per eligible seat and capture to the chosen output file, and prints the aggregate errors. Add `--allow-near` for the second fit.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/returning_independent_flows.py 2013fed 2016fed 2019fed 2022fed 2025fed 2022vic 2024qld 2025wa 2026sa --output independent-flow-trial.json
```

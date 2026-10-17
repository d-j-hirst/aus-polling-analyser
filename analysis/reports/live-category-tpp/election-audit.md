# Audit of historical category comparisons

This audit checks why completed category counts sometimes appear inconsistent
with election-night experience. It separates category behaviour, category
sizes, historical forecast centres and the order in which votes reported.
The earlier relationship report remains an exploratory first pass; its fitted
coefficients should not be used for calibration before these issues are resolved.
No production forecast code or election inputs are changed.

## What needed correction

The earlier category errors carry previous category leans forward but do not
re-centre them for current category fractions. They therefore do not combine
to the election-wide forecast error. This audit adds a common log-odds shift
to the previous category shares until their current-vote-weighted mean equals
the expected total. Category errors then reconcile exactly in percentage points.
This uses final category fractions as a diagnostic, not as election-night
information. A live model would have to allow uncertainty in those fractions.

NSW 2023 also needs a comparability adjustment: iVote disappeared. We show
2019 postals plus iVote as the previous remote-voting category, compared with
2023 postals. This is a sensitivity with a strong procedural basis, not proof
that all former iVote users voted postal. Comparing postals alone changes the
composition of that population and gives the wrong impression of its behaviour.
[Antony Green's first-hand analysis](https://antonygreen.com.au/why-nsw-labors-election-night-majority-disappeared/)
identifies the same problem.

Each row below uses the same named districts across both elections and all
categories. These are not redistribution-adjusted districts. State samples
exclude unavailable non-classic preference pairs. The expected sample total
preserves the original analysis's transformed difference between the sample
result and the election-wide result. Thus sample levels are not statewide TPP.
Counts are from the commission source tables used by the original diagnostic;
indicative preference tables can differ slightly from final recounts.

## Forecast centres and accounting

All numbers in this table are Labor TPP percentages. The adjusted centre is
the saved median-level Python reconstruction, not the original GUI forecast.
The raw trend is the last saved unadjusted TPP median. Historical adjustment
files can be retrospective; neither column proves what was forecast live.
The result is the saved eventual-results value used in the original study.

| Election | Districts | Raw trend | Adjusted centre | Result | Sample forecast | Old weighted expectation | Re-centred ordinary error (pp) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2025fed | 149 | 51.94 | 51.47 | 55.22 | 51.50 | 51.43 | +4.90 |
| 2024qld | 84 | 47.47 | 47.43 | 46.20 | 47.58 | 47.71 | +0.97 |
| 2023nsw | 86 | 53.28 | 52.99 | 54.26 | 53.93 | 53.67 | +2.01 |
| 2022vic | 60 | 55.48 | 54.53 | 55.00 | 53.20 | 52.38 | -1.33 |
| 2022fed | 150 | 52.66 | 52.12 | 52.13 | 52.09 | 51.38 | -1.03 |
| 2019fed | 143 | 51.18 | 50.73 | 48.47 | 50.27 | 49.95 | -1.90 |
| 2018vic | 76 | 54.87 | 54.84 | 57.30 | 53.90 | 53.73 | +3.13 |

The old weighted expectation should have equalled the sample forecast but did
not. The difference measures the missing category-size re-centring, not a new
polling error. The re-centred ordinary error is actual minus expected ordinary
TPP, in percentage points; earlier reported errors used transformed points.

## Election-by-election counts

Each category row gives its previous and current Labor TPP, its fraction of
the current two-party count, and its error against the re-centred expectation.
Contribution is that error multiplied by the category fraction. Contributions
sum to actual minus expected sample TPP. Other includes small residual categories
such as provisional and mobile votes; every normalized vote is included once.
The contrast change is the change in category-minus-ordinary log odds, measured
in transformed points. It is not the category's contribution to the final result.

### 2025fed, compared with 2022fed

| Category | Previous TPP | Current TPP | Current fraction (%) | Expected TPP | Error (pp) | Contribution (pp) | Contrast change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ordinary | 53.75 | 58.06 | 40.32 | 53.16 | +4.90 | +1.98 | +0.00 |
| early | 50.89 | 53.33 | 42.03 | 50.30 | +3.02 | +1.27 | -1.94 |
| postal | 49.14 | 51.63 | 13.59 | 48.55 | +3.08 | +0.42 | -1.88 |
| absent | 57.47 | 60.16 | 3.23 | 56.89 | +3.27 | +0.11 | -1.60 |
| other | 59.12 | 55.92 | 0.82 | 58.55 | -2.63 | -0.02 | -7.66 |

Sample result **55.25%**, expected **51.50%**; contributions sum to **+3.75 pp**. Carrying the completed ordinary swing into all previous category shares, with current category fractions, would give **56.42%**. This is a completed-count counterfactual, not a reconstruction of the election-night projection.

### 2024qld, compared with 2020qld

| Category | Previous TPP | Current TPP | Current fraction (%) | Expected TPP | Error (pp) | Contribution (pp) | Contrast change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ordinary | 55.79 | 50.90 | 26.58 | 49.93 | +0.97 | +0.26 | +0.00 |
| early | 52.66 | 44.54 | 50.04 | 46.78 | -2.24 | -1.12 | -3.23 |
| postal | 51.48 | 42.16 | 16.67 | 45.61 | -3.45 | -0.58 | -4.48 |
| absent | 54.72 | 53.72 | 4.63 | 48.85 | +4.87 | +0.23 | +3.91 |
| other | 55.76 | 49.31 | 2.08 | 49.90 | -0.59 | -0.01 | -1.56 |

Sample result **46.36%**, expected **47.58%**; contributions sum to **-1.23 pp**. Carrying the completed ordinary swing into all previous category shares, with current category fractions, would give **48.55%**. This is a completed-count counterfactual, not a reconstruction of the election-night projection.

### 2023nsw, compared with 2019nsw

| Category | Previous TPP | Current TPP | Current fraction (%) | Expected TPP | Error (pp) | Contribution (pp) | Contrast change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ordinary | 49.05 | 56.64 | 50.62 | 54.63 | +2.01 | +1.02 | +0.00 |
| early | 46.67 | 53.23 | 32.90 | 52.25 | +0.97 | +0.32 | -1.06 |
| postal | 46.32 | 50.51 | 8.48 | 51.90 | -1.39 | -0.12 | -3.43 |
| absent | 51.96 | 58.34 | 6.22 | 57.50 | +0.84 | +0.05 | -1.18 |
| other | 57.03 | 61.72 | 1.78 | 62.40 | -0.69 | -0.01 | -2.77 |

Sample result **55.19%**, expected **53.93%**; contributions sum to **+1.26 pp**. Carrying the completed ordinary swing into all previous category shares, with current category fractions, would give **55.95%**. This is a completed-count counterfactual, not a reconstruction of the election-night projection.

### 2022vic, compared with 2018vic

| Category | Previous TPP | Current TPP | Current fraction (%) | Expected TPP | Error (pp) | Contribution (pp) | Contrast change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ordinary | 58.46 | 54.46 | 35.05 | 55.79 | -1.33 | -0.47 | +0.00 |
| early | 54.70 | 53.34 | 49.30 | 52.00 | +1.34 | +0.66 | +2.69 |
| postal | 50.34 | 51.32 | 10.74 | 47.62 | +3.70 | +0.40 | +5.05 |
| absent | 61.65 | 56.62 | 4.45 | 59.05 | -2.43 | -0.11 | -1.14 |
| other | 60.74 | 55.49 | 0.46 | 58.11 | -2.62 | -0.01 | -1.32 |

Sample result **53.67%**, expected **53.20%**; contributions sum to **+0.47 pp**. Carrying the completed ordinary swing into all previous category shares, with current category fractions, would give **51.86%**. This is a completed-count counterfactual, not a reconstruction of the election-night projection.

### 2022fed, compared with 2019fed

| Category | Previous TPP | Current TPP | Current fraction (%) | Expected TPP | Error (pp) | Contribution (pp) | Contrast change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ordinary | 50.44 | 53.71 | 44.78 | 54.74 | -1.03 | -0.46 | +0.00 |
| early | 46.21 | 50.83 | 37.00 | 50.51 | +0.32 | +0.12 | +1.36 |
| postal | 42.39 | 49.02 | 14.61 | 46.64 | +2.38 | +0.35 | +3.42 |
| absent | 53.69 | 57.39 | 3.16 | 57.94 | -0.54 | -0.02 | +0.48 |
| other | 49.66 | 59.02 | 0.45 | 53.96 | +5.06 | +0.02 | +6.19 |

Sample result **52.10%**, expected **52.09%**; contributions sum to **+0.01 pp**. Carrying the completed ordinary swing into all previous category shares, with current category fractions, would give **51.06%**. This is a completed-count counterfactual, not a reconstruction of the election-night projection.

### 2019fed, compared with 2016fed

| Category | Previous TPP | Current TPP | Current fraction (%) | Expected TPP | Error (pp) | Contribution (pp) | Contrast change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ordinary | 50.63 | 49.93 | 53.76 | 51.84 | -1.90 | -1.02 | +0.00 |
| early | 47.35 | 45.75 | 32.59 | 48.55 | -2.80 | -0.91 | -0.90 |
| postal | 43.41 | 41.88 | 8.53 | 44.60 | -2.72 | -0.23 | -0.86 |
| absent | 53.56 | 53.18 | 4.05 | 54.76 | -1.58 | -0.06 | +0.31 |
| other | 50.52 | 49.43 | 1.07 | 51.73 | -2.29 | -0.02 | -0.39 |

Sample result **48.01%**, expected **50.27%**; contributions sum to **-2.25 pp**. Carrying the completed ordinary swing into all previous category shares, with current category fractions, would give **48.37%**. This is a completed-count counterfactual, not a reconstruction of the election-night projection.

### 2018vic, compared with 2014vic

| Category | Previous TPP | Current TPP | Current fraction (%) | Expected TPP | Error (pp) | Contribution (pp) | Contrast change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ordinary | 51.75 | 58.19 | 48.11 | 55.06 | +3.13 | +1.51 | +0.00 |
| early | 49.53 | 54.31 | 36.95 | 52.85 | +1.47 | +0.54 | -1.71 |
| postal | 44.30 | 49.92 | 7.60 | 47.60 | +2.32 | +0.18 | -0.87 |
| absent | 54.57 | 61.65 | 6.66 | 57.84 | +3.81 | +0.25 | +0.78 |
| other | 58.44 | 60.52 | 0.69 | 61.63 | -1.10 | -0.01 | -4.35 |

Sample result **56.37%**, expected **53.90%**; contributions sum to **+2.47 pp**. Carrying the completed ordinary swing into all previous category shares, with current category fractions, would give **57.05%**. This is a completed-count counterfactual, not a reconstruction of the election-night projection.

## NSW marginal seats

All shares below use published 2023 preference counts, including the NSWEC's
[Ryde table](https://pastvtr.elections.nsw.gov.au/SG2301/LA/ryde/TCP/lane-jordan/howison-lyndal/tcp_table.html)
and [Terrigal table](https://pastvtr.elections.nsw.gov.au/SG2301/LA/terrigal/TCP/crouch-adam/boughton-sam/tcp_table.html).

| Seat | Ordinary TPP | Early TPP | Postal TPP | Total TPP | Postal swing minus ordinary swing (pp; 2019 postals + iVote) |
| --- | --- | --- | --- | --- | --- |
| Ryde | 51.93 | 48.06 | 45.19 | 49.95 | -5.08 |
| Terrigal | 51.61 | 46.25 | 37.94 | 48.78 | -11.19 |
| Goulburn | 51.20 | 46.93 | 39.49 | 48.71 | -8.97 |
| Miranda | 48.89 | 46.26 | 42.60 | 47.65 | -6.72 |
| Holsworthy | 49.47 | 49.32 | 47.93 | 49.63 | -2.36 |
| Oatley | 49.85 | 48.32 | 43.87 | 49.20 | -5.83 |

These are same-name district comparisons, not redistributed matched-booth
swings. The direction is informative, but boundary changes particularly limit
Holsworthy and Oatley. Kiama's incumbent changed from Liberal to independent,
so it is not represented by this Labor-Coalition diagnostic.

## Interpretation and limitations

- NSW's postal contrast changes from +3.42 to -3.43 transformed points when
  2019 iVote is included. The early contrast remains -1.06. This supports the
  observed adverse late-count pattern rather than contradicting it.
- Victoria 2022's large positive departures are a narrowing of the exceptionally
  adverse 2018 early/postal gap. Early and postal TPP still lie below ordinary
  TPP in 2022. A five-point postal departure is not a five-point statewide gain:
  postal votes are only about eleven percent of the sample. The published
  broader [VIC category comparison](https://antonygreen.com.au/vic22-results-by-vote-type-and-vote-type-by-electorate/)
  shows the same pattern (ordinary swing -3.5 pp, early -1.2, postal +1.3).
- Federal 2022's saved adjusted centre is 52.12%, effectively the 52.13% result;
  its raw trend is 52.66%. Category-size re-centring makes the ordinary error
  more negative and resolves the apparent failure of positive early/postal
  departures to add up. The old -0.33 was not a complete accounting residual.
- Federal 2019 uses 50.73% as its adjusted centre, not 51.5%. This explains much
  of the smaller measured error. The raw trend is 51.18%; a 51.5% benchmark would
  imply a 3.03-point total miss. These expectations must not be interchanged.
- Victoria 2018's statewide category means mask some district differences.
  The original current-category-weighted mean of within-district transformed
  contrast changes is early -2.42 and postal -2.30, compared with aggregated
  -1.71 and -0.87. This is a composition sensitivity, not a replacement estimate
  on redistributed geography. Completed ordinary counts also conceal the order
  in which particularly strong Melbourne booths reported.

Further calibration needs comparable category definitions, category fractions,
and explicit treatment of geography. A final ordinary category is not the
partially counted ordinary vote at 19:00, nor the mix of ordinary and pre-poll
votes counted by midnight. These data alone cannot identify an intranight peak.

## Reproduction

From `analysis/`, after producing the original cached diagnostic:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.audit_category_tpp
.\.venv-win\Scripts\python.exe -B -m unittest discover -s tests -p test_category_tpp_relationships.py
```

`election-audit.json` retains the exact values underlying this report. Original
commission URL provenance remains in the normalized cache and the original
report's source inventory. No downloads or forecast runs are performed here.

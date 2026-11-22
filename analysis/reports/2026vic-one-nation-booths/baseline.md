# One Nation’s booth baseline after a large rise in support

This study tests what the previous state election can contribute when One Nation stands in more seats and its vote rises sharply. The **district forecast sets the 2026 level**. The question for an ordinary election-day booth is whether the preceding state vote helps locate relatively strong and weak parts of that district. The South Australian test compares old state geography, federal geography and combinations of the two. The Victorian illustration uses the 20 November general forecast and the prepared live turnout weights. [Australian Election Forecasts, 20 November 2026](https://www.aeforecasts.com/archive/2026vic/2746)

## How different is the Victorian starting point?

One Nation stands in all 88 districts. The previous Assembly results have a candidate in five. The table compares each candidate’s actual share of **all district formal votes** with the district’s **median 2026 first-preference forecast**. The last column counts 2026 ordinary venues that match earlier One Nation votes in the same district, divided by all current ordinary venues there. A blank historical count in the other 83 districts records that voters had no One Nation candidate on their Assembly ballot.

| District | Previous share | 20 November forecast | Matching ordinary venues |
| --- | ---: | ---: | ---: |
| Bendigo East | 5.70% | 21.32% | 26 / 28 |
| Macedon | 3.63% | 15.40% | 24 / 25 |
| Morwell | 6.12% | 35.79% | 18 / 20 |
| Narracan | 6.03% | 26.51% | 23 / 32 |
| Pakenham | 3.40% | 28.31% | 11 / 15 |

The archived statewide forecast median was **20.45%**. The district medians ranged from **9.04% in Hawthorn** to **35.79% in Morwell**. Narracan’s previous result comes from the January 2023 supplementary election. Its November 2022 turnout comparison is incomplete, but its later ballot supplies actual named booth votes. [District calculations](baseline-districts.csv) · [VEC Narracan results](https://www.vec.vic.gov.au/results/state-election-results/state-by-elections-timeline/narracan-district-supplementary-election-results)

## Does the former state candidate add geographic information?

In SA, 19 districts had a One Nation candidate in 2022 and 2026. Across those districts, **312 of 335** ordinary booths with formal votes matched the name of a booth with a 2022 result in the predecessor district. This tests name continuity and does not establish that every catchment or candidate remained the same. The **eight districts** with a 2022 share at or below 6.2% provide a closer comparison with the five Victorian candidacies. The threshold rounds up the largest former Victorian share.

The federal method uses the earlier study’s three-site ordinary pattern and 10 km fallback. The state-only method brings the *relative* 2022 booth odds forward to the 2026 level. Unmatched booths take the district expectation. The blend measures how far the old state pattern differs from the federal one at the matched booths, after removing their difference in overall level. It moves partway from the federal pattern towards the old state pattern at those booths. Remaining booths keep their federal estimate. Each test area is excluded from fitting both the federal strength and the state weight. Fitting all SA districts gives the old pattern **40.5%** weight at matched sites. This describes the booth-level fit. [Coefficients by held-out federal area](baseline-strengths.csv)

Each entry below is mean absolute error in percentage points, with equal weight per district. The individual-booth test sets every method to the same completed district ordinary share. The replay obtains the level from the counts published at the first update with one or three ordinary booths reporting at least 100 formal ballots. It predicts the combined vote at ordinary booths still to report, using size estimates available before the election.

| Comparison group | Method | Individual booth | Remaining after first return | Remaining after three returns |
| --- | --- | ---: | ---: | ---: |
| All 19 returning districts | Federal | 2.98 | 3.50 | **1.00** |
| | Previous state | 3.46 | 4.29 | 2.53 |
| | Fitted blend | **2.66** | **3.34** | 1.18 |
| Eight districts starting at 6.2% or below | Federal | 2.89 | **3.11** | **0.82** |
| | Previous state | 3.77 | 4.63 | 2.84 |
| | Fitted blend | **2.67** | 3.31 | 1.33 |
| 28 districts with no former candidate | Uniform | 2.43 | 2.41 | 1.79 |
| | Federal | **1.93** | **2.17** | **1.29** |

The old state pattern contains some booth-level information even at a low previous share. Its addition improved individual-booth error by 0.23 points in the eight closest comparisons. At the three-return milestone it increased error by 0.51 points. The 95% interval for the blend’s **gain** over federal at that milestone runs from −1.07 to +0.26 points when resampling whole federal areas. These eight districts supply little evidence of a reliable improvement to the outstanding vote. The federal-only estimate is therefore the better-supported starting point for a live count. The old state pattern offers a separate sensitivity comparison in five Victorian seats. [Full results and intervals](baseline-comparisons.csv)

## What does an ordinary baseline mean in practice?

The level must refer to the votes being projected. The published forecast covers the **whole seat**. In the 47 SA districts the median difference between the ordinary booth share and the whole-seat share was **−0.82 points**. The median difference was −0.54 for named early centres and +1.37 for postal votes. These are descriptive differences from one election. A geographic contrast between two ordinary booths says nothing about how the postal or early pool should differ from the ordinary pool. [SA voting-mode comparison](baseline-vote-types.csv)

The illustration sets the ordinary target equal to the published whole-district median so the scaling assumption is visible. It uses the live turnout model’s relative ordinary booth sizes. All three patterns reproduce the supplied ordinary target before rounding. Using the VEC venue estimates to centre a pattern and then adding the booths with the live turnout weights would move Greenvale’s ordinary estimate by **+1.11 points** before any count. Using one set of weights throughout removes that discrepancy. [Victorian district checks](baseline-districts.csv)

For **Morwell East**, at an assumed district ordinary share of **35.79%**, federal geography gives **40.33%**, the blend gives **33.48%** and full carry-over of the previous state pattern gives **24.34%**. A result around 40% there would be close to the federal-based expectation. In **Geelong**, which had no previous candidate, the **24.27%** district illustration gives **17.93%** at Fyans Park and **33.47%** at Bannockburn. The spread comes from the difference between federal results near the two venues. The statewide and district forecasts have already supplied the level.

## Reproducing the comparison

From the repository root the first command rebuilds all SA geography comparisons from the public booth table, the local first-return observations prepared by the source reader, and the repository’s 2022 SA booth history. The second also compares voting modes in ECSA’s reviewed final source files and applies the Victorian district medians using the prepared local roster and turnout prior. For the Victorian calculation, save the public archive JSON as `raw/20261120-regular.json` in this study folder, or pass its path with `--report`. Use `--out` for a separate output directory. Per-booth Victorian illustrations are written under `operator/`. [Archived forecast JSON](https://www.aeforecasts.com/forecast-api/election-archive/2026vic/2746/) · [SA validation inputs and source revisions](sources.json)

~~~powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-one-nation-booths\baseline.py
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-one-nation-booths\baseline.py --victoria --sa-final downloads\turnout\2026sa\version-912-b1bb7181a5f8
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-one-nation-booths\test_study.py
~~~

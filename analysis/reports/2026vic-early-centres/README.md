# Estimating the vote at Victoria’s 2026 early voting centres

This study measures how much the result at one early voting centre (EVC) tells us about another in the same district. A centre serving a different part of the electorate can report first and give a misleading impression of the district swing. The test uses party votes at nearby ordinary booths from the preceding election to estimate that difference. The 2026 examples use the roster available on 22 November 2026.

The five nearest ordinary booths support a location adjustment for Labor two-party preferred (TPP) and for the division of first preferences (FP) among Labor, Coalition and Greens. The combined FP share for other parties and independents is more reliable at the district baseline. The centre-volume assumptions also matter, particularly when a shared venue serves voters from several districts.

## How much did geography help at the 2022 state election?

The VEC’s final recheck gives candidate votes at 153 named EVCs across 87 November districts. In each district, adding all named centres and the residual reproduces the published early counts for all four FP groups. There are 64 districts with at least two centres recording 500 or more formal votes. Their 130 centres cast 1,047,096 formal votes and give 68 distinct within-district pairs. The 2022 workbook records first preferences. The separate VEC Labor–Coalition summary is by district. [VEC results and final recheck workbook](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results) · [VEC historical venue coordinates](https://www.vec.vic.gov.au/electoral-boundaries/download-boundary-maps)

For each pair, take one centre’s actual share and predict the other’s, then reverse their roles. The predictors pool the **2018** ballots at the five nearest ordinary booths serving the **2022 reporting district**. A continuing booth must match by name and lie within 1 km of its earlier site. Across the redistribution, a transferred booth needs an unambiguous match. The table gives mean absolute errors in percentage points of formal votes. Each district has equal weight. Within a district, a pair’s weight is proportional to the product of the two centres’ final formal counts.

| Party group | Assume equal shares | Separate federal estimates | Adjust all four groups together | Adjust three, hold Other fixed |
| --- | ---: | ---: | ---: | ---: |
| Labor | 3.80 | 2.77 | 3.20 | **2.84** |
| Coalition | 5.26 | 3.73 | 4.10 | **3.73** |
| Greens | 2.85 | 1.96 | 2.12 | **1.88** |
| Other parties and independents combined | 2.36 | 3.13 | 4.74 | **2.36** |

The separate estimates fit a strength for each group from preceding federal elections through May 2022. They illustrate the individual signals. The two joint methods make all FP shares sum to 100%. Both carry across four-fifths of differences in local party ratios. The last column reserves the observed Other share and divides the remainder among the three main groups. A change in who stands as a local independent or minor-party candidate can change the meaning of the historical Other vote.

Five booths worked across all three major groups. For the fixed-Other method, using one booth gave errors of 4.10, 4.54 and 2.83 points for Labor, Coalition and Greens. Using eight gave 2.80, 3.77 and 2.12. The choice of five and the fixed-Other refinement were assessed against this state election. Their performance in a second Victorian state election remains unmeasured. The recorded scores include all five neighbourhood sizes and district-resampled uncertainty intervals. [Historical scores](scores.csv)

## What happens when the first substantial centre reports?

Victorian federal results provide actual Labor TPP by booth, including divisions with a different final pair. Each test election uses the previous federal election’s ordinary votes. The adjustment strength is fitted only on earlier federal centre results, starting with 2013 outcomes and 2010 ordinary booths. A persistent ordinary-booth ID must stay within 1 km, and its earlier votes are assigned to its current division. The TPP error when predicting one substantial centre from another fell from **6.64 to 3.83** points in 2019, **5.96 to 3.42** in 2022 and **6.34 to 3.96** in 2025. [AEC 2022 booth results](https://results.aec.gov.au/27966/Website/HouseDownloadsMenu-27966-Csv.htm) · [AEC 2025 booth results](https://results.aec.gov.au/31496/Website/HouseDownloadsMenu-31496-Csv.htm)

The election-night test uses 242 retained updates in 2022 and 229 in 2025. In each division, take the first observed EVC count of at least 500 Labor–Coalition votes. Use its actual share at that time to predict the eventual **combined TPP at the other centres**. Both methods start from the exact published share. The test uses the first substantial return, even if smaller interim counts are also available. The measure is the average absolute error in that remaining group’s share, with equal weight per division. Whole-seat movements also depend on how many ballots each category contributes and how the forecast responds to those counts.

| Election | Other centres receiving a prediction | Divisions | Uniform error | Location error | Divisions improved |
| --- | --- | ---: | ---: | ---: | ---: |
| 2022 | Substantial centres, final size weights | 30 | 5.29 | 4.16 | 18 |
| 2022 | Full located roster, pre-count size weights | 30 | 5.33 | 5.23 | 15 |
| 2025 | Substantial centres, final size weights | 26 | 6.32 | 3.57 | 21 |
| 2025 | Full located roster, pre-count size weights | 26 | 6.31 | 3.31 | 22 |

Final weights isolate the error in predicted party shares and use information that arrives after election night. The pre-count version includes small and zero-return centres. It uses the previous count for a continuing centre, the previous division’s median substantial centre for a new or moved site, and the Victorian median for a new division. The roster and coordinates are reconstructed from final official location files. Giving every remaining centre equal weight produced 5.23 and 3.56 points of error in 2022 and 2025. The 2022 replay is a reason to check venue weights before relying on an approaching batch. [District forecast comparisons](replay-comparisons.csv) · [AEC media feed and archives](https://www.aec.gov.au/media/mediafeed/)

For guest centres the state workbook supplies limited direct evidence. As a federal check, a venue is classed as likely outside its reporting division when none of the three nearest unshared ordinary sites belongs to that division. Among pairs containing such a centre, geographic TPP errors fell from 7.13 to 5.12 points in 2022 and from 7.88 to 4.86 in 2025. This supports looking among ordinary booths **of the district whose ballots are being counted**, including when the venue itself is elsewhere. These federal venues are an approximation to Victorian joint-centre catchments. [Catchment comparisons](catchments.csv)

## Applying the test to the 2026 roster

1. **Start with the district’s forecast for ballots reported as EVCEarly.** The residual Early and MobileVotingEarly counts have their own populations. The VEC’s district EVCEarly total includes the detailed venue rows. The live forecast uses each venue’s actual votes once, preserves votes reported only at district level, and applies the venue estimate to outstanding votes.
2. **Pool ballots at the five nearest matched ordinary booths of the reporting district.** For TPP, use the historical Labor-to-Coalition odds and raise them to the fitted power **0.8395**. A ten-point neighbourhood difference around 50% suggests about eight points between EVCs. For FP, raise the local Labor, Coalition and Greens counts to the power **0.8** and divide the available share among them. Half a ballot per group keeps observed zeros finite. The combined share of other candidates receives the district baseline. This study estimates no individual minor-party or independent effect.
3. **Preserve the district expectation.** Use the same multiplier for a given party across the district’s centres, adjusting those multipliers until the volume-weighted centre shares reproduce the supplied district shares. The live forecast also respects each finalist’s own primaries when allocating TPP. A centre lacking FP evidence takes the district mix. Without measured neighbourhood Labor–Coalition TPP, the district preference assumptions apply to the local primaries.

The application covers all **310 district–centre entries in 88 districts**, including 100 guest entries. Five matched historical ordinary booths supply FP evidence for **307 entries in 87 districts**. A direct Labor–Coalition count supplies TPP for **261 entries in 73 districts**. The three Narracan entries have no ordinary comparison in the prepared November turnout match, following its postponed election. Only 87 entries are within 1 km of a named 2022 EVC result for the same district. The ordinary-booth method can also describe new venues. [District coverage and weight sensitivity](2026-districts.csv)

**Polwarth illustrates the size of the effect.** Colac and Torquay’s 2022 EVCs gave Labor 23.9% and 37.9% of first preferences. Around the new 2026 venues, the five ordinary booths gave Labor 35.9% and 64.3% TPP. With an illustrative district EVC TPP of 50% and VEC venue weights of 11,683 and 14,692, the trial allocation gives **36.62% in Colac and 60.64% in Torquay**. Their weighted mean is 50%. The 24.01-point gap is the largest among districts with a direct TPP comparison. [VEC 2022 first preferences](https://www.vec.vic.gov.au/-/media/c4665365795d431e811d0ce818e373d4.xlsx) · [VEC 2026 venue estimates](https://www.vec.vic.gov.au/voting/2026-state-election/voting-locations)

The venue estimates are relative weights in these examples. Melbourne’s six entries sum to 91,121, and the workbook lists separate host and guest entries. How much of a CBD host estimate represents its own enrolled voters is unverified. The district table shows the largest change to any centre when all venues instead receive equal weight. In Ovens Valley that sensitivity reaches 6.12 points for TPP and 5.93 for FP. The FP examples use the district’s actual 2022 **named-EVC** party mix. Both the FP baseline and the illustrative 50% TPP can be replaced by a forecast-date district expectation in [the allocation functions](application.py).

## Reproduce the recorded test

Run these commands from the repository root with the project’s non-Stan Python environment. The first command regenerates historical scores from the published final-result table. It also regenerates election-night comparisons when the local first-report table is present. The second also produces the 2026 allocation using the local prepared roster. The third checks repeatability, missing evidence and conservation of district vote shares.

~~~powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-early-centres\study.py --historical-only
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-early-centres\study.py
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-early-centres\test_study.py
~~~

Each row of [historical-centres.csv](historical-centres.csv) is one election, district and centre. Unprefixed vote columns are final outcomes. Columns beginning with n1, n3, n5, n8 or n12 contain pooled **previous-election ordinary votes** for that many nearby booths, capped at the available number. Formal is the FP denominator. TPP has its own denominator. Blank means unavailable. The recorder keeps the first qualifying AEC updates and their counts under `operator/count-history/`. The public district comparisons give the uniform and location-based predictions, the eventual outstanding share and the errors in percentage points. Files in the local operator folder include the 310 centre estimates, their ordinary-booth names, distances, evidence flags, relative weights and shifts in percentage points. Positive shifts favour the named party. The district CSV records the example baselines.

To rebuild the recorded inputs, the source reader uses the repository’s 2018 and 2022 Victorian booth histories, federal-prepoll and category-TPP source caches, prepared 2026 roster and turnout audit. With those in place, the command below obtains the four public VEC workbooks if missing and rereads the two retained election-night archives. Subsequent runs can omit the fetch option. Source URLs and SHA-256 revisions identify the inputs. The full venue-level outputs and raw setup material stay in the operator workspace. [Source revisions](sources.json) · [Election-night capture inventories](archive-sources.json)

~~~powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-early-centres\record.py --fetch --with-night
~~~

The source reader checks the recorded revisions before replacing tables. For a different roster or capture revision, add the output option with a fresh directory to the command above. Pass that directory to the study script with the inputs option to calculate the new results there. Both commands support the historical-only option on a machine without the operator’s 2026 setup.

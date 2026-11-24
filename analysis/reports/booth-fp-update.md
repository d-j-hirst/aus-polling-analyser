# Testing an order-independent update from ordinary booths

This comparison asks whether an ordinary booth can update the forecast for other places in an explainable way while predicting their first preferences accurately. It measures what each forecast would have known before the next places reported, then compares its district projection with the final result. The prototype fits the current ordinary returns together against a fixed opening geography. Actual votes in every category count in full.

## What was compared

The main state tests use nine saved captures each from Victoria 2022 and South Australia 2026. Each includes its local opening forecast. After inspecting these two elections, the same settings were checked on nine 2025 federal captures. Only districts with some ordinary FP already counted enter the scores. The Victorian election-night count covers 87 districts, excluding the deferred Narracan election. Predictions use the actual forecast at each capture as their comparison. The Australian Electoral Commission and Victorian results come from the repository’s district results tables. South Australian FP outcomes use ECSA’s reviewed candidate totals, including absent ordinary votes. The final distribution identifies the eventual pair.

The prototype was tried with starting weights equivalent to 250, 500, 1,000, 3,000 and 6,000 votes, always with a 500 effective-vote ceiling on the influence of any one booth on *other* booths. These are tuning settings. They do not change the number of votes counted at a booth. The 250-vote setting had the smallest errors for the two main FP measures in **both state elections** among the settings tested. The main comparison keeps the prototype’s opening background fixed. A separate sensitivity applies the archived regional and statewide FP movements before fitting local returns.

District FP error is the percentage of formal ballots that would have to move between FP groups to match the final result, or half the sum of the absolute share errors. The groups are the separately modelled main parties and named independent with opening support, plus one remainder. The same grouping applies to both methods and the result. Each individual booth uses the same error measure against its first subsequent return. Only places with zero FP in the reference capture and comparable predictions from both approaches qualify. Booths are weighted by ballots within each district. For district projections, captures are weighted equally within each district. Each district then counts once in its election average.

## Prediction errors in percentage points

Each cell gives the existing forecast followed by the prototype with a 250-vote starting weight. Smaller errors are better.

| Quantity | Victoria 2022 | SA 2026 | Federal 2025 |
|---|---:|---:|---:|
| Next ordinary booth FP | 7.71 → 7.15 | 8.47 → 7.58 | 6.94 → 6.47 |
| Final district FP | 4.59 → 4.04 | 4.91 → 4.17 | 1.82 → 1.67 |
| Final FP gap between eventual finalists | 4.05 → 3.98 | 3.65 → 3.15 | 1.57 → 1.44 |
| Final TCP share, holding the implied preference split fixed | 2.35 → 2.35 | 2.35 → 2.05 | 1.32 → 1.31 |

The next-booth comparison covered 1,232 Victorian booths, 332 South Australian booths and 5,593 federal booths. The original 3,000-vote prototype had next-booth errors of 8.83, 10.08 and 7.50 respectively. The gain from the smaller starting weight was particularly clear early. In Victorian districts with one ordinary booth in, final FP error fell from 7.09 to 5.83. With at least five ordinary booths in, the errors were 2.88 and 2.91. The corresponding SA comparisons were 7.21 to 5.76 and 3.23 to 2.81. The three archives cover different parts of counting, so compare methods within each election.

The TCP row is a sensitivity check on the FP change. It uses only endpoints where the actual finalists were modelled separately, had at least 65% pairing probability, and a feasible preference split could be inferred from their published FP means and conditional TCP median. Apply that split to the prototype's new minor-candidate pool. This does not redistribute preferences source by source, simulate the exclusion order, or measure win probabilities or uncertainty calibration.

## Which contests behaved differently?

| Actual final pairing | Districts | Final district FP error, existing → prototype |
|---|---:|---:|
| Victorian ALP–LIB | 67 | 4.26 → 3.67 |
| Victorian ALP–GRN | 7 | 7.34 → 6.17 |
| Victorian ALP–NAT | 7 | 3.62 → 3.53 |
| Victorian IND–NAT | 2 | 10.16 → 10.23 |
| SA ALP–ON | 25 | 4.62 → 3.71 |
| SA LIB–ON | 4 | 7.35 → 6.88 |
| SA IND–ON | 2 | 3.71 → 4.19 |
| Federal ALP–IND | 7 | 2.78 → 2.24 |
| Federal IND–LIB | 12 | 2.21 → 1.73 |
| Federal ALP–GRN | 9 | 1.81 → 1.61 |

The prototype improved district FP at endpoints where the largest projected pairing had at most 65% probability. Errors fell from 6.39 to 5.15 across six Victorian districts, 6.80 to 6.10 across 18 SA districts, and 3.91 to 3.45 across 16 federal divisions. This measures the parties’ primaries at a time when the final pair was uncertain. Which two ultimately survived was used only for grouping or scoring.

In the Monbulk replay that motivated the change, the 250-vote prototype moved from a 4.00 pp Liberal FP lead to a 3.46 pp Labor lead after Canterbury Gardens. Wandin East then reduced Labor’s lead to 1.11 pp. This is a much stronger response to the first booth than with the original 3,000-vote setting. Shepparton’s district FP error fell from 9.15 to 8.60 across the Victorian test captures, while Mildura’s rose from 11.17 to 11.86.

## Checks across counting stages

In a separate 153-capture SA replay, the next ordinary booth error fell from 7.69 to 6.86. The first return from a declaration category such as postal or absent votes was predicted less accurately, 8.78 to 9.60 across 288 returns. Those batches may be partial. For district FP projections made between 50% and 80% counted, error rose from 1.48 to 1.69. The prototype extends an ordinary-booth movement to uncounted declarations and does not learn separately from the declaration batches. This is a reason to keep category-specific forecasting when integrating the ordinary-booth update. The full replay is another run of the same election and uses its contemporary model settings.

Allowing an approximate update for shared regional and statewide movements also improved next ordinary booth FP over the existing forecast at the 250-vote setting, to 7.11 in Victoria and 7.64 in SA. A second Victorian replay using 13 early captures improved from 9.67 to 8.88. These checks reuse the same election outcomes.

## How much of the first booth reaches the Monbulk seat projection?

The local-only 7.46-point movement after Canterbury Gardens is a change in the **district gap between the Labor and Liberal primary shares**. The prototype extended its movement to every outstanding ballot. Canterbury Gardens reported 181 FP, including 42.5% Labor, 29.8% Liberal and 16.0% Greens. The opening booth estimates were 35.1%, 44.9% and 7.7% respectively. Wandin East then reported another 125 ballots with a primary gap more favourable to Liberal than its opening estimate.

The following check includes the published shared regional and statewide FP adjustments. Each cell gives Labor's projected TCP against Liberal and then its overall win chance. The first column is a common starting point.

| Treatment of ordinary returns | Before either booth | Canterbury Gardens | Plus Wandin East |
|---|---:|---:|---:|
| Published replay | 53.2% / 74.4% | 53.7% / 78.9% | 55.2% / 86.2% |
| 250 starting votes for all outstanding ballots | 53.2% / 74.4% | 58.0% / 93.7% | 56.7% / 90.9% |
| Same fit, further blended using the average FP precision weight | 53.2% / 74.4% | 53.6% / 77.9% | 53.6% / 77.1% |
| 250 starting votes for ordinary, 1,000 for other locations | 53.2% / 74.4% | 56.3% / 89.7% | 55.5% / 87.4% |
| 1,000 starting votes throughout | 53.2% / 74.4% | 55.0% / 85.0% | 54.7% / 83.2% |

These alternative TCPs and win chances are **sensitivities**. At each capture, the calculation substitutes the alternative district FP means, retains the statewide Greens flow to Labor (about 80%) and infers a rate for the remaining minor votes that reproduces the archived median. It shifts that capture's conditional TCP distribution and estimates how many Labor–Liberal outcomes cross 50%. The archived frequency of the pairing (about 96%) and outcomes against other opponents stay fixed. This isolates the FP effect while omitting any change to the spread or the candidates making the final two under a fresh simulation. The precision comparison uses one share-weighted fraction across parties to keep each booth balanced, 2.5% and then 7.3%.

In the three nine-capture tests, giving the 250-vote fit full effect at ordinary booths and using 1,000 elsewhere produced final district FP errors of 4.24 in Victoria, 4.82 in SA and 1.71 federally. Fixed-flow TCP errors were 2.18, 2.24 and 1.32. Giving the ordinary results zero influence on other voting types raised district FP errors to 5.61, 6.35 and 2.96. All variants count actual ballots at full value. These tests use the fixed opening background across districts, as in the main comparison above.

## Competitive non-major candidates and misleading early counts

This check asks whether moderating the response to ordinary booths improves the final FP estimate for candidates who already had a realistic chance on the opening forecast. It includes named independents, Greens and One Nation candidates with an opening win chance of 5–95%, irrespective of who ultimately made the TCP. The table uses captures with one to four ordinary booths counted and averages absolute candidate FP errors first within each district, then across districts. Errors are in percentage points. This check includes the archived regional and statewide movements in all three diagnostic sequences.

| Election and candidate | Districts | Existing | 250 everywhere | 250 ordinary / 1,000 elsewhere | 1,000 everywhere |
|---|---:|---:|---:|---:|---:|
| Victoria 2022 IND | 15 | 4.85 | 4.80 | 5.78 | 6.20 |
| Victoria 2022 GRN | 8 | 5.73 | 5.61 | 5.49 | 5.48 |
| SA 2026 ON | 16 | 2.82 | 2.43 | 2.93 | 3.43 |
| SA 2026 IND | 6 | 4.05 | 3.77 | 5.12 | 6.16 |
| SA 2026 GRN | 2 | 4.83 | 3.79 | 4.12 | 4.66 |
| Federal 2025 IND | 13 | 4.04 | 2.83 | 2.84 | 3.26 |
| Federal 2025 GRN | 3 | 2.08 | 1.19 | 1.47 | 1.67 |
| Federal 2025 ON | 1 | 5.53 | 2.34 | 3.26 | 3.94 |

The probability check uses archived overall win chances and translates the conditional TCP distribution for the leading pairing when it accounts for at least 65% of draws. It sets aside matched FP and counted TCP ballots and varies the FP allocation for the outstanding vote. One effective rate for all remaining preference sources reproduces the published TCP median before each substitution. The calculation holds the distribution's width, the frequency of reaching each pairing, and other opponents' outcomes fixed. It estimates how much changing the FP centre alone matters. A fresh ranking simulation would also vary preference sources, exclusions, and uncertainty. The program omits probability sensitivities beyond the archived percentile range.

**Mildura.** Ali Cupper started at 39.3% projected FP and a 74.83% win chance. She eventually received 33.86% FP and 48.82% TCP against Jade Benham. Across the first eight ordinary booths she took 35.6% FP against a booth-specific opening expectation of 29.6%. Benham took 30.5% against 38.9% expected. Murrayville provided one of the strongest signals (Cupper 42.9% against 8.9% expected). At 18:55 the cautious update reduced Cupper's estimated win chance from 93.9% to 90.0%. At 19:15 it reduced 98.65% to 98.34%, and at 19:30 it increased 98.91% to 99.26%. By 21:00 the existing forecast had adjusted more quickly in the opposite direction, giving her 28.6% against an estimated 44.2% using the cautious update. At 19:15 the conditional fifth percentile for Cupper against Benham had already risen to 52.77%. A small change in the central FP estimate could do little to that tail.

**Wills.** In the 2025 federal comparison replay (`preferences7`), the Greens reached 96.04% at 20:29. They had 54.1% of 8,788 counted TCP votes against Labor; Brunswick North and Brunswick North West alone added 3,272 votes at around 59–60% Greens between 20:08 and 20:29. The Greens eventually received 48.57% TCP. At the peak, the strong, vote-type and cautious substitutions gave approximately 96.0%, 93.6% and 92.7% win chances. The active ranking replay (`preferences11`) had a much lower sampled peak of 72.25% at 21:31. On that capture its cautious sensitivity was 54.1%. The two older Wills traces hold the opening regional background fixed. They use each replay's own archived distribution and their available timestamps differ.

Among comparable captures with up to 10% of district FP counted, the 95% check covered 39 competitive candidate–district cases with a dominant pairing and interpolable probabilities under all three substitutions. Mildura was the only eventual loser assigned 95% or more by any method; all four methods crossed that threshold there. The threshold check cannot assess candidates whose revised FP might change their chance of surviving exclusions in a three-way race.

### Did uncertain historical booth results explain Mildura or Wills?

Mildura gained territory around Charlton and Donald from Ripon. Cupper had never stood there. The VEC estimated her historical TCP in the transferred territory using her 36.28% share in the part of Buloke Shire she had already represented. The replay's district settings also include a 0.70-point redistribution adjustment towards the Nationals. The historical FP numerator for Cupper and the formal denominator at **each of the first eight booths** in the 19:15 replay match the 2018 Mildura booth file exactly. All eight were in the old district. Charlton and Donald had yet to report. [VEC redistribution methodology](https://www.vec.vic.gov.au/-/media/4dd734d659a44a08b45d415501981cc6.pdf) · [2018 VEC booth primaries](https://itsitecoreblobvecprd01.blob.core.windows.net/public-files/historical-results/state2018/fpvbyvotingcentremilduradistrict.html)

Two places accounted for more than the early FP overperformance. Murrayville and Wycheproof gave Cupper 41.2% of their 648 FP against a combined opening expectation of 17.9%. The remaining six gave her 32.7% of 1,248 FP against 35.7% expected. As a sensitivity check, keeping all 1,896 ballots fixed while omitting those two booths from the evidence about *outstanding* votes lowered the cautious prototype's projected district FP from 44.4% to 39.9%. The strong prototype went from 46.3% to 38.5%. Both comparisons retain the same regional and statewide background. The two booths' FP gains survived the recheck. Compared with 2018 at the same places, weighting by 2022 formal ballots, Cupper gained 27.4 points there and 2.5 points in the other six. Across the 25 other comparable old-district election-day booths she lost 0.4 points. [2022 VEC booth primaries](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/mildura-district-results/mildura-district-results-by-voting-centre)

The largest numerical miss was outside the newly added territory. To compare forecast and result by location, the commentary benchmark allocates the published district TCP median across booths, preserves counted TCP and respects reported FP. These are reconstructed allocations of the seat forecast. At 19:15 this put Cupper at 45.2% across Charlton and Donald, compared with 34.2% in their eventual 1,244 booth TCP ballots. It put her at 63.4% in the early vote, compared with 51.3% in 18,610 ballots. The latter accounts for about 5.9 points of district TCP when weighted by the recorded booth turnout, versus 0.35 points for Charlton and Donald. These calculations use the VEC's indicative booth TCP table; the final distribution determines the official district result. [VEC booth TCP](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/mildura-district-results/mildura-2cp-results-by-voting-centre) · [Final distribution](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/mildura-district-results/mildura-results-distribution)

Wills also gained voters on redistribution. At 20:29 in the older federal replay, the seven election-day booths reporting TCP with a historical comparison from Wills itself collectively gave the Greens 54.6%, up from 48.4% at those booths in 2022 (weighted by the TCP ballots then counted in 2025). The comparable transferred Fitzroy North booth gave the Greens 53.1% against 67.9% for Adam Bandt on the old Melbourne ballot. The 23 further same-district, matched election-day booths ultimately swung 9.3 points to the Greens; the five further matched election-day booths flagged as coming from a different district swung 8.7 points away. These last two comparisons weight the prior booth shares by each booth's final 2025 TCP turnout. Historical party totals may be well measured even where candidates, polling venues and voters change. [AEC redistribution](https://www.aec.gov.au/redistributions/2023/vic/announcement.html) · [2022 Melbourne booth results](https://results.aec.gov.au/27966/Website/HouseDivisionPage-27966-228.htm)

At that peak the district median was 52.64% Greens TCP. Its reconstructed postal estimate was 45.6%. The final 18,977 postal ballots gave the Greens 37.4%, almost identical to their 37.3% share of the previous Wills postal count. Across physical early voting centres, weighting the individual benchmark shares by their eventual turnout gives 50.7% compared with an actual 47.7%. The district finished at 48.57%. The largest blocks of outstanding votes had reason to differ from the first ordinary booths even where the comparisons for those booths were genuine. [AEC 2022 Wills results](https://results.aec.gov.au/27966/Website/HouseDivisionPage-27966-234.htm) · [AEC 2025 Wills results](https://results.aec.gov.au/31496/Website/HouseDivisionPage-31496-234.htm)

### Testing a middle-booth adjustment

This test asks whether particularly good or bad results in a few places pull the forecast too far for ordinary booths still to report. For each FP group, take the observed share minus its **opening** booth expectation in percentage points. Order these departures and find the middle one by counted formal ballots, averaging both middle values at an exact tie. Starting with three ordinary booths, add a fraction of the difference between this median and the vote-weighted mean to the ordinary-booth prototype. Taper it by the prototype's existing effective ballot total divided by that total plus the opening vote equivalent. Balance the revised FP shares at each booth and add back measured ballots at full value. Uncounted declarations keep their original model for this test.

At 19:15 in Mildura the mean Cupper departure was +5.99 points, the weighted median −5.24 (Boundary Bend). Three below-expectation booths supplied 961 of 1,896 ballots. With a 1,000-vote opening and a half-strength median correction, Cupper's final projected FP moved from 44.38% to 43.53%. With the previously described fixed-spread TCP sensitivity her conditional median fell from 59.93% to 59.26% and her win chance from 98.34% to 97.75%. Those probabilities describe the prototype.

Across nine captures each for Victoria 2022, SA 2026 and federal 2025, with shared movements enabled, the cautious prototype's next ordinary booth errors (the full FP mix, averaged by district) were 7.700, 8.809 and 6.517 points. With a quarter-strength correction these became 7.685, 8.820 and 6.512. With a half correction they were 7.678, 8.843 and 6.520. The corresponding final district FP errors under the half correction moved from 4.597 to 4.587, 5.223 to 5.220 and 1.674 to 1.668. The observed differences across seats do not establish an optimal weight.

### Testing an adjustment for booths transferred from the same former district

This test asks whether a return at a transferred booth helps predict outstanding ordinary booths transferred from **that same district**. Federal booth IDs link each 2025 booth to its 2022 parent. For a given TCP pair, require measured history, at least one distinct reporting booth and at least one still unreported with projected ballots in that group. The replay checks the ALP against LIB, NAT and GRN separately. Only still-unreported booths enter the score. Use the actual final vote volume to weight errors within the pending group, average observations within each destination/source/pair, then give each such group equal weight.

Two comparisons matter. With a seat-wide swing as the starting prediction, replacing half of it with the source-area swing reduced error across 52 eligible groups from 4.35 to 3.14 points. For the more realistic comparison, start with the archived forecast allocated to booths, calculate the average swing it already implies for the pending source area, and move that towards the measured swing. The trial gives each report an effective booth count of ballots divided by ballots plus 250. Divide their sum by one plus that sum for the fraction of the difference applied. Preserve each booth's other forecast differences and counted-primary lower bounds for the two finalists. Among 43 groups with usable dominant-pair forecast benchmarks, error fell from 3.61 to 2.63 points; 34 improved. Excluding Wills, the figures were 3.57 and 2.58. These are repeated observations in one election.

At 20:29 in Wills, the one former Melbourne booth with TCP (Fitzroy North) swung 14.78 points to Labor. The overall matched ordinary swing was 3.30 points away from Labor. The five outstanding Melbourne booths ultimately swung 8.72 points to Labor. The existing forecast already implied some movement towards Labor there. The trial applied 45% of the remaining difference, moved the conditional district TCP median by 0.35 points to Labor, and reduced mean absolute booth error from 8.34 to 7.01 points. By 20:59 the area swing from three booths was +9.51 and the remaining three swung +9.13. The archived forecast already incorporated local FP evidence at some of those booths. The added adjustment then slightly worsened their individual errors, from 5.60 to 5.97, despite the accurate aggregate swing.

### Why the full middle-booth adjustment moved Mildura by 1.73 points

At 19:15 the word “full” meant using all of the 11.23-point difference at the median-correction step. The ordinary-booth update still had a 1,000-vote starting weight. The 1,896 ballots counted across eight locations supplied 1,132 effective votes about other locations. That gave the correction 53.1% weight and reduced the downward adjustment for Cupper to 5.96 points. Each party has a different middle booth. Making their revised shares add to 100% at each location left a turnout-weighted reduction of 4.83 points across outstanding ordinary ballots.

There were 14,184 of those ballots in an estimated 39,667-vote district. The rest included the ballots already counted and the projected early and declaration votes. Multiplying 4.83 by 35.76% gives a district reduction of 1.73 points, from 44.38% to 42.65% FP. The 1,000-vote starting weight and the decision to extend the correction only to other ordinary booths are assumptions of this prototype.

### Does disagreement between early booths identify excess uncertainty?

This test asks two questions. Does a wide spread of booth results identify harder forecasts? Does the spread add information beyond the uncertainty the forecast already publishes? For each candidate, compare each ordinary booth's FP share with its opening estimate, then calculate the vote-weighted standard deviation of those differences in percentage points. Booths that all miss by ten points in the same direction have little disagreement. Use one observation per seat at each of three stages (3–5, 6–10, and 11 or more ordinary booths reporting), up to 50% of projected district FP counted. Include candidates with at least 5% opening projected FP and an identifiable result. For the next-booth check compare the current forecast with ordinary booths first reporting in the next available capture, weighting the absolute errors by the size of each new return.

For each test election the other two supplied the median disagreement for each party group and stage. Major parties share a group. An above-median reading is classified as high. The groups below count candidate and stage observations. The final result and the later returns enter only when scoring.

| Election | Observations, low / high | Next ordinary FP error, low / high | Final district FP error, low / high | Coverage of published 80% interval, low / high |
|---|---:|---:|---:|---:|
| Victoria 2022 | 244 / 296 | 3.05 / 4.49 | 1.51 / 2.16 | 94.7% / 91.2% |
| South Australia 2026 | 153 / 123 | 3.20 / 4.51 | 1.18 / 1.66 | 95.4% / 95.9% |
| Federal 2025 | 513 / 442 | 2.64 / 3.56 | 1.09 / 1.64 | 96.7% / 90.5% |

The next-booth comparison has fewer observations because the next capture may supply no new ordinary booths or no usable benchmark. Widening both sides of the published FP interval by 20% at high disagreement worsened the interval score in every election. That score charges one point for each point of interval width and ten for each point the result falls outside the central 80% interval. Restricting the rule to readings above the other elections' 90th percentile also worsened the overall score in each election. At 19:15 Cupper's 14.28-point disagreement exceeded that independent threshold of 11.30. Even a 20% widening lowered her published tenth percentile only from 36.90% to 35.38%, above her final 33.86%.

A second comparison calibrated the overall width on the two training elections before asking whether disagreement should change it. Using separate high/low scales, partly weighted back towards the common scale, changed the interval score from 8.45 to 8.43 in Victoria, 6.65 to 6.61 in SA and 6.196 to 6.198 federally. The absolute difference between the median and mean booth departures was less consistent than the standard deviation. For actual TCP pairings with at least 65% forecast probability, a similar check used the disagreement about the two finalists' FP gap. A 20% widening again worsened the score in all three elections. These tests concern the archived intervals. They do not estimate tail probabilities for a new simulator.

### Does a transferred area's improvement carry through to district TCP?

This comparison adds the estimated Labor vote changes from all eligible transferred areas before scoring each district's conditional TCP median against its official final distribution. Each seat receives equal weight after averaging its eligible election-night captures. Unaffected booths, turnout, the projected pairing and the rest of the district stay as forecast. Penalties for errors at individual outstanding booths use their final TCP turnout, with equal weight for each source-area group after averaging its captures. Both replays cover **the same federal election**.

The second variant limits the adjustment where finalists already have FP ballots counted. For example, if those two candidates already own 70% of a booth's expected ballots, scale the source-area change to 30% of its original size. Both variants preserve measured TCP and each finalist's counted primaries.

| Federal 2025 replay | Measure | Archived | Source-area adjustment | Scaled to ballots still assignable |
|---|---|---:|---:|---:|
| preferences7 | Outstanding booth TCP error, 43 areas | 3.61 | 2.63 | 2.64 |
| preferences7 | Final district TCP error, 34 seats, 283 seat-captures | 1.100 | 1.102 | 1.105 |
| testing2 | Outstanding booth TCP error, 21 areas | 4.18 | 3.02 | 2.93 |
| testing2 | Final district TCP error, 19 seats, 43 seat-captures | 1.518 | 1.474 | 1.479 |

In Wills at 20:29, weighting the five outstanding former Melbourne booths by their eventual turnout, their average absolute error was 8.34 points in the benchmark, 7.01 after the source correction and 5.26 when scaling for counted FP. Labor's district median moved from 47.36% to 47.71% or 47.57% respectively, against a final 51.43%. A method can improve predictions at individual places while shifting the district median less.

**Hughes was the largest regression.** At 21:31 the four former Werriwa booths with TCP averaged a 2.17-point swing away from Labor. The two yet to report went 7.02 points towards Labor. Macquarie Fields East initially swung 13.8 points away, revised to 2.2 points away by the final capture. The other three counted booths at 21:31 were all 2.5–3.2 points towards Labor. The published forecast already implied a 5.77-point swing towards Labor in the pending area. Moving that towards the reported result increased pending-booth error from 1.25 to 7.04 points. Neither pending booth had counted FP to limit the second variant. Across all Hughes sources the largest deterioration in district TCP error came at 21:22, 0.32 points or 0.28 with the FP adjustment. No scored district snapshot worsened by half a point in either replay.

Other reversals included New England, where North Star's 91 votes swung 11.38 points away from Labor while Gravesend and Warialda ultimately swung 2.70 points towards Labor between them. In testing2 at 19:13 the pending-booth error rose from 1.38 to 4.05 points. Across the longer series the unscaled rule also worsened mean booth error by 1.19 points for Mitchell's former Berowra booths and 1.18 for Hotham's former Chisholm booths. Scaling for counted FP reduced the Mitchell deterioration to 0.20 points and the Hotham group improved. Shared geographical origin supplies information, but early booths and subsequent corrections can still be unrepresentative of the remaining area.

### Does extending the median correction make forecasts more accurate or more stable?

This trial uses the same ordinary FP returns to calculate the correction in every variant. From the third reporting booth onwards it moves the forecast towards the turnout-weighted middle booth's departure from opening expectations. The scope determines who receives it. Ordinary applies it to outstanding ordinary ballots. All also applies it to outstanding early and declaration votes. Counted ballots in every category always keep their reported distribution.

The table gives the cautious fit with **no correction / full ordinary-only correction / full all-vote correction**. Each district counts equally. FP error is the percentage of final formal ballots that would need to change candidate group to reproduce the forecast. Revision size measures the change in a candidate's win probability between adjacent archived captures using relative entropy (Bernoulli KL, in thousandths of a nat). On this scale a move from 50% to 45% costs 5.0, and a move from 99% to 94% costs 58.8. The lag slope measures how much the previous probability change predicts the next one. A positive slope indicates average continuation and a negative slope indicates reversal. Zero would be consistent with an unpredictable next move. A quiet forecast can still be poorly calibrated.

| Replay | District FP error (pp) | Revision size | Lag slope |
|---|---:|---:|---:|
| Victoria 2022, nine captures | 4.597 / 4.582 / 4.599 | 18.06 / 17.54 / 17.64 | +0.102 / +0.100 / +0.102 |
| SA 2026, nine captures | 5.223 / 5.222 / 5.288 | 24.29 / 24.30 / 24.86 | −0.429 / −0.357 / −0.272 |
| Federal 2025, nine captures | 1.674 / 1.665 / 1.686 | 30.03 / 29.99 / 30.59 | +0.309 / +0.304 / +0.272 |
| SA 2026, 153 captures | 2.268 / 2.267 / 2.303 | 0.872 / 0.871 / 0.919 | +0.113 / +0.111 / +0.094 |

In Mildura at 19:15 the cautious FP estimate for Cupper went from 44.38% without a median correction to 42.65% for ordinary only and 39.81% for all votes. The corresponding win probabilities were about 98.3%, 96.9% and 92.0%. She ultimately received 33.86% FP and lost. Half strength across all votes gave a district FP error of 4.576 in Victoria, 1.661 federally and 2.271 in the full SA series. The short early Victorian run and the nine selected Victorian captures overlap. The SA series also overlap.

Separately, reducing the opening weight from 1,000 to 250 equivalent votes, with the median correction off, improved full-series SA FP error from 2.268 to 2.019 and increased revision size from 0.872 to 1.025. Its lag slope fell from +0.113 to +0.092. Candidate win-probability error against the eventual winner (mean squared error, or Brier score) rounded to 0.0213 for both. Federally the stronger fit reduced the lag slope from +0.309 to +0.165 and improved the Brier score from 0.0543 to 0.0524. These comparisons leave room for a stronger setting as well as for changes to how the model uses new information.

The win-probability measures reconstruct changes for initially competitive seats using the dominant pairing (at least 65% of draws) and the archived conditional distribution. Its width, pair probability and results in other pairings stay fixed. The same named matchup must qualify on consecutive captures. Probability changes beyond the saved tails use the nearest available percentile, with probabilities bounded at 0.05% and 99.95% for scoring. The 153-capture SA series supplied 3,913 adjacent pairs and 3,875 adjacent triples. Resampling whole district histories gave a 95% interval of +0.046 to +0.190 for the cautious uncorrected lag slope and +0.029 to +0.186 for the full all-vote correction. These intervals share the same election-wide movements. The capture intervals differ between replays and make the levels of revision size unsuitable for comparisons **between** replays.

An additional sensitivity allowed early and declaration counts to **supply** the median as well as receive the adjustment, and included initially safe seats in the win-probability scores. In the full SA replay (6,193 eligible adjacent seat forecasts), the cautious full correction then had FP error 2.301, revision size 0.652 and lag slope +0.090. Using only ordinary returns to supply it gave 2.303, 0.644 and +0.093 on the same sample. Ordinary-only correction gave 2.267, 0.613 and +0.110. Adding the other categories as evidence did little to offset the accuracy cost of extending the full correction to them.

### Should ordinary votes respond more strongly than other vote types?

The split update uses the same ordinary booth evidence for both calculations. It uses a 250-vote starting weight when predicting outstanding election-day ordinary ballots and 1,000 for outstanding early, declaration and other ballots. A larger starting weight means a smaller response. In all three methods every observed ballot counts in full. This comparison switches off the median correction and uses the same archived statewide and regional movements.

For the **153-capture SA replay**, lower FP error means a more accurate district primary mix. Lower Brier error means a more accurate estimate of who wins. Lower revision size means smoother win probabilities. For the slope, the target is zero.

| Method | District FP error (pp) | Winner Brier error | Confidence-sensitive revision size | Next-move slope after ≥5 pp | Qualifying moves |
|---|---:|---:|---:|---:|---:|
| Cautious everywhere | 2.268 | 0.0213 | 0.872 | +0.137 | 64 |
| Strong ordinary, cautious elsewhere | 2.135 | 0.0210 | 0.918 | +0.130 | 61 |
| Strong everywhere | 2.019 | 0.0213 | 1.025 | +0.115 | 73 |

The split recovered 53% of the stronger method's FP error reduction with 30% of its increase in revision size. For the nine Victorian captures the FP errors in the same order were 4.597, 4.368 and 4.223 pp, and the revision sizes were 18.06, 18.80 and 22.37. Federally the errors were 1.674, 1.628 and 1.636 pp, and revision sizes were 30.03, 31.36 and 35.31. The split had the lowest federal FP error by a small margin. Its federal next-move slope was +0.262, between +0.305 for cautious and +0.159 for strong. In Victoria the corresponding slopes were +0.038, −0.003 and −0.064 for cautious, split and strong.

The **revised slope** includes a forecast movement if the preceding change in that candidate's win probability was at least 5 percentage points in either direction. Each qualifying pair of movements contributes once. The regression divides the sum of their products by the sum of the squared preceding changes. A slope of +0.13 corresponds to a further move of about 1.3 pp in the same direction after a 10 pp move. A zero or small subsequent change is a valid outcome. Excluding it based on its size would use information unavailable at the time of prediction. FP error, Brier error and revision size still use all their eligible observations. The earlier median-correction table used all preceding moves and first averaged them within districts. Its slope is available using `--min-move-pp 0` and the `equal_seat_lag` column.

Five points is an editorial choice for materiality. At thresholds of 2, 5 and 10 pp, the split slopes in the full SA series were +0.115, +0.130 and +0.150, based on 190, 61 and 24 moves. Requiring changed candidate-level local FP or TCP during the triggering move gave +0.140 across 55 cases at the 5 pp threshold. Requiring all methods to use the same 70 moves selected by the archived forecast gave +0.152, +0.142 and +0.120 for cautious, split and strong. The local check compares reported booth/candidate ballots at the two endpoints, including transfers that leave turnout unchanged. It identifies the presence of local counting without attributing the forecast movement to it.

The split also moderates early individual-seat responses. At 18:45 in Mildura, Cupper's estimated win chance was 83.6% under cautious, 86.9% under split and 90.9% under strong. By 19:15 the corresponding figures were 98.3%, 98.8% and 99.2%. The choice of strength alone would still have left the eventual loser a strong favourite at that point.

### Why the two SA sequences give different slopes

The short replay takes nine election-night observations. Its filenames and the full replay's snapshot codes use different timestamps, but all nine original XML filenames match. The opening through the ninth feed spans 48 captures in the full replay. The following comparison isolates the effect of skipping captures, using the strongest update in each case. Intervals resample whole district histories within this election.

| Sequence | Captures | Moves preceded by ≥5 pp | Next-move slope | 95% interval |
|---|---:|---:|---:|---:|
| Full SA replay through 1 April | 153 | 73 | +0.115 | +0.045 to +0.180 |
| Same run through the short replay's last feed | 48 | 41 | +0.164 | +0.050 to +0.225 |
| Same run restricted to the nine exact feeds | 9 | 17 | −0.016 | −0.214 to +0.165 |
| Separately generated nine-feed replay | 9 | 20 | −0.159 | −0.599 to +0.168 |

In the short replay, the last change compares the XMLs updated at 19:54:48 and 21:06:51 on election night. Over this 72-minute gap Stuart's independent rose 58.5 pp after falling 8.6, and Labor in Bragg fell 39.7 pp after rising 6.5. Neither preceding move had a change in the seat's reported FP or TCP ballots. Both subsequent moves did. Leaving out that last interval gives +0.025 for the other 14 qualifying moves. Keeping the entire short replay and requiring local counting on the preceding move gives +0.142 across 13 cases. The evidence for systematic reversal on this short series is weak. The longer run shows some continuation even after removing small moves. All these probability comparisons hold the archived pairing mix and conditional spread fixed while testing the different FP estimates.

The run itself makes a difference too. In the full run sampled at those nine feeds, Stuart's independent fell only 2.0 pp before rising 46.9 pp. That preceding move falls below the 5 pp threshold. This is one reason that the two nine-feed rows differ even with the same counting data.

## Use in the live simulation

The simulator uses the split update. Ordinary election-day returns inform both fits, with a 500-vote ceiling on how much one booth can tell us about other locations. The opening estimate carries the weight of 250 votes when projecting outstanding ballots at ordinary election-day booths, and 1,000 for other outstanding ballots, including early centres and declarations. Once per capture the simulator aligns booth geography to the district's opening vote mix, applies statewide and regional movement, and fits the ordinary returns. It weights each venue by its outstanding turnout when combining the estimates. Counted ballots enter in full. The fitted centre is available to named candidates with a prior. Candidate identities without an individual prior keep their booth-based variation. Early and declaration returns continue to supply their own evidence through the existing integration. The existing correlated uncertainty draws vary around the fitted centre. Diagnostics identify the method as `split_ordinary_fit`. The median correction remains a replay experiment.

The live TCP calculation uses the source-area comparison for eligible Labor–Liberal, Labor–National and Labor–Greens matchups in compulsory-preference contests. It requires exact historical finalists, genuine ordinary locations and a distinct reported and still-unreported booth from the same former district. The classic comparison also requires a single Coalition candidate. The live version reconciles the booth geography to each draw's district share by the votes free to move in that direction. The archived comparison above used the published conditional median. Preparation tabulates the district adjustment across possible shares. At simulation time only unresolved preferences can move, protecting measured TCP and the finalists' own primary votes. The diagnostic records the contributing former districts and their counts.

Passing the archived booths through the native source-area calculation reproduces the Wills result at 20:29. Fitzroy North supplied a +14.78 pp swing to Labor across 1,103 TCP ballots. Five former Melbourne booths still awaited TCP. At the archived 47.36% district estimate the native adjustment gives 47.57% Labor. In Hughes at 21:22 the Macarthur and Werriwa groups together lowered Labor's estimate from 52.82% to 52.54%. At 21:31 they lowered it from 52.54% to 52.38%. Those checks isolate the geographic adjustment with the archived district forecast held as its starting point.

## Reproduce the comparison

From the repository root, with the local replay archives and the reviewed SA results installed, run:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/score_booth_fp_update.py
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/score_booth_fp_update.py --shared --median-fraction 0.5
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/redistributed_booth_swing.py
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/redistributed_booth_swing.py --run 2025fed-testing2
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/booth_disagreement.py
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/score_booth_fp_update.py --other-prior 1000
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/monbulk_seat_response.py
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/nonmajor_booth_response.py 2022vic-testing5 --shared
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/nonmajor_booth_response.py 2022vic-testing5 --shared --seat Mildura
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/nonmajor_booth_response.py 2025fed-preferences7 --seat Wills
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/score_booth_fp_update.py 2022vic-testing5 2026sa-testing1 --shared
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/score_booth_fp_update.py 2026sa-changes2-261122 2022vic-6-initial
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/booth_revision_trial.py 2022vic-testing5 2026sa-testing1 2025fed-testing2 2026sa-changes2-261122 2022vic-7-initial
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/booth_revision_trial.py --all-sources --include-safe 2026sa-changes2-261122
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/booth_revision_trial.py --min-move-pp 5 --match-feed-run 2026sa-testing1 2026sa-changes2-261122
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/booth_revision_trial.py --min-move-pp 5 --match-feed-run 2026sa-testing1 --keep-intermediate 2026sa-changes2-261122
```

The default prints the three main election comparisons, overall and by actual pairing. Use `--output <path.csv>` to retain individual paired errors, count progress and the probability of the most likely pair. These commands use saved forecasts and do not require a simulator rebuild.

# Federal 2025 live turnout count prototype

This report examines whether using a shared estimate of the eventual vote total improves live estimates of votes still to be counted. It compares the existing booth/category size rules with the turnout prototype at successive retained Federal 2025 result updates. It measures formal vote counts; it does not calculate party shares, winners or seat probabilities. The initial comparisons use the broad turnout prediction. The counting-progress comparison below additionally varies declaration additions and can reduce the district total as counting slows.

## Inputs and comparison methods

The AEC polling-place preload supplies current district, booth ID, name and type. The retained final 2022 House feed supplies actual previous counts. Current first-preference counts come directly from the 2025 detailed Light ZIP archives. Historical “Ghost” candidates, two-candidate counts and Senate results are excluded. Candidate totals reconcile with formal summaries; booth totals reconcile with district ordinary counts. A numeric zero is retained as reported; a missing measurement is not replaced with zero.

**Count baseline** is a Python reproduction of the deterministic size rules in `LiveV2.cpp`. Reported ordinary booths, including ordinary pre-poll voting centres (PPVCs), use their counted totals. Unreported ordinary booths use previous counts; a missing or zero previous count uses the existing 500-vote fallback, or 50 for a hospital booth. Unreported PPVCs use previous counts or 500, multiplied by the existing national PPVC size factor. That factor is (2,000 + current votes at matched reported PPVCs) / (2,000 + their previous votes). Each declaration category uses max(current count, integer part of 1.05 × max(previous count, 30)), using the same 32-bit arithmetic as the C++ calculation. The reference deliberately retains these existing fallback and bound rules.

This reference does not run the GUI, prepare party-share distributions or include the simulator’s random declaration-size variation. It is a central count-rule comparison, not an export from a full simulation. Divisional Office booths and those identified by `BLV` in their official names retain LiveV2’s Other treatment and do not train or receive its PPVC multiplier, while remaining in their official vote category.

1,185 current booths have no accepted previous-ID match under the existing rules. Their previous measurements remain unknown in the export; the baseline fallback is an estimate, not an invented observed zero. Previous district declaration counts cannot be matched by name for: Bullwinkel. The analysis makes no inferred predecessor assignment. Explicit project previous-seat aliases can be supplied when known.

**Prototype** uses the saved pre-election distribution for Federal 2025, trained on earlier elections. It combines previous turnout and formality, current enrolment, district postal-application counts and an aggregate early-vote estimate. Federal district early-vote measurements are not used as direct district targets because counting-location and elector-district totals differ. Final counts do not enter the prior or current counted account. This is one earlier-only training example, not a comparison establishing that this training choice is superior to leaving out each test election in turn.

The operational inputs are saved final pre-election records, including later source reconciliation where present. This replay tests those fixed estimates against the live count progression; it does not reconstruct every version of an operational source available at the time of the original election.

The fitted parameters and operational measurements remain frozen, but the replay uses the enrolment recorded in the no-results election-day feed. This differs from the fixture’s later final published roll by 7,206 electors nationally. Individual differences are retained in the export. The later roll is not substituted into prediction inputs.

The six groups are **ordinary** (ordinary votes outside the pre-poll group, including mobile services), **ordinary pre-poll**, **declaration pre-poll**, **absent**, **provisional** and **postal**. Provisional votes use the fixture’s `other` key. Ordinary pre-poll and declaration pre-poll are separate parts of the combined early-vote estimate. All six sum to one district formal total. Previous counts and explicit starting-size assumptions supply fixed relative booth weights within each group; final booth sizes never supply those weights.

### Starting sizes for new or changed pre-poll centres

This allocation prevents a new public centre from receiving a tiny fallback while existing centres inherit almost all the district’s expected early vote. It changes the shares assigned to centres within the existing category expectation; it does not increase that expectation.

The historical reference is 2022 centres without an exact positive-count same-district, same-name and same-type match in the 2019 feed. EAV, Divisional Office and BLV services are excluded. A location screen retains centres whose nearest ordinary booth belongs to the home electorate. The 148 retained centres have a median of 7,404 formal votes and a mean of 7,764.

For 2025, coordinates come from the election-day preload. A centre is **likely local** when its nearest ordinary booth belongs to its electorate, **likely outside** when none of the nearest three does, and **uncertain** otherwise. Missing coordinates remain unknown. These labels are a practical location screen, not a boundary lookup. Ordinary venues listed for multiple electorates at the same coordinates are excluded from the geographic reference, because their assignments do not establish which electorate contains the site. An outside city service such as Melbourne CALWELL PPVC does not train the local historical estimate or receive the large local fallback.

Unreliably matched public PPVCs screened as local use the historical median for their seat type as their starting weight. Other centres retain their previous count or existing fallback, and EAV retains its separate treatment. Weights are normalised within each category. The new size assumption does not turn a changed centre into a reliable observation for learning trends or compensation. No 2025 final count chooses these weights.

The project’s existing seat classifications separate city electorates (inner and outer metropolitan), regional urban electorates (provincial), and rural electorates. This checks whether a single national size assumption overestimates small rural services. Each row describes the retained 2022 new/changed local centres, with formal votes per centre. Districts count distinct electorates in that row. An unclassified seat uses the national reference.

| Seat type | Districts | Centres | Mean votes | Median votes |
| --- | --- | --- | --- | --- |
| Inner metropolitan | 28 | 41 | 8,439 | 8,078 |
| Outer metropolitan | 27 | 40 | 8,852 | 8,127 |
| Provincial | 19 | 31 | 9,513 | 8,557 |
| Rural | 21 | 36 | 4,282 | 3,760 |

## What does a still-unreported centre imply as counting progresses?

A centre still without a formal return several days after polling may contribute fewer future votes than its starting expectation. This analysis checks that possibility before reducing its expected size: a large centre can also simply report late. Public PPVCs are considered separately from EAV services, which are excluded here.

The historical checks use Federal 2019 and 2022. At each saved update, **Unreported centres** counts public PPVC identities present with no formal votes. **Later positive returns** counts those that eventually report positive formal votes under that same identity. **Total ratio** is their combined eventual formal feed count divided by the combined starting expectation of all currently unreported centres. **Mean centre ratio** averages each centre’s eventual count divided by its own expectation, giving small and large centres equal weight. Both ratios are shown as percentages; 100% means the starting expectation was met.

Starting expectations use the preceding election’s count, or an older local new/changed-centre median for that seat type. They do not use the current election’s final sizes, turnout prior or pooled adjustment. A record still empty in the final feed contributes no observed later return to that identity; its actual venue count remains unknown. This distinction is essential: the analysis measures later feed additions, rather than claiming that the venue received no votes. Final CSV counts reconcile with retained final verbose feeds.

The following selected observations retain their actual source times; they do not establish exactly when each centre first reported. Coordinates come from retrospective final polling-place records, so the geographic screen is not a reconstruction of historical pre-election boundaries. Five unresolved 2022 final identities are excluded; none are excluded for that reason in 2019.

| Election | AEC source time | Hours after eastern poll close | Unreported centres | Later positive returns | Total ratio % | Mean centre ratio % |
| --- | --- | --- | --- | --- | --- | --- |
| 2019fed | 2019-05-19 12:02:55 | 18.0 | 73 | 34 | 79.9 | 74.9 |
| 2019fed | 2019-05-19 17:54:07 | 23.9 | 46 | 7 | 22.0 | 10.6 |
| 2019fed | 2019-05-19 23:16:01 | 29.3 | 42 | 3 | 4.9 | 1.7 |
| 2019fed | 2019-05-20 11:59:54 | 42.0 | 40 | 1 | 5.1 | 1.7 |
| 2019fed | 2019-05-20 18:03:21 | 48.1 | 40 | 1 | 5.1 | 1.7 |
| 2019fed | 2019-05-20 21:49:40 | 51.8 | 40 | 1 | 5.1 | 1.7 |
| 2019fed | 2019-05-21 21:49:43 | 75.8 | 40 | 1 | 5.1 | 1.7 |
| 2019fed | 2019-05-22 21:48:56 | 99.8 | 39 | 0 | 0.0 | 0.0 |
| 2022fed | 2022-05-22 12:00:27 | 18.0 | 179 | 157 | 101.6 | 99.0 |
| 2022fed | 2022-05-22 18:05:07 | 24.1 | 90 | 69 | 94.3 | 95.8 |
| 2022fed | 2022-05-22 21:51:29 | 27.9 | 76 | 55 | 90.8 | 82.4 |
| 2022fed | 2022-05-23 11:59:46 | 42.0 | 66 | 45 | 81.7 | 71.1 |
| 2022fed | 2022-05-23 17:54:34 | 47.9 | 48 | 27 | 76.1 | 73.7 |
| 2022fed | 2022-05-23 21:53:30 | 51.9 | 32 | 11 | 15.6 | 33.8 |
| 2022fed | 2022-05-24 21:55:50 | 75.9 | 26 | 5 | 1.3 | 11.9 |
| 2022fed | 2022-05-25 21:51:06 | 99.9 | 24 | 3 | 0.5 | 3.3 |

The elections differ substantially. In 2022, the Monday afternoon unreported group still subsequently returned roughly three quarters of its starting expectation, before falling sharply that evening. In 2019 the decline occurred much earlier. Two elections support a cautious delay rule, but do not establish a universal schedule or a calibrated probability distribution.

This replay retains full expected size through 48 hours after 18:00 on Saturday, then uses the factors below. Between these times the factor changes smoothly on a logarithmic scale; after 102 hours it stays at 0.02. These are conservative judgement choices informed by the historical ratios, rather than fitted parameters. The clock uses the embedded feed time and one eastern polling-close reference for the whole election.

The factor multiplies the odds of a centre’s expected formal votes as a share of enrolment. For a small share this is close to multiplying its count. District accounting then reconciles the result with other unfinished categories. Positive counted votes always replace this estimate; EAV receives no direct time adjustment. The positive final factor retains some allowance for a later return without declaring a centre closed. This simple response does not separately model the chance of a return and its size when it arrives.

| Hours after eastern poll close | Approximate time | Odds factor |
| --- | --- | --- |
| 48.0 | Monday 18:00 | 1.0 |
| 54.0 | Tuesday 00:00 | 0.25 |
| 78.0 | Wednesday 00:00 | 0.1 |
| 102.0 | Thursday 00:00 | 0.02 |

## What does balancing do?

**Coherent allocation** means that the count estimates agree with each other: counted votes are preserved, additions are nonnegative, booths and declarations sum to their groups, and groups sum to one district total. For example, a district expected to finish with 90,000 votes after 80,000 are counted must allocate exactly 10,000 additions among its unfinished units. Accounting consistency does not establish accuracy.

Each update starts again from the same pre-election outcomes and cumulative counts. Reported ordinary/PPVC booths are approximately complete; declaration counts are minimums. A bounded calculation in log odds, log(p / (1 − p)), moves impossible low outcomes above counted minimums. Unfinished booths start from their own expected sizes. Their combined amount is reserved smoothly within the district remainder. Declaration estimates divide what remains. Categories are sums of their units, rather than targets whose shortfalls must enter the last unreported booth.

The broad district-total rule in the initial comparisons does not lower the total because reported ordinary or PPVC counts fall below expectations. Declarations consequently absorb differences after the booth reservation. This is an untested assumption about compensation between vote categories. The early/postal evidence supplies starting estimates, rather than immutable category totals after counting begins.

Partial compensation applies only to unfinished, reliably matched non-EAV PPVCs. If other reliable completed centres in the same district run below expectations, an eligible unfinished centre can increase, and vice versa. The deviation is attenuated by the share of the other expected PPVC vote represented by those completed centres. Unmatched centres, EAV services and centres expected below 2,000 votes receive no such adjustment. They retain their starting size estimates, subject to the common district accounting constraint.

The coefficients are 0.14 for expected sizes 2,000–<4,000, 1.17 for 4,000–<8,000, and 0.63 for 8,000 or more. These are prediction slopes in transformed shares of enrolment, not correlations or fractions of a vote-count deficit. They use the conservative end nearest zero of 95% district-resampling intervals from reliable Federal 2025 matches, rounded towards zero. This election’s final results choose the fixed research coefficients: the replay is therefore an exploratory same-election check, not independent validation of this adjustment. Separate uncertainty in individual booth allocations has not yet been calibrated.

The Federal 2025 Rockingham PPVC and Rockingham Central PPVC identities are excluded from learning live booth-size trends because their returns were consolidated. Brand is also excluded from the paired compensation calibration: each centre’s comparison includes the other centres in that district. This exception does not delete reported votes, close either identity, or exclude Brand’s district total from scoring. It does not apply to earlier elections’ Rockingham records.

**Balanced** uses these size estimates and partial compensation. **Adapted** additionally learns one ordinary adjustment and one PPVC adjustment from reliable completed booths across the election. **Pooled** means the observations are combined rather than estimating a factor per district. Each observation is a log counted/expected ratio. Ordinary adjustment retains 8 equivalent unchanged booths. PPVC adjustment instead uses the observed vote count V: influence is V^0.9 / (H^0.9 + V^0.9), with H = 2,000,000 votes. H is the count at which half the raw deviation is applied; this is a cautious experimental strength rather than fitted precision. The average log ratio is multiplied by this influence before producing a factor. Only reliable non-EAV unfinished booths receive it, applied in log odds. A factor of 0.9 corresponds approximately to a 10% reduction for a small share. Counted votes and district totals remain unchanged; category totals can change.

The main Prototype columns use Balanced. Adapted is a diagnostic comparison: a reporting subset can give a misleading shared factor, so the extra adjustment is not part of the main prototype estimate.

In the diagnostic version, compensation is measured against reference sizes already adjusted for the pooled trend. This keeps a common election-wide decline separate from the remaining district deviation.

EAV is identified by its service role, independently of size. It neither trains nor receives public-centre trend or compensation adjustments. An unmatched EAV overrides the generic centre fallback. Its starting count is the median expectation among reliably matched historical EAV services, calculated separately in each prior outcome. With no historical EAV reference, 25 votes is an explicit service assumption. This uses historical prediction inputs, not this election’s final EAV counts. An already reported EAV always retains its actual count.

The Light feed has no authoritative first-preference finalisation flag used here. Declared-winner status and dates do not establish completion. Rare substantial booth revisions are exported separately. Matching identities does not establish comparability after boundary changes.

## Does the district-total estimate improve?

The following table compares predicted final formal totals with the reviewed final totals over all 150 districts. **Mean absolute error** is the average absolute difference in votes per district, using mean prototype predictions. **Actually remaining** is final votes minus votes counted nationally; it is retrospective scoring information. All rows use the same districts, so changes can be compared directly.

| AEC source time | Votes counted | Actually remaining | Baseline error | Prototype error |
| --- | --- | --- | --- | --- |
| 2025-05-03 17:17:46 | 0 | 15,490,236 | 9,564 | 2,334 |
| 2025-05-03 18:29:46 | 3,730 | 15,486,506 | 9,926 | 2,334 |
| 2025-05-03 19:59:47 | 2,560,760 | 12,929,476 | 11,295 | 2,334 |
| 2025-05-03 21:59:48 | 6,950,319 | 8,539,917 | 7,352 | 2,334 |
| 2025-05-04 01:01:19 | 11,625,499 | 3,864,737 | 3,610 | 2,333 |
| 2025-05-07 21:04:49 | 13,817,444 | 1,672,792 | 2,504 | 2,294 |
| 2025-05-14 21:04:43 | 15,056,116 | 434,120 | 2,086 | 1,487 |
| 2025-05-22 21:54:29 | 15,493,674 | -3,438 | 2,274 | 1,749 |

Actually remaining can be negative when a retained snapshot contains more votes than the subsequently revised final result. Counted votes are preserved in that snapshot; the prototype does not borrow a later downward correction to improve its score.

The missing previous district is not the main source of the difference. Restricting both methods to the same 149 districts with previous declaration counts changes the no-results comparison to 9,285 versus 2,336 votes of mean absolute error, and the latest comparison to 2,289 versus 1,749. Much of the district-total improvement exists before results arrive: it comes from the calibrated pre-election estimate, rather than learning a turnout change from reporting booths.

## Are the total and category intervals useful?

Intervals should be narrow enough to inform a forecast while still covering the final counts. A nominal 95% interval contains the middle 95% of modelled outcomes. **Coverage** is the percentage of final counts inside their intervals. **Width** is their average width in votes. **Interval score** is width plus 40 times the distance of a missed final count outside the interval; lower is better. The baseline has no corresponding count interval in this analysis.

District rows give equal weight to 150 final totals. Category rows give equal weight to 150 districts × 6 groups = 900 final counts. Category errors are therefore in votes per district-group, not percentage points or percentages of the national total. Repeated snapshots remain a single election and cannot establish general probability calibration. The extra pooled-adjustment error column checks whether the diagnostic Adapted version improves category counts on these same cases.

| Quantity | AEC source time | Baseline error | Prototype error | Pooled-adjustment error | Coverage % | Mean width | Interval score |
| --- | --- | --- | --- | --- | --- | --- | --- |
| District total | 2025-05-03 17:17:46 | 9,564 | 2,334 | — | 88.7 | 9,929 | 13,509 |
| District total | 2025-05-03 18:29:46 | 9,926 | 2,334 | — | 88.7 | 9,929 | 13,509 |
| District total | 2025-05-03 19:59:47 | 11,295 | 2,334 | — | 88.7 | 9,929 | 13,509 |
| District total | 2025-05-03 21:59:48 | 7,352 | 2,334 | — | 88.7 | 9,929 | 13,509 |
| District total | 2025-05-04 01:01:19 | 3,610 | 2,333 | — | 88.7 | 9,920 | 13,500 |
| District total | 2025-05-07 21:04:49 | 2,504 | 2,294 | — | 88.7 | 9,688 | 13,241 |
| District total | 2025-05-14 21:04:43 | 2,086 | 1,487 | — | 92.7 | 6,256 | 8,111 |
| District total | 2025-05-22 21:54:29 | 2,274 | 1,749 | — | 0.0 | 4,313 | 9,572 |
| Category count | 2025-05-03 17:17:46 | 2,976 | 1,476 | 1,476 | 86.1 | 9,960 | 11,426 |
| Category count | 2025-05-03 18:29:46 | 3,054 | 1,476 | 1,897 | 86.2 | 9,957 | 11,427 |
| Category count | 2025-05-03 19:59:47 | 2,922 | 1,394 | 1,379 | 92.6 | 9,069 | 10,633 |
| Category count | 2025-05-03 21:59:48 | 1,983 | 1,322 | 1,296 | 88.7 | 6,617 | 10,365 |
| Category count | 2025-05-04 01:01:19 | 1,043 | 975 | 956 | 65.2 | 3,065 | 11,959 |
| Category count | 2025-05-07 21:04:49 | 628 | 567 | 567 | 59.2 | 2,050 | 6,687 |
| Category count | 2025-05-14 21:04:43 | 484 | 323 | 323 | 59.2 | 1,430 | 2,447 |
| Category count | 2025-05-22 21:54:29 | 380 | 292 | 292 | 8.3 | 1,048 | 1,528 |

## Are still-unreported booth estimates better?

This isolates unfinished ordinary booths and PPVCs, comparing their predicted final counts with final AEC booth counts. Each booth has equal weight. District, booth ID, name and official category must match. Reported booths are excluded because the completion assumption gives them their counted total. The reviewed Rockingham consolidation identities are excluded because their individual final counts are not comparable. Entirely empty final records are excluded pending assessment, without closing their prediction identities. There are 27 such final records in the retained CSVs. A dash means no eligible unreported booth remains. “ordinary” in this booth-type table includes mobile services and Divisional Office/BLV booths that do not receive the PPVC size rule. It is not the six-group category label. All errors are mean absolute differences in votes per booth.

| AEC source time | Type | Booths | Baseline error | Balanced error | Adapted error |
| --- | --- | --- | --- | --- | --- |
| 2025-05-03 17:17:46 | ordinary | 7631 | 153 | 157 | 157 |
| 2025-05-03 17:17:46 | ppvc | 1281 | 1,954 | 1,426 | 1,426 |
| 2025-05-03 18:29:46 | ordinary | 7610 | 153 | 157 | 175 |
| 2025-05-03 18:29:46 | ppvc | 1279 | 1,980 | 1,428 | 1,426 |
| 2025-05-03 19:59:47 | ordinary | 4300 | 164 | 175 | 173 |
| 2025-05-03 19:59:47 | ppvc | 1183 | 2,108 | 1,475 | 1,469 |
| 2025-05-03 21:59:48 | ordinary | 1732 | 166 | 173 | 173 |
| 2025-05-03 21:59:48 | ppvc | 856 | 1,966 | 1,410 | 1,397 |
| 2025-05-04 01:01:19 | ordinary | 682 | 171 | 165 | 164 |
| 2025-05-04 01:01:19 | ppvc | 339 | 1,128 | 865 | 826 |
| 2025-05-07 21:04:49 | ordinary | 2 | 490 | 407 | 407 |
| 2025-05-07 21:04:49 | ppvc | 150 | 10 | 12 | 12 |
| 2025-05-14 21:04:43 | ordinary | 1 | 481 | 214 | 214 |
| 2025-05-14 21:04:43 | ppvc | 0 | — | — | — |
| 2025-05-22 21:54:29 | ordinary | 0 | — | — | — |
| 2025-05-22 21:54:29 | ppvc | 0 | — | — | — |

At the latest snapshot, open declaration categories still retain additions. District-total 95% coverage is 0.0%. Subsequent downward corrections can put final counts below the preserved counted minimum, but they do not explain all expected additions. These intervals retain pre-election uncertainty and counted bounds; they are not a calibrated model of declaration-counting completion.

## Could reporting order bias the shared factor?

Small and large booths can report at different times. Eligible booths are split at their type’s median frozen expected size. **Reported change** is the geometric mean counted/expected ratio among currently reported booths, expressed as a percentage change before shrinkage. **Final change** uses all positive, matching final counts in that size group and is never used for prediction. Differences expose a reporting subset that does not yet represent the full group.

| AEC source time | Type | Size | Reported / eligible | Reported change % | Final change % |
| --- | --- | --- | --- | --- | --- |
| 2025-05-03 17:17:46 | ordinary | smaller | 0 / 2946 | — | 10.5 |
| 2025-05-03 17:17:46 | ordinary | larger | 0 / 2946 | — | 0.6 |
| 2025-05-03 17:17:46 | ppvc | smaller | 0 / 414 | — | -12.7 |
| 2025-05-03 17:17:46 | ppvc | larger | 0 / 414 | — | -12.4 |
| 2025-05-03 18:29:46 | ordinary | smaller | 18 / 2946 | -11.6 | 10.5 |
| 2025-05-03 18:29:46 | ordinary | larger | 0 / 2946 | — | 0.6 |
| 2025-05-03 18:29:46 | ppvc | smaller | 2 / 414 | 16.7 | -12.7 |
| 2025-05-03 18:29:46 | ppvc | larger | 0 / 414 | — | -12.4 |
| 2025-05-03 19:59:47 | ordinary | smaller | 1866 / 2946 | 7.8 | 10.5 |
| 2025-05-03 19:59:47 | ordinary | larger | 1028 / 2946 | -2.5 | 0.6 |
| 2025-05-03 19:59:47 | ppvc | smaller | 42 / 414 | -5.6 | -12.7 |
| 2025-05-03 19:59:47 | ppvc | larger | 12 / 414 | -37.7 | -12.4 |
| 2025-05-03 21:59:48 | ordinary | smaller | 2639 / 2946 | 10.4 | 10.5 |
| 2025-05-03 21:59:48 | ordinary | larger | 2406 / 2946 | 0.8 | 0.6 |
| 2025-05-03 21:59:48 | ppvc | smaller | 109 / 414 | 5.4 | -12.7 |
| 2025-05-03 21:59:48 | ppvc | larger | 147 / 414 | -20.2 | -12.4 |
| 2025-05-04 01:01:19 | ordinary | smaller | 2918 / 2946 | 11.1 | 10.5 |
| 2025-05-04 01:01:19 | ordinary | larger | 2933 / 2946 | 1.1 | 0.6 |
| 2025-05-04 01:01:19 | ppvc | smaller | 216 / 414 | -2.0 | -12.7 |
| 2025-05-04 01:01:19 | ppvc | larger | 363 / 414 | -13.1 | -12.4 |
| 2025-05-07 21:04:49 | ordinary | smaller | 2946 / 2946 | 11.0 | 10.5 |
| 2025-05-07 21:04:49 | ordinary | larger | 2946 / 2946 | 0.9 | 0.6 |
| 2025-05-07 21:04:49 | ppvc | smaller | 263 / 414 | -4.2 | -12.7 |
| 2025-05-07 21:04:49 | ppvc | larger | 414 / 414 | -12.6 | -12.4 |
| 2025-05-14 21:04:43 | ordinary | smaller | 2946 / 2946 | 10.5 | 10.5 |
| 2025-05-14 21:04:43 | ordinary | larger | 2945 / 2946 | 0.6 | 0.6 |
| 2025-05-14 21:04:43 | ppvc | smaller | 412 / 414 | -13.1 | -12.7 |
| 2025-05-14 21:04:43 | ppvc | larger | 414 / 414 | -12.4 | -12.4 |
| 2025-05-22 21:54:29 | ordinary | smaller | 2946 / 2946 | 10.5 | 10.5 |
| 2025-05-22 21:54:29 | ordinary | larger | 2945 / 2946 | 0.6 | 0.6 |
| 2025-05-22 21:54:29 | ppvc | smaller | 412 / 414 | -12.7 | -12.7 |
| 2025-05-22 21:54:29 | ppvc | larger | 414 / 414 | -12.3 | -12.4 |

## Accounting, timing and reproduction

All counted candidate records are retained in the local export under their AEC candidate IDs. The prototype produces additional counts only; it does not invent projected party records. The largest category-to-total accounting difference was 1.7e-10 votes. Log-odds marginal conditioning followed by proportional reconciliation is an approximation, not an exact joint posterior. Updated turnout and formality rates, party-share sensitivities and new joint uncertainty calibration are not produced by this component.

The calculation took 41.20 seconds for 8 selected snapshots with 1,024 prior outcomes each, across all 150 districts. The slowest pair of Balanced/Adapted count updates took 3.018 seconds. These Python count timings exclude full party-share preparation and simulation iterations; they do not demonstrate full application timing.

From the repository’s `analysis` directory:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_federal_live --baseline-only
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_federal_live --dry-run --ppvc-starting-sizes median_by_type --pooled-half-votes 2000000 --compensation-order after_pool --ppvc-reporting-decay --reporting-history "..\docs\turnout-federal-live-prototype\ppvc-reporting-history.json"
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_federal_live --ppvc-starting-sizes median_by_type --pooled-half-votes 2000000 --compensation-order after_pool --ppvc-reporting-decay --reporting-history "..\docs\turnout-federal-live-prototype\ppvc-reporting-history.json"
```

`--baseline-only` writes `baseline.json` and needs no prior fixture or final scoring inputs. The full comparison writes `analysis.json` and this report. `--dry-run` writes nothing. Default snapshots come from Downloads and run through 22 May 2025, selecting eight points from no results through late counting. The previous verbose result XML and current polling-place preload XML are retained in `downloads`. Use `--sources`, `--previous` and `--preload` for explicit locations. `--through` selects a different final date. These large local inputs are not distributed in Git.

`--ppvc-starting-sizes legacy` reproduces the original allocation weights; `mean` and `median` select the historical local-centre assumption across all seat types. `mean_by_type` and `median_by_type` use separate seat-type references. `--ppvc-reporting-decay` enables the delayed-public-centre experiment; it is off by default. The default starting weights remain `legacy`. The commands above reproduce the settings used in this report. These alternatives require the retained 2019 verbose result feed and the 2022 polling-place CSV selected by `downloads/turnout/federal-prepoll/2022fed/latest.json`. `--pooled-half-votes` selects the diagnostic PPVC influence scale. For a controlled PPVC-only comparison, run `scripts.turnout.turnout_pooled_size_experiment` with `--ppvc-starting-sizes median --samples 1024`; its export contains formula, scale and compensation-order alternatives with ordinary adjustment held off throughout.

Historical reporting diagnostics can be reproduced with `scripts.turnout.turnout_ppvc_reporting --fetch`. This selects a small set of checkpoints from the [AEC media-feed archive](https://results.aec.gov.au/) and retains downloaded bytes by SHA256. Later runs reuse them; `--refresh` retrieves revisions without deleting older source bytes. The script also uses retained preloads and final feeds under `F:/Election Data/AEC media feed archive` and `downloads`, plus earlier official CSV revisions selected by `downloads/turnout/federal-prepoll`. Use `--archive` for a different local archive location. `--reporting-history` includes the resulting JSON in this report only, after checking its source hashes; it is not a prediction input.

The frozen fixture is `docs/turnout-prior-prototype/fixtures-v3.json`. Final scoring uses `analysis/Data/Turnout/2025fed.json`, the audited `FederalPrepoll/final.json` supplement and byte-addressed CSV downloads under `downloads/turnout/federal-prepoll/2025fed`. The export records embedded AEC timestamps, SHA256 hashes of input revisions and code, model versions, samples, seed and shrinkage strength. Changes to prior source measurements or its sampler require a refreshed fixture. Final scoring revisions must reconcile with the saved district and category results.

If GUI project settings give a district a different previous name or a `useFpResults` source, supply `--seat-aliases` with a JSON object mapping the current name to the relevant project fields. For example, `{"Current name": {"previousName": "Previous name"}}`. A `useFpResults` value supplies declaration counts but does not make booths from that district eligible for same-district adaptation. Matching district settings are needed to reproduce a particular GUI project’s baseline exactly; this run uses current district names only.

## How does counting progress change the remaining-vote estimate?

This analysis examines whether observed slowing of declaration counting improves estimates of votes still to arrive. It compares the broad prediction described above with an additional layer that gradually concentrates predictions near small additions while retaining a rare larger-batch possibility. Final results measure errors; they do not define completion evidence. The SA comparison additionally uses explicit retrospective category repairs, described below. Both predictions use the Balanced allocation, without a shared ordinary or PPVC trend multiplier. Federal starting sizes and the existing unreported-PPVC time adjustment are the same on both sides of this comparison.

Each prediction starts from the same frozen prior and current counted account. Recent absolute count changes include corrections and reversals, and their influence decays continuously with age. The amount of movement and how widely it occurs provide separate evidence. Missing measurements supply no evidence; long observation gaps reduce confidence. Known zero counts are included when assessing whether reporting has started. Federal state measurements blend with national measurements according to their available district count: a state with 20 measured districts receives equal state and national influence. For an unstarted local category, this layer supplies no category-specific slowing-count evidence. Evidence from the rest of the district can nevertheless support a shared possibility of no further votes.

Shared measurements describe typical district progress rather than every exceptional source record. Before averaging each measurement, the model removes one observation at each end per ten measured districts, up to two per end. Pools of fewer than ten districts retain every observation. Activity, reporting-start support and relative movement are trimmed separately. Relative movement is the recent age-weighted absolute count change divided by that district's current category count plus half a vote; its shared average gives districts equal weight. Local counts and local activity are never trimmed. This limits errors in a few records from determining expectations across an election, without recognising or correcting their cause.

**Evidence strength** is a score between zero and one describing support for slowing counts. It combines recent observation coverage, local activity, activity across districts and whether reporting has started. It is not a probability that counting is complete. Activity responds smoothly to the size of each change; a ten-vote scale combined with 0.1% of the current count sets its scale rather than a pass/fail tolerance. Its memory fades exponentially. Regular observations build support, while a long gap supplies less support than daily returns.

The evidence changes a mixture of three distributions: the broad previous-expectations prediction, very small additions, and a wider late batch. Its weights vary continuously; there is no cutoff on either the evidence score or elapsed time. Both added distributions have overlapping continuous support. Counted votes remain fixed, and a reported ordinary/pre-poll booth remains complete. Within the branch with further votes, unfinished booth estimates are held at their broad values. Declarations and unused electorate capacity share a bounded transform, so reducing declarations can reduce the formal total instead of forcing all those votes into another category.

Here, **unused capacity** means enrolment minus the broad predicted formal total; it includes informal votes and people who do not vote. For each preparation outcome, declaration additions are expressed relative to that capacity in log odds. Log odds means log(p / (1 − p)) for a share p. The small-addition and late-batch components vary on that scale, and their joint allocation stays inside the available pool. There is no cap or accumulated probability at the enrolment bound. Positive additions are not rounded down to zero.

**No further votes** is a separate, shared outcome for the whole district. It keeps every current count exactly unchanged, including in tiny or unstarted categories. Its probability grows continuously with count-weighted slowing evidence from started declaration categories. The broad expected remainder weakens that probability: multiply by the square of counted declaration votes divided by counted declaration votes plus the broad expected remainder. Unreported booths therefore also weaken it. A supplied postal receipt window discounts the whole-district probability as well as postal-specific slowing evidence.

The current exploratory maximum fraction is 95%, further discounted by the product of each category's probability of no exceptional batch. Probability is transferred from the broad and small-addition branches; each category's original unconditional late-batch probability is preserved. These settings have not been fitted as completion probabilities. Explicit authoritative finalisation still makes no further votes certain. Ordinary evidence never permanently closes a category.

The exact counted outcome is retained analytically alongside samples of the branch with further votes. Means and intervals include both branches: for example, a 60% probability of no further votes and a conditional mean of 100 additional votes gives an overall mean of 40. This avoids changes driven by whether a small preparation sample happened to select a zero outcome, and makes the branch available for separate party-composition evaluation.

**These are exploratory shared parameters, rather than fitted district parameters.** The late-batch probability within the slowing-count distribution is 2%; the broad distribution remains an additional source of substantial additions until the evidence weakens its weight. The near-zero component starts from a 0.25 vote equivalent. Counting-activity memory is 24 hours. These assumptions have not been established as optimal or calibrated for every jurisdiction. The conditional batch starts from the broad remaining estimate plus 5% of the category's counted votes, with the small count equivalent also included. Its uncertainty is deliberately much wider than that of small additions.

| Setting | Value | Purpose |
|---|---:|---|
| Activity memory | 24 hours | Old count changes gradually stop arguing against finalisation. |
| Observation coverage memory | 48 hours | Discount stale evidence and gaps between available sources. |
| Shared activity scale | 0.1 | The shared quietness factor halves at this mean activity strength. |
| Shared movement scale | 0.5% | The movement factor halves when the trimmed mean district-relative movement reaches this value. |
| Unstarted-reporting scale | 10% | The starting-support factor halves when this fraction of shared starting evidence is absent. |
| Evidence response scale | 0.3 | Sets how gradually the broad prediction loses weight. |
| Maximum no-addition fraction | 95% | Sets the shared finished-count branch before evidence, remaining-count and receipt-window discounts. |
| Small / batch spread in log odds | 1 / 1.5 | Separate standard deviations retain a concentrated small addition and a wide batch. |

If e is evidence strength, the fraction assigned to the two slowing-count components is 1 − exp(−(e / 0.3)²). With the current settings, evidence of 0.25 replaces roughly half the broad prediction; evidence of 0.5 replaces about 94%. The remaining fraction keeps its broad distribution. This response was made conservative after observing postal-count pauses; it is a modelling assumption, not a fitted completion probability.

Postal counting has a different reason for pauses: votes can still be arriving from voters. The [AEC timetable](https://www.aec.gov.au/Elections/federal_elections/2025/timetable.htm) sets the 2025 receipt deadline at **6 pm on 16 May**, thirteen days after polling day. Votes must have been cast before polling closed. A quiet local postal count before this date provides weaker completion evidence than a quiet category whose ballots have already been cast and are awaiting internal transport or scrutiny.

For Postal only, multiply the ordinary slowing-evidence score by **1 / (1 + exp(−t / h))**, where t is the number of hours after the published receipt deadline and h is 48 hours. The factor is half at the deadline, smaller beforehand and gradually approaches one afterwards. This preserves more of the original remaining-vote prediction during pauses. The deadline never closes counting: current activity still weakens completion evidence, and the broad and late-batch possibilities remain. The transition scale is an exploratory assumption, not a calibrated arrival curve. Other jurisdictions receive no federal deadline unless their own date is explicitly supplied.

### Do the paired remaining-vote predictions improve?

Each row compares the same districts against the same final formal totals. **Broad MAE** and **Progress MAE** are mean absolute errors in votes for the broad and progress-aware predictions. **Mean additions** is the progress-aware expected additional vote per district. **95% width** is the average width of its 95% prediction interval. **Interval score** is that width plus forty times any distance by which the final result lies outside the interval; lower is better. It penalizes premature confidence as well as unnecessary uncertainty. Broad samples are repeated onto the same count-draw grid before scoring, so repetition alone cannot change the paired interval endpoints. Final rechecks can leave the reviewed final total below the counted account, which this count-preserving prototype cannot predict. The explicit no-addition branch permits an interval to include the exact current total. These intervals are not established as calibrated 95% probabilities.

| Source update | Broad MAE | Progress MAE | Mean additions | 95% width | Interval score: broad → progress |
|---|---:|---:|---:|---:|---:|
| 2025-05-03 17:17:46 | 2,345 | 2,345 | 102,002 | 10,182 | 13,967 → 13,967 |
| 2025-05-04 01:01:19 | 2,344 | 2,344 | 24,501 | 10,170 | 13,954 → 13,954 |
| 2025-05-11 21:50:01 | 2,192 | 2,193 | 7,102 | 9,255 | 12,826 → 12,826 |
| 2025-05-14 21:49:58 | 1,480 | 1,480 | 3,139 | 6,296 | 8,182 → 8,182 |
| 2025-05-17 21:49:26 | 1,496 | 1,418 | 1,906 | 4,767 | 6,301 → 5,811 |
| 2025-05-18 21:49:38 | 1,496 | 871 | 1,279 | 4,328 | 6,301 → 4,439 |
| 2025-05-19 21:48:49 | 1,634 | 1,458 | 1,673 | 4,484 | 7,064 → 5,975 |
| 2025-05-20 21:47:56 | 1,755 | 1,693 | 1,700 | 4,296 | 9,487 → 8,914 |
| 2025-05-22 21:54:29 | 1,771 | 965 | 942 | 3,474 | 9,438 → 4,478 |
| 2025-05-23 21:02:58 | 1,768 | 255 | 238 | 1,846 | 9,227 → 2,584 |
| 2025-05-31 21:05:45 | 1,757 | 126 | 126 | 1,204 | 8,596 → 1,212 |

At the last retained source, district-total error falls from 1,757 to 126 votes. These are repeated observations of one election; they do not establish performance in another jurisdiction.

Category errors check whether a smaller total is also allocated more accurately, rather than merely moving the error elsewhere. Each observation is one district/group with reviewed final data. SA uses the three supported early/postal/remaining groups and excludes unreliable category partitions; federal uses six groups. These comparison units differ from the district-total table. The following averages use the last retained source, with equal weight per known district/group.

| Known district/group observations | Broad category MAE | Progress category MAE | Interval score: broad → progress |
|---:|---:|---:|---:|
| 900 | 293 | 21 | 1,383 → 34 |

At the 11 May postal pause, district-total error changes from 2,192 to 2,193 votes. Some postal estimates fall before counting resumes. The receipt-window discount preserves more of the broad prediction during such pauses; counting can continue after the last receipt date.

Late residual totals also include unfinished ordinary-booth identities, whose expectations this layer does not change.


### Preparation, continuity and reproduction

The comparison uses 256 frozen preparation outcomes for the whole election and 8 count draws per outcome, producing 2048 updated count outcomes per snapshot. These additional draws reuse the preparation inputs; they do not represent extra independent party-composition samples. Both conditional distributions are also evaluated explicitly, so a rare batch remains represented without requiring a random branch selection. The core also returns the per-preparation reference, small-addition and batch parameters separately, alongside the joint sampled category/total outcomes. They are not reduced to a single standard deviation.

For Adelaide, the continuity check sweeps evidence strength from zero to one and perturbs each point by 1e-06. The largest change in the mean or 95% interval endpoints was 0.233920 votes. This is a numerical continuity check, not evidence that the chosen response speed is optimal. The largest category-to-total accounting discrepancy across snapshots was 1.6e-10 votes.

The JSON also includes a mechanical first-preference illustration using the currently observed candidate mix within each source category. Districts with unsupported outstanding-category mixes are omitted. It does not run the C++ party forecast, estimate new late-voter preferences, or calculate winners.

First reproduce the base count replay described above to create its local `analysis.json`. That file supplies the frozen no-results allocation and metadata paths. The progress comparison also requires the retained daily source snapshots; these large inputs are not distributed in Git. Then run from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_late_batch --elections 2025fed
```

Use `--dry-run` to calculate without writing outputs. `--samples` controls preparation outcomes and `--count-draws` controls their additional count evaluations. Retained feed locations can be supplied with `--archive` and `--downloads`. `--postal-transition-hours` changes the smooth postal deadline response scale for the federal experiment. The command writes `late-batch-analysis.json` beside this report and updates this section. Its provenance records the frozen origin, fixture, current code and exact source revisions. Python replay timings include this count calculation; they do not establish whole-application C++ runtime.

Reusing the last broad preparation, one count draw per preparation took 0.590 seconds; 8 draws took 4.070 seconds for the whole election. This times count evaluations only. It includes no additional party-composition preparation, feed parsing or main simulation iterations.

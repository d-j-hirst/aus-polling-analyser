# Initial turnout and vote-category distributions

This report examines how reliably initial estimates can predict the final number of valid votes, both overall and by voting method. It helps readers understand the uncertainty in these estimates before counted election results are used.

The analysis compares predicted ranges with final election results, calculates summaries for reduced-sample preparation, and provides reproducible numerical examples. It describes an offline prototype; the live forecast is unchanged.

Generated 2026-10-03T00:28:19.039019+00:00. Model proportional-prior-6.

## How the starting estimates are constructed

A formal vote is a valid lower-house first-preference ballot. Turnout is ballots as a percentage of enrolment; formality is formal votes as a percentage of ballots. Each district starts with an estimated final formal-vote total: current enrolment × expected turnout × expected formality, with percentages converted to fractions.

Starting formality retains the previous election’s rate. For turnout, the analysis compares retaining the previous rate, adding half the average historical change, and adding the full average historical change. These changes are calculated and applied in log odds using the permitted training elections. Tables using half the historical change use the middle choice, not half the turnout rate.

**Log odds** means log(p / (1 - p)), where p is the rate as a fraction and log is the natural logarithm. The inverse is p = 1 / (1 + exp(-z)). Fitting changes and uncertainty on this scale makes movements taper near 0% and 100%, rather than accumulating simulated results at a cap. For example, doubling the odds takes 95% formality to about 97.44%, and 99% to about 99.50%. Both leave informal votes. A starting turnout of 90% with an average log-odds change of -0.2 gives approximately 90%, 89.06% and 88.05% for the three turnout choices. The starting rate is the centre on the transformed scale, not a guaranteed mean percentage after uncertainty is drawn. The project’s existing vote-share transform uses these same odds multiplied by 25; this component uses unscaled natural log odds consistently in fitting, drawing and sensitivities.

A **control** here is an estimated final formal-vote count for a category or a published group of categories, derived from an independently published early-voting or postal count. The published figure may count votes cast, postal applications, ballots issued or ballots returned. A conversion multiplier learned from other elections translates that figure into expected final formal votes. For illustration, 20,000 postal applications and a multiplier of 0.80 give a postal control of 16,000 formal votes. The control is an uncertain estimate, not a guaranteed final count.

Controls anchor the early/postal category estimates. The remaining formal votes are divided among categories without a control using compatible previous proportions. In this prototype, controls change the division between categories rather than the district’s estimated overall total. Where federal early results distinguish ordinary and declaration pre-polls, the declaration expectation retains its previous percentage of all formal votes, and ordinary pre-polls receive the balance of the combined early estimate. This avoids interpreting known growth in ordinary pre-polls as equivalent growth in declaration pre-polls.

The detailed federal split is used only when the previous election is 2010 or later. Older comparisons retain combined early votes because the ordinary/declaration processing basis changed in 2010. A detailed starting declaration estimate must fit inside its combined early pool; an incompatible starting estimate is rejected. Where ordinary early voting is very small, the declaration-rate rule has limited room to allocate the balance and its suitability needs a separate assessment.

## Main findings

The following comparison uses elections with usable controls and compatible historical category baselines. SA 2026 receives a separate broad-category diagnostic. Category interval score charges for both breadth and missed results; lower is better. Scores are expressed per 1,000 enrolled electors. Target coverage is the intended percentage of final results inside an interval; observed coverage is the percentage actually inside it in these prediction comparisons. Values use half the average historical turnout change in log odds.

| Test method | Target coverage % | Previous-share interval score / 1,000 | Controlled interval score / 1,000 | Controlled coverage % |
| --- | --- | --- | --- | --- |
| Other elections | 80.000 | 813.293 | 417.233 | 83.040 |
| Earlier elections only | 80.000 | 861.294 | 472.457 | 85.310 |
| Other elections | 95.000 | 1121.324 | 628.158 | 92.507 |
| Earlier elections only | 95.000 | 1178.112 | 693.735 | 93.058 |

Each comparison contains 13 independently tested elections.

Controls improve category interval scores in both training methods at both interval levels. This supports the allocation rule together with uncertainty, rather than requiring wider ranges to compensate for carrying the previous voting mix forward.

Total distributions are identical for the two allocation rules within each interface: controls redistribute the estimated formal-vote total and do not update its total in this prior prototype. Any difference in total prediction comes from the rate assumption, not the addition of category controls. The interfaces share the same count calculation, so interval tables display it once.

Coverage differs across quantities and test elections. Some 95% comparisons under-cover, especially in the other-election tests. The parameters are usable prototype candidates, not independently established probability guarantees. Agreement between the count interfaces does not validate the shared distribution or its interval coverage.

The individual-election tables below identify category and district coverage limitations that an overall average can hide. An election total inside its range does not establish reliable district or category probabilities.

## What is being estimated?

A prior here means the initial expectation when live forecasting begins. It can already incorporate published early-voting and postal counts, but does not use counted election results.

The [category analysis](../turnout-category-dynamics/report.md) establishes the proportional rule. Converted early/postal estimates receive their allocated counts; categories without a control divide the remaining formal votes according to compatible previous proportions. The baseline carries all previous category shares forward. Both receive training-based uncertainty.

One simulated outcome is a set of possible final totals and category counts for all districts in an election together. Every simulated district has nonnegative category counts summing to its formal total. State and election totals are sums of districts. Displayed average counts therefore add up; separate category medians or interval endpoints generally do not. A category group means an observed historical grouping, not an inferred split of a combined publication.

## Where uncertainty comes from

This identifies why a plausible final result can differ from the initial estimate. Each component has election-wide variation shared across districts and local variation. Historical parameters are pooled; no district coefficients are fitted.

| Source | Meaning | Separate response |
| --- | --- | --- |
| Turnout | A change in ballot participation relative to the previous election | Votes per change in turnout log odds |
| Formality | A change in the fraction of ballots that are formal | Votes per change in formality log odds |
| Early conversion | Published early counts differ from final matched formal votes | Votes per change in the early pool |
| Postal conversion | Applications/issues/returns differ from final formal postals | Votes per change in the postal pool |
| Composition | Previous proportions imperfectly divide a supplied pool | Votes per common or local composition driver |

Turnout and formality retain their training-estimated pairing. Conversion and composition drivers are independent of those rate drivers and of each other in this prototype; this is a simplifying assumption, not evidence of independence. Conservation still creates related category errors. Federal state variation is included in the local historical rate scale but is not a separate shared state factor. Federal state interval results below expose the consequences.

Composition uncertainty is estimated after supplying actual totals and controlled pool sizes in training elections only. This separates it from conversion/total error and avoids adding the full category-error matrix on top of those errors. Shared composition second moments include historical bias; the point allocation itself receives no new composition correction.

Federal early controls are national. Local resident-allocation errors are learned from previous resident category counts, then centred so they redistribute the drawn national request rather than create additional national early votes. The smooth reconciliation with district totals described below can subsequently change that requested amount. Issuing-centre district counts remain excluded as resident controls.

Two historical comparisons are diagnostic only: category-share changes versus turnout, and federal issuing-centre district early counts versus resident-electorate final counts. The former use different denominators (formal votes and enrolment); the latter count different district populations. Neither supplies a prediction coefficient or district uncertainty parameter here. Turnout/formality errors are estimated separately, and federal early district uncertainty comes from previous resident results.

## How the preparation summaries work

The purpose is to summarize district variation once and retain distinct shared responses, so richer party-share preparation need not be repeated in every main simulation iteration. Both count interfaces retain transformed rates, their turnout × formality product and transformed category proportions. These count operations are inexpensive. The saved preparation export uses a smaller local sample and separate shared sensitivities; it is not a complete live simulation.

The exported sensitivities describe changes in category counts. They are distinct from the current live simulator’s sensitivity to changes in a category’s party shares. Mapping them into FP/TPP/TCP share responses requires the counted snapshot and its category party projections; those are not evaluated here.

A separate preparation calculation uses 288 possible count outcomes for one election to summarize variation specific to each district. Election-wide uncertainty is held fixed during these draws so it can be represented separately. The saved summaries are the average category counts and RMS variation about the central estimates: the square root of the average squared difference, which includes any shift in the average. This avoids recording the same election-wide error as both independent district noise and a shared effect. The timing section explains how this count-only calculation relates to the live simulator’s preparation.

## Independent prediction tests and interpretation

The question is whether the ranges work for an election that did not supply their parameters. “Other elections” excludes the test election; rate and category-change training also excludes successor transitions containing it. “Earlier elections only” additionally restricts training to earlier completed elections. Neither method is assumed to be the definitive verdict.

Every conversion and uncertainty scale is rebuilt inside the permitted training set. Conversion error uses internal predictions of training elections. Exported full-evaluation uncertainty allowances are not reused. Actual held-election votes are kept separate from the numerical prediction interface and used only for scoring.

An 80% interval is the middle 80% of simulated outcomes; 95% gives a wider tail check. Coverage is the fraction of results inside the range. Width measures its breadth. Interval score is width plus 2/(1 − target coverage) times the distance of an actual result outside the interval. Lower is better: enormous intervals and narrow missed intervals both cost more.

Width and score use votes per 1,000 enrolled electors. District counts are summed within each election before normalization. Category width/score sums across all categories and is not the width of an interval for the total. Coverage weights districts by enrolment, with equal weight across their reported categories; elections are then averaged equally. Thousands of districts are not thousands of independent election-wide tests.

Final enrolment is used as the known exposure. Reconciled historical operational series remain labelled in the parameters; historical publication availability is not fully verified. Previous district rates and proportions use matched names, without a boundary adjustment for redistributions. District names without local rate history use previous parent rates and extra local uncertainty. These are prior-transfer tests, not historical live replays.

## Main comparisons

These tables compare the same elections for each quantity. Starting totals using half the average historical turnout change are shown first; the unchanged-turnout and full-change alternatives follow. The SA 2026 category comparison is separate because its earlier detailed baseline does not exist.

### Elections with usable controls

| Quantity | Test method | Allocation | Target coverage % | Elections | Observed coverage % | Width / 1,000 electors | Interval score / 1,000 electors |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Category counts | Earlier elections only | Controls + proportional remainder | 80.000 | 13 | 85.310 | 383.680 | 472.457 |
| Category counts | Earlier elections only | Previous category shares | 80.000 | 13 | 81.962 | 638.843 | 861.294 |
| Category counts | Other elections | Controls + proportional remainder | 80.000 | 13 | 83.040 | 310.303 | 417.233 |
| Category counts | Other elections | Previous category shares | 80.000 | 13 | 80.387 | 573.684 | 813.293 |
| Category counts | Earlier elections only | Controls + proportional remainder | 95.000 | 13 | 93.058 | 596.519 | 693.735 |
| Category counts | Earlier elections only | Previous category shares | 95.000 | 13 | 91.711 | 974.506 | 1178.112 |
| Category counts | Other elections | Controls + proportional remainder | 95.000 | 13 | 92.507 | 498.343 | 628.158 |
| Category counts | Other elections | Previous category shares | 95.000 | 13 | 91.202 | 879.580 | 1121.324 |
| District formal totals | Earlier elections only | Controls + proportional remainder | 80.000 | 14 | 74.838 | 53.273 | 81.991 |
| District formal totals | Earlier elections only | Previous category shares | 80.000 | 14 | 74.838 | 53.273 | 81.991 |
| District formal totals | Other elections | Controls + proportional remainder | 80.000 | 14 | 72.893 | 51.337 | 81.655 |
| District formal totals | Other elections | Previous category shares | 80.000 | 14 | 72.893 | 51.337 | 81.655 |
| District formal totals | Earlier elections only | Controls + proportional remainder | 95.000 | 14 | 92.253 | 81.519 | 111.217 |
| District formal totals | Earlier elections only | Previous category shares | 95.000 | 14 | 92.253 | 81.519 | 111.217 |
| District formal totals | Other elections | Controls + proportional remainder | 95.000 | 14 | 90.232 | 78.741 | 113.363 |
| District formal totals | Other elections | Previous category shares | 95.000 | 14 | 90.232 | 78.741 | 113.363 |
| Election formal total | Earlier elections only | Controls + proportional remainder | 80.000 | 14 | 71.429 | 42.818 | 63.885 |
| Election formal total | Earlier elections only | Previous category shares | 80.000 | 14 | 71.429 | 42.818 | 63.885 |
| Election formal total | Other elections | Controls + proportional remainder | 80.000 | 14 | 71.429 | 38.220 | 69.016 |
| Election formal total | Other elections | Previous category shares | 80.000 | 14 | 71.429 | 38.220 | 69.016 |
| Election formal total | Earlier elections only | Controls + proportional remainder | 95.000 | 14 | 85.714 | 64.942 | 71.508 |
| Election formal total | Earlier elections only | Previous category shares | 95.000 | 14 | 85.714 | 64.942 | 71.508 |
| Election formal total | Other elections | Controls + proportional remainder | 95.000 | 14 | 78.571 | 59.043 | 98.442 |
| Election formal total | Other elections | Previous category shares | 95.000 | 14 | 78.571 | 59.043 | 98.442 |
| Federal state formal totals | Earlier elections only | Controls + proportional remainder | 80.000 | 7 | 68.905 | 38.980 | 64.843 |
| Federal state formal totals | Earlier elections only | Previous category shares | 80.000 | 7 | 68.905 | 38.980 | 64.843 |
| Federal state formal totals | Other elections | Controls + proportional remainder | 80.000 | 7 | 63.136 | 34.073 | 71.537 |
| Federal state formal totals | Other elections | Previous category shares | 80.000 | 7 | 63.136 | 34.073 | 71.537 |
| Federal state formal totals | Earlier elections only | Controls + proportional remainder | 95.000 | 7 | 83.015 | 58.281 | 78.745 |
| Federal state formal totals | Earlier elections only | Previous category shares | 95.000 | 7 | 83.015 | 58.281 | 78.745 |
| Federal state formal totals | Other elections | Controls + proportional remainder | 95.000 | 7 | 72.993 | 52.669 | 114.407 |
| Federal state formal totals | Other elections | Previous category shares | 95.000 | 7 | 72.993 | 52.669 | 114.407 |

### All retained elections

| Quantity | Test method | Allocation | Target coverage % | Elections | Observed coverage % | Width / 1,000 electors | Interval score / 1,000 electors |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Category counts | Earlier elections only | Controls + proportional remainder | 80.000 | 24 | 85.838 | 423.342 | 522.203 |
| Category counts | Earlier elections only | Previous category shares | 80.000 | 24 | 84.025 | 561.555 | 732.823 |
| Category counts | Other elections | Controls + proportional remainder | 80.000 | 24 | 85.178 | 397.699 | 498.726 |
| Category counts | Other elections | Previous category shares | 80.000 | 24 | 83.741 | 540.364 | 713.258 |
| Category counts | Earlier elections only | Controls + proportional remainder | 95.000 | 24 | 93.489 | 658.743 | 774.664 |
| Category counts | Earlier elections only | Previous category shares | 95.000 | 24 | 92.759 | 863.486 | 1037.035 |
| Category counts | Other elections | Controls + proportional remainder | 95.000 | 24 | 93.391 | 635.304 | 756.128 |
| Category counts | Other elections | Previous category shares | 95.000 | 24 | 92.684 | 841.808 | 1023.260 |
| District formal totals | Earlier elections only | Controls + proportional remainder | 80.000 | 25 | 79.748 | 54.664 | 79.989 |
| District formal totals | Earlier elections only | Previous category shares | 80.000 | 25 | 79.748 | 54.664 | 79.989 |
| District formal totals | Other elections | Controls + proportional remainder | 80.000 | 25 | 77.212 | 50.134 | 77.843 |
| District formal totals | Other elections | Previous category shares | 80.000 | 25 | 77.212 | 50.134 | 77.843 |
| District formal totals | Earlier elections only | Controls + proportional remainder | 95.000 | 25 | 93.638 | 83.937 | 111.262 |
| District formal totals | Earlier elections only | Previous category shares | 95.000 | 25 | 93.638 | 83.937 | 111.262 |
| District formal totals | Other elections | Controls + proportional remainder | 95.000 | 25 | 92.098 | 76.785 | 108.052 |
| District formal totals | Other elections | Previous category shares | 95.000 | 25 | 92.098 | 76.785 | 108.052 |
| Election formal total | Earlier elections only | Controls + proportional remainder | 80.000 | 25 | 80.000 | 45.535 | 64.233 |
| Election formal total | Earlier elections only | Previous category shares | 80.000 | 25 | 80.000 | 45.535 | 64.233 |
| Election formal total | Other elections | Controls + proportional remainder | 80.000 | 25 | 76.000 | 38.088 | 63.501 |
| Election formal total | Other elections | Previous category shares | 80.000 | 25 | 76.000 | 38.088 | 63.501 |
| Election formal total | Earlier elections only | Controls + proportional remainder | 95.000 | 25 | 88.000 | 69.551 | 81.333 |
| Election formal total | Earlier elections only | Previous category shares | 95.000 | 25 | 88.000 | 69.551 | 81.333 |
| Election formal total | Other elections | Controls + proportional remainder | 95.000 | 25 | 84.000 | 58.584 | 85.984 |
| Election formal total | Other elections | Previous category shares | 95.000 | 25 | 84.000 | 58.584 | 85.984 |
| Federal state formal totals | Earlier elections only | Controls + proportional remainder | 80.000 | 7 | 68.905 | 38.980 | 64.843 |
| Federal state formal totals | Earlier elections only | Previous category shares | 80.000 | 7 | 68.905 | 38.980 | 64.843 |
| Federal state formal totals | Other elections | Controls + proportional remainder | 80.000 | 7 | 63.136 | 34.073 | 71.537 |
| Federal state formal totals | Other elections | Previous category shares | 80.000 | 7 | 63.136 | 34.073 | 71.537 |
| Federal state formal totals | Earlier elections only | Controls + proportional remainder | 95.000 | 7 | 83.015 | 58.281 | 78.745 |
| Federal state formal totals | Earlier elections only | Previous category shares | 95.000 | 7 | 83.015 | 58.281 | 78.745 |
| Federal state formal totals | Other elections | Controls + proportional remainder | 95.000 | 7 | 72.993 | 52.669 | 114.407 |
| Federal state formal totals | Other elections | Previous category shares | 95.000 | 7 | 72.993 | 52.669 | 114.407 |

## Sensitivity to the turnout assumption

This checks whether interval performance changes when the previous turnout rate is left unchanged, or adjusted by half or all of the historical average change described above. All three choices are fixed beforehand. The table uses the controlled shared count calculation and election totals, keeping the allocation rule constant.

| Test method | Turnout assumption | Target coverage % | Elections | Observed coverage % | Interval score / 1,000 electors |
| --- | --- | --- | --- | --- | --- |
| Earlier elections only | Full average historical log-odds turnout change | 80.000 | 25 | 80.000 | 64.820 |
| Earlier elections only | Full average historical log-odds turnout change | 95.000 | 25 | 96.000 | 76.039 |
| Earlier elections only | Half the average historical log-odds turnout change | 80.000 | 25 | 80.000 | 64.233 |
| Earlier elections only | Half the average historical log-odds turnout change | 95.000 | 25 | 88.000 | 81.333 |
| Earlier elections only | Previous turnout | 80.000 | 25 | 80.000 | 64.434 |
| Earlier elections only | Previous turnout | 95.000 | 25 | 88.000 | 94.335 |
| Other elections | Full average historical log-odds turnout change | 80.000 | 25 | 76.000 | 61.716 |
| Other elections | Full average historical log-odds turnout change | 95.000 | 25 | 88.000 | 76.280 |
| Other elections | Half the average historical log-odds turnout change | 80.000 | 25 | 76.000 | 63.501 |
| Other elections | Half the average historical log-odds turnout change | 95.000 | 25 | 84.000 | 85.984 |
| Other elections | Previous turnout | 80.000 | 25 | 80.000 | 66.566 |
| Other elections | Previous turnout | 95.000 | 25 | 84.000 | 92.041 |

## Federal declaration pre-poll predictions

This checks the declaration expectation across the federal elections with observed ordinary/declaration pre-poll splits and a usable combined early control. Comparisons without that control are absent from this particular check. Each row uses controls and half the average turnout change in log odds. The average national declaration count is compared with the final count, while district 95% coverage tests the range at district level. A close national count can conceal district errors. Interval score charges for both width and misses and is expressed per 1,000 electors. Earlier tests with little comparable training still use the labelled default widths; this table does not assume that all ranges are calibrated.

| Election | Test method | Composition training elections | Actual national formal declarations | Mean predicted national declarations | District 95% coverage % | Interval score / 1,000 electors |
| --- | --- | --- | --- | --- | --- | --- |
| 2013fed | Other elections | 3 | 488338.000 | 495093.265 | 93.922 | 37.934 |
| 2016fed | Other elections | 3 | 491244.000 | 522894.145 | 96.590 | 32.881 |
| 2016fed | Earlier elections only | 1 | 491244.000 | 579643.876 | 100.000 | 74.554 |
| 2019fed | Other elections | 3 | 593878.000 | 517018.545 | 89.404 | 57.797 |
| 2019fed | Earlier elections only | 2 | 593878.000 | 517061.372 | 90.062 | 57.700 |
| 2022fed | Other elections | 3 | 530718.000 | 633097.656 | 90.726 | 46.717 |
| 2022fed | Earlier elections only | 3 | 530718.000 | 632250.877 | 90.103 | 47.180 |
| 2025fed | Other elections | 4 | 532449.000 | 566021.600 | 95.922 | 35.330 |
| 2025fed | Earlier elections only | 4 | 532449.000 | 566445.969 | 96.555 | 34.749 |

## Reading zero and missing counts

A published zero establishes the observed count, not whether that outcome was possible. This distinction matters because a zero starting weight excludes a category from every simulated outcome. Historical counts, category definitions and service availability answer different questions; the normalized data preserve the counts without assigning every zero an availability explanation.

| Observation | Meaning for interpretation |
| --- | --- |
| Known unavailable category or service | A structural zero, supported by evidence about that election. Availability can change at the next election. |
| Available category with no observed votes | A possible small-count outcome; zero does not establish permanent absence. |
| Empty batch, venue or separate reporting row | Describes that row only. Other rows can contain votes in the same category. |
| Missing count or category reported inside a combined total | The separate count is unknown. Null or omission must not be interpreted as zero. |
| Reviewed implausible reporting zero | The analytical count is unknown. A potentially misclassified district split cannot train or score allocation. |
| Zero in a dated cumulative series | The reported amount at that date; it does not establish a zero final count. |

Category aggregation occurs before this prototype constructs weights: empty source rows are added to nonempty rows in the same district and category. Groups still empty after aggregation are retained in the local output for individual review, with their source labels. Unknown counts remain null or omitted; an entirely empty group does not supply an invented equal division.

Possible zeros in an available category receive a half-vote equivalent before forming logarithmic category weights. The transformed input floor is the smaller of 0.1% and half a vote divided by the observed parent count. This preserves genuine positive shares below 0.1% in large groups. The floor applies to starting observations, not to simulated outcomes; uncertainty still approaches the bounds smoothly. Published counts remain unchanged; reviewed missing targets are excluded from scoring. 0 retained district/category baselines use this possible-zero treatment.

Reviewed missing counts are listed separately from possible observed zeros. The affected district category partitions are omitted from allocation training and scoring because the missing votes may have been classified elsewhere. District turnout and formality totals remain usable. Missing earlier district partitions receive the labelled previous-election aggregate baseline. Published early/postal controls remain usable for prediction even when a final category target is missing; only known targets train their conversion multipliers. The exclusion applies while the reviewed group remains reported as zero; a refreshed source supplying a positive count releases it.

Known service, availability and definition changes use separate comparison rules. Where possible, the changed category is combined with ordinary votes into a broader observed pool. Its individual change is excluded from behavioural uncertainty training. VIC’s 2006–2010 declaration/provisional transition and WA’s 2021–2025 provisional-eligibility change use this common mechanism. WA 2025 is evaluated as postal versus all non-postal votes. QLD 2020 cancelled declared-institution placeholders are suppressed within the election; separately reported remote-mobile votes remain. The shared policy lists the affected elections and the reasons.

All retained district turnout and formality observations are strictly between 0% and 100%, so their transforms do not need endpoint replacements. Only the selected final pre-election early/postal observation enters calibration. Earlier daily zeros are irrelevant to that selection. 0 selected final controls have a zero count and require review.

Transforming a rate does not make its parent group interchangeable with another rate’s parent. Turnout uses enrolment; formality uses ballots; category shares use formal votes. Their joint uncertainty and category/turnout relationships are explicitly flagged for human review in the local output. Federal issuing-location versus resident district comparisons have a similar population mismatch; national early controls avoid using those district counts as resident estimates. Postal application conversion remains a linear multiplier because applications are not a strict subset of final votes.

Detailed Queensland composition training stays on the same side of the 2017–2020 reporting change as the test election. Earlier observations can still support broader combined-early and total-rate comparisons. A shared category name alone does not establish comparable observations.

## Individual elections and scarce training

This table shows which results support the averages and where default widths are needed. Rate training counts election transitions. Early/postal training counts independent calibration elections; a dash means that control is absent. Rate defaults use at least 0.167 turnout and 0.158 formality natural-log-odds units of common spread when fewer than two examples exist, with 0.167/0.211 local units when no relevant local history exists. Near 90% turnout and 95% formality these correspond to 1.5/0.75 common and 1.5/1.0 local percentage-point spreads; their percentage effects shrink at rates nearer an endpoint. Single-election conversion spread is at least 10% of its multiplier; missing local conversion spread also uses 10%. These are labelled broad assumptions, not fitted evidence.

Composition with fewer than two training elections receives additional independent logarithmic spreads of log(1.5) common and log(1.25) local, with the average direction removed before normalization. These are assumed relative widths, not percentage-point errors. The count-based zero treatment above applies both to fitting and starting prediction weights. New names without a local rate baseline receive 1.5 times the local rate scale. All defaults remain visible in local output.

| Election | Test method | Districts | Rate training | Early training | Postal training | Broad defaults | Election total in 80% interval |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2007fed | Other elections | 150 | 25 | — | 6 | No | No |
| 2007fed | Earlier elections only | 150 | 0 | — | 1 | Yes | No |
| 2008wa | Other elections | 59 | 25 | — | 4 | No | No |
| 2008wa | Earlier elections only | 59 | 1 | — | 1 | Yes | No |
| 2009qld | Other elections | 89 | 25 | — | — | Yes | Yes |
| 2009qld | Earlier elections only | 89 | 2 | — | — | Yes | Yes |
| 2010sa | Other elections | 47 | 25 | — | — | No | Yes |
| 2010sa | Earlier elections only | 47 | 3 | — | — | Yes | Yes |
| 2010fed | Other elections | 150 | 25 | — | 6 | No | No |
| 2010fed | Earlier elections only | 150 | 4 | — | 2 | Yes | No |
| 2010vic | Other elections | 88 | 25 | — | — | No | Yes |
| 2010vic | Earlier elections only | 88 | 5 | — | — | Yes | Yes |
| 2012qld | Other elections | 89 | 25 | — | — | Yes | Yes |
| 2012qld | Earlier elections only | 89 | 6 | — | — | Yes | Yes |
| 2013wa | Other elections | 59 | 25 | — | — | No | No |
| 2013wa | Earlier elections only | 59 | 7 | — | — | Yes | Yes |
| 2013fed | Other elections | 150 | 25 | 4 | 6 | No | Yes |
| 2013fed | Earlier elections only | 150 | 8 | — | 2 | Yes | Yes |
| 2014sa | Other elections | 47 | 25 | — | — | No | Yes |
| 2014sa | Earlier elections only | 47 | 9 | — | — | Yes | Yes |
| 2014vic | Other elections | 88 | 25 | — | — | Yes | Yes |
| 2014vic | Earlier elections only | 88 | 10 | — | — | Yes | Yes |
| 2016fed | Other elections | 150 | 25 | 4 | 6 | No | Yes |
| 2016fed | Earlier elections only | 150 | 12 | 1 | 3 | Yes | Yes |
| 2017wa | Other elections | 59 | 25 | — | 4 | No | Yes |
| 2017wa | Earlier elections only | 59 | 13 | — | 5 | No | Yes |
| 2017qld | Other elections | 93 | 25 | — | — | Yes | No |
| 2017qld | Earlier elections only | 93 | 14 | — | — | Yes | No |
| 2018sa | Other elections | 47 | 25 | — | — | No | Yes |
| 2018sa | Earlier elections only | 47 | 15 | — | — | No | Yes |
| 2018vic | Other elections | 88 | 25 | 4 | 5 | Yes | No |
| 2018vic | Earlier elections only | 88 | 16 | 1 | 2 | Yes | No |
| 2019fed | Other elections | 151 | 25 | 4 | 6 | No | Yes |
| 2019fed | Earlier elections only | 151 | 17 | 2 | 4 | No | Yes |
| 2021wa | Other elections | 59 | 25 | 3 | 4 | No | Yes |
| 2021wa | Earlier elections only | 59 | 19 | 1 | 2 | Yes | Yes |
| 2022sa | Other elections | 47 | 25 | — | — | No | Yes |
| 2022sa | Earlier elections only | 47 | 20 | — | — | No | Yes |
| 2022fed | Other elections | 151 | 25 | 4 | 6 | No | Yes |
| 2022fed | Earlier elections only | 151 | 21 | 3 | 5 | No | Yes |
| 2022vic | Other elections | 87 | 26 | 3 | 4 | No | Yes |
| 2022vic | Earlier elections only | 87 | 22 | 2 | 3 | No | Yes |
| 2024qld | Other elections | 93 | 26 | — | 4 | Yes | Yes |
| 2024qld | Earlier elections only | 93 | 23 | — | 4 | Yes | Yes |
| 2025wa | Other elections | 59 | 26 | — | — | No | Yes |
| 2025wa | Earlier elections only | 59 | 24 | — | — | No | Yes |
| 2025fed | Other elections | 150 | 26 | 4 | 6 | No | Yes |
| 2025fed | Earlier elections only | 150 | 25 | 4 | 6 | No | Yes |
| 2026sa | Other elections | 47 | 26 | 3 | 4 | No | Yes |
| 2026sa | Earlier elections only | 47 | 26 | 3 | 4 | No | Yes |

### Recent total and category interval performance

These rows make the quantity-specific limitations visible. Coverage is district enrolment weighted for district totals and categories, and either zero or 100% for an individual election total. They use the controlled model and starting totals based on half the average historical turnout change in log odds. A close national result does not establish close district or category results.

| Election | Quantity | Test method | Target coverage % | Observed coverage % | Interval score / 1,000 electors |
| --- | --- | --- | --- | --- | --- |
| 2022vic | Category counts | Earlier elections only | 80.000 | 96.296 | 202.498 |
| 2022vic | Category counts | Other elections | 80.000 | 94.702 | 192.658 |
| 2022vic | Category counts | Earlier elections only | 95.000 | 98.376 | 294.659 |
| 2022vic | Category counts | Other elections | 95.000 | 98.153 | 278.808 |
| 2022vic | District formal totals | Earlier elections only | 80.000 | 84.837 | 82.799 |
| 2022vic | District formal totals | Other elections | 80.000 | 84.837 | 82.765 |
| 2022vic | District formal totals | Earlier elections only | 95.000 | 94.237 | 121.333 |
| 2022vic | District formal totals | Other elections | 95.000 | 95.383 | 121.784 |
| 2022vic | Election formal total | Earlier elections only | 80.000 | 100.000 | 44.540 |
| 2022vic | Election formal total | Other elections | 80.000 | 100.000 | 43.729 |
| 2022vic | Election formal total | Earlier elections only | 95.000 | 100.000 | 65.706 |
| 2022vic | Election formal total | Other elections | 95.000 | 100.000 | 66.668 |
| 2024qld | Category counts | Earlier elections only | 80.000 | 94.052 | 794.377 |
| 2024qld | Category counts | Other elections | 80.000 | 94.467 | 787.064 |
| 2024qld | Category counts | Earlier elections only | 95.000 | 98.104 | 1182.318 |
| 2024qld | Category counts | Other elections | 95.000 | 98.095 | 1172.299 |
| 2024qld | District formal totals | Earlier elections only | 80.000 | 99.248 | 57.959 |
| 2024qld | District formal totals | Other elections | 80.000 | 99.248 | 56.847 |
| 2024qld | District formal totals | Earlier elections only | 95.000 | 100.000 | 88.767 |
| 2024qld | District formal totals | Other elections | 95.000 | 100.000 | 85.390 |
| 2024qld | Election formal total | Earlier elections only | 80.000 | 100.000 | 48.639 |
| 2024qld | Election formal total | Other elections | 80.000 | 100.000 | 46.658 |
| 2024qld | Election formal total | Earlier elections only | 95.000 | 100.000 | 74.182 |
| 2024qld | Election formal total | Other elections | 95.000 | 100.000 | 69.664 |
| 2025fed | Category counts | Earlier elections only | 80.000 | 72.506 | 382.777 |
| 2025fed | Category counts | Other elections | 80.000 | 72.496 | 382.830 |
| 2025fed | Category counts | Earlier elections only | 95.000 | 86.373 | 568.602 |
| 2025fed | Category counts | Other elections | 95.000 | 86.276 | 567.827 |
| 2025fed | District formal totals | Earlier elections only | 80.000 | 68.685 | 86.441 |
| 2025fed | District formal totals | Other elections | 80.000 | 66.018 | 88.510 |
| 2025fed | District formal totals | Earlier elections only | 95.000 | 87.647 | 113.346 |
| 2025fed | District formal totals | Other elections | 95.000 | 86.036 | 115.150 |
| 2025fed | Election formal total | Earlier elections only | 80.000 | 100.000 | 43.355 |
| 2025fed | Election formal total | Other elections | 80.000 | 100.000 | 40.355 |
| 2025fed | Election formal total | Earlier elections only | 95.000 | 100.000 | 64.207 |
| 2025fed | Election formal total | Other elections | 95.000 | 100.000 | 62.643 |
| 2025fed | Federal state formal totals | Earlier elections only | 80.000 | 72.418 | 66.074 |
| 2025fed | Federal state formal totals | Other elections | 80.000 | 72.418 | 68.426 |
| 2025fed | Federal state formal totals | Earlier elections only | 95.000 | 100.000 | 64.711 |
| 2025fed | Federal state formal totals | Other elections | 95.000 | 72.418 | 72.137 |
| 2025wa | Category counts | Earlier elections only | 80.000 | 97.780 | 302.343 |
| 2025wa | Category counts | Other elections | 80.000 | 98.597 | 328.562 |
| 2025wa | Category counts | Earlier elections only | 95.000 | 100.000 | 466.907 |
| 2025wa | Category counts | Other elections | 95.000 | 100.000 | 502.972 |
| 2025wa | District formal totals | Earlier elections only | 80.000 | 94.297 | 74.649 |
| 2025wa | District formal totals | Other elections | 80.000 | 96.110 | 74.668 |
| 2025wa | District formal totals | Earlier elections only | 95.000 | 99.006 | 105.487 |
| 2025wa | District formal totals | Other elections | 95.000 | 99.006 | 107.198 |
| 2025wa | Election formal total | Earlier elections only | 80.000 | 100.000 | 53.631 |
| 2025wa | Election formal total | Other elections | 80.000 | 100.000 | 52.159 |
| 2025wa | Election formal total | Earlier elections only | 95.000 | 100.000 | 80.191 |
| 2025wa | Election formal total | Other elections | 95.000 | 100.000 | 80.180 |
| 2026sa | Category counts | Earlier elections only | 80.000 | 96.491 | 117.959 |
| 2026sa | Category counts | Other elections | 80.000 | 96.491 | 121.335 |
| 2026sa | Category counts | Earlier elections only | 95.000 | 97.383 | 180.292 |
| 2026sa | Category counts | Other elections | 95.000 | 97.383 | 184.891 |
| 2026sa | District formal totals | Earlier elections only | 80.000 | 86.844 | 61.117 |
| 2026sa | District formal totals | Other elections | 80.000 | 86.844 | 62.097 |
| 2026sa | District formal totals | Earlier elections only | 95.000 | 97.843 | 82.963 |
| 2026sa | District formal totals | Other elections | 95.000 | 97.843 | 82.292 |
| 2026sa | Election formal total | Earlier elections only | 80.000 | 100.000 | 41.891 |
| 2026sa | Election formal total | Other elections | 80.000 | 100.000 | 44.637 |
| 2026sa | Election formal total | Earlier elections only | 95.000 | 100.000 | 64.493 |
| 2026sa | Election formal total | Other elections | 95.000 | 100.000 | 66.886 |

## SA 2026 broad-category diagnostic

This evaluates early, postal and the combined remainder separately, without creating old SA early/declaration shares. The previous-share baseline uses the observed ordinary/combined-declaration grouping and is compared only for totals. The category intervals are scored only on districts with reliable final splits; whole-election total scores still include all 47 districts. These are initial expectations; they do not determine whether a late booth or declaration batch has finished.

| Test method | Target coverage % | Observed category coverage % | Category interval score / 1,000 electors |
| --- | --- | --- | --- |
| Other elections | 80.000 | 96.491 | 121.335 |
| Other elections | 95.000 | 97.383 | 184.891 |
| Earlier elections only | 80.000 | 96.491 | 117.959 |
| Earlier elections only | 95.000 | 97.383 | 180.292 |

## Constraints, timing and reproducibility

### Keeping simulated counts physically possible

Rates and category shares describe fractions of finite vote pools. Their uncertainty should taper near zero and one, while positive starting quantities remain positive. Turnout and formality use log odds; category proportions use normalized logarithmic changes. No simulated rate is capped at 0% or 100%. This numerical property does not determine whether an observed zero is structural or a small-count outcome.

Early/postal controls also compete for a finite formal-vote total. Their positive count draws first provide requested amounts. The combined controlled share is then adjusted in log odds relative to its valid central share, retaining the early/postal ratio. This matches the requested count response for small changes, but gradually reduces extreme upward requests instead of assigning almost every vote to controls. The same principle applies to the declaration share of combined federal pre-polls. Incompatible starting expectations are reported as errors rather than silently capped.

For clarity, let s be the central combined controlled share and r the requested controlled share divided by s. The adjusted share has log odds equal to log(s / (1 - s)) + log(r) / (1 - s). The factor 1 / (1 - s) matches the requested count response at the central estimate. With 40% controlled centrally, requesting 120% gives about 81% after adjustment, leaving roughly 19% for the remainder instead of a numerical reserve near zero. Small changes are preserved to first order; the distribution average need not equal the original converted count.

National early/postal requests are normalized before district reconciliation. Smooth reconciliation can then change the national amount, even when the original requests fit. The maximum difference is recorded in the numerical output. State and election totals are always sums of the resulting district counts.

The table checks endpoints, conflicting requests and the size of this smooth adjustment. It uses controls, half the average historical turnout change in log odds, and both training methods. Percentages are first calculated within each prediction setup and then averaged equally across elections and training methods. **Rate endpoints** counts district outcomes with turnout or formality exactly 0% or 100%. **Requests exceed total** counts district outcomes where unadjusted early/postal requests exceed the formal total. **Control adjustment** adds absolute differences between requested and reconciled controls across districts, averages outcomes and expresses them per 1,000 electors; this includes smooth changes to feasible requests. **Positive categories becoming zero** uses category/outcome combinations whose central count exceeds one vote. **Largest category-sum discrepancy** compares summed category counts with the formal total after adjustment; it checks arithmetic, not forecast accuracy.

| Rate endpoints % | Requests exceed total % | Control adjustment / 1,000 electors | Positive categories becoming zero % | Largest category-sum discrepancy (votes) |
| --- | --- | --- | --- | --- |
| 0.000 | 0.007 | 0.980 | 0.000 | 0.000 |

The zero-category column checks positive starting categories only; it cannot assess whether a zero starting weight was appropriate. The near-zero accounting discrepancy confirms that the adjusted counts add up; it does not resolve the interval coverage limitations reported above.

### What the historical evaluation time measures

The evaluation took 37.15 seconds on this host. It covered 25 historical elections, two ways of selecting training data, three starting-turnout assumptions and three model/interface combinations: previous shares through the preparation interface, controls through the reference interface, and controls through the preparation interface. The two controlled interfaces share the same count calculation. That makes 450 prediction setups (25 × 2 × 3 × 3), rather than 450 separate elections or complete live forecast runs.

Each setup generates 1,024 possible final count outcomes for its one election. One outcome assigns formal totals and category counts to every district together, using both shared and district-specific uncertainty. These outcomes provide the prediction intervals; they contain no party vote shares, candidate results or winner calculations.

This is a historical comparison of alternative models. A single election forecast uses a chosen set of assumptions rather than this entire sweep. The elapsed time includes fitting the historical prediction setups, drawing counts, scoring them against final results and preparing the saved examples. It does not measure the C++ application’s preparation or simulation iterations and cannot establish whether the complete live forecast meets its runtime limit.

### What the preparation time for one election measures

The live simulator first performs a smaller preparation sample to summarize district variation, then uses saved statistics and separate shared responses in its main simulation iterations. The calculation below is a standalone count-only analogue of that arrangement. For each example election, it draws 288 possible count outcomes with election-wide uncertainty held fixed, records average category counts and their RMS variation, and calculates separate count responses to turnout, formality, category composition and any supported early/postal controls. The purpose is to provide a small reusable description of count uncertainty without running the richer count model in every main iteration.

Each row covers all listed districts in one example election. **Local count draws** is the number of possible outcomes used to calculate those district statistics, not the number of districts or main forecast iterations. **Count statistics and responses** is the time for that one preparation calculation, including its saved responses. It excludes parameter fitting, polling-booth calculations, party-share preparation, main simulation iterations and file writing. These timings therefore cannot be substituted for a measurement of the whole live application.

| Example election | Districts | Local count draws | Count statistics and responses (seconds) |
| --- | --- | --- | --- |
| 2022vic | 87 | 288 | 0.013 |
| 2025wa | 59 | 288 | 0.006 |
| 2025fed | 150 | 288 | 0.035 |
| 2026sa | 47 | 288 | 0.007 |

### Reproducing the results and saved examples

`fixtures-v3.json` is a set of saved numerical examples. It contains file format and model versions, prediction inputs, the elections used for training and their fitted uncertainty scales, separate count responses, preparation statistics and three reproducible outcomes from each representation. Actual final results are absent from its prediction inputs. `analysis.json` contains prediction scores and the parameters fitted for each historical test election. Both files are generated locally and ignored by Git; the consolidated report is public.

The following elections are omitted from these joint total/category comparisons because their published category definitions changed. Training across that change would confuse a reporting difference with a change in how people voted:

| Election | Reason |
| --- | --- |
| 2015qld | Category definition break |
| 2020qld | Category definition break |

Run from analysis/ on native Windows:

~~~powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prior_prototype --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prior_prototype
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prior_prototype --check
~~~

Defaults are 1,024 count outcomes per prediction setup, 288 local count draws per saved preparation example and a fixed random seed for reproducible results. The input/code fingerprints, numerical-library version and options identify the generated analysis. --check detects local revisions; refresh source adapters and the federal supplement before regenerating after source corrections. It does not poll remote sources. NSW remains excluded from this prototype.

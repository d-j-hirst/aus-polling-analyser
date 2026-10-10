# Live turnout model

This document explains how the live forecast estimates votes still to be counted, how those estimates affect its uncertainty, and how to supply the required election inputs.

The model combines pre-election expectations with reported first-preference counts and the history of counting received so far. It supplies one vote-count account to the live forecast: booth estimates, district totals, completion percentages and simulated additions all use that account. Party composition and preference rechecking are prepared separately and then applied to those counts.

## Inputs and preparation

A **prior** is the fixed pre-election distribution of possible final formal vote counts. Each **prior outcome** is one possible set of final counts for all districts and categories together. Keeping those counts together retains their relationships: for example, one outcome can represent unusually high turnout across many districts. Enrolment, previous turnout and **formality** (formal votes as a proportion of turnout), and available early-vote and postal-application counts determine the starting expectations.

The calculation uses several derived quantities with different roles:

| Quantity | Meaning and use |
| --- | --- |
| Turnout rate | Turnout divided by enrolment. |
| Formality rate | Formal votes divided by turnout. Multiplying enrolment by both rates gives the expected formal total. |
| Conversion factor | A multiplier converting a reported early-vote or postal-application count into expected final formal votes. Its uncertainty describes how reliably that reported count predicts the result. |
| Control | That converted expected count and its uncertainty. It anchors a category or group of categories before results arrive. |
| Unit weight | A service's expected fraction of its district/category group. Weights split a group total between services; they are not extra votes. |
| Pooled measurement | A summary combining evidence from several districts. Shared counting progress is measured separately for each category, excluding unavailable services and limiting exceptional districts' influence. |
| Response coefficient or weight | A number controlling how strongly one piece of evidence changes an estimate. It is a probability only where explicitly described as one. |

For example, 100,000 enrolled electors, 90% turnout and 97% formality imply 90,000 ballots and 87,300 formal votes. A hypothetical conversion factor of 0.97 applied to 20,000 reported early votes gives a control of 19,400 expected formal early votes. The model retains uncertainty around these expectations rather than treating them as final counts.

Rates and category shares stay within their parent totals using a **bounded transformation**. For a proportion `p`, the calculation works with its log odds, `log(p / (1 - p))`, and converts back afterwards. Thus a change is measured relative to both the included and excluded parts of the parent, and simulated proportions remain between zero and one without endpoint caps. Application-to-vote conversion is a proportional process and retains its linear factor.

An input **unit** is an ordinary booth, a pre-poll voting centre (PPVC), or a declaration category such as Postal. Its metadata identifies its district, category group, expected share of that group, historical match, electoral-assistance voting (EAV) service status and any known closure or calibration exclusion. A **category group** is a division supported by historical results; finer splits that were not reported historically must not be invented as observed voter behaviour.

Each election requires `forecasts/<election>/live-inputs/turnout-prior.json`. The live run selects this path using its own election code. A missing prior or incompatible mapping produces an input error. Election timetables, category definitions and service exceptions are data in this input, rather than election-code branches in the turnout calculation.

## Recovering erroneous live counts

Recovery lets a forecast update when a clearly invalid booth or category record appears in an otherwise readable feed. The program identifies and checks the candidate counts before constructing the live model. Turnout estimates, party composition and completion percentages then use the same effective records, with district summaries rebuilt from them.

FP (first preferences) and TCP (two-candidate preferred) are checked independently. An invalid count is replaced as an entire candidate-count vector: the program uses the most recent compatible accepted record from an earlier source time, or treats the count as unreported if none exists. It never repairs just one candidate's count. A restored TCP must also be compatible with the effective FP record.

| Count status | Effect on the forecast and counting history |
| --- | --- |
| Current | A valid measured record supplies counted votes and a current observation of counting progress. Explicit zero counts remain genuine zeros. |
| Restored | An earlier accepted record supplies counted votes and keeps its original observation time. It supplies no fresh evidence of activity or a pause. |
| Unavailable | The record supplies no counted lower bound. Its service retains its prior estimate and is not assumed closed. |
| Explicitly closed service | The configured service exception remains separate from measurement availability; it contributes no behavioural progression evidence. |

For example, if a booth previously reported 300 FP votes and now contains a malformed candidate count, all its FP counts revert to the earlier 300-vote record. Other booths can still update. If the sum of a district's FP records reaches or exceeds enrolment without identifying a particular invalid record, its whole FP account reverts to the earlier consistent account. With no compatible previous account, district FP is unreported. Current TCP is rechecked against that account. A rejected FP source cannot supply a new finalisation flag; whole-district restoration can retain the earlier account's validated flag.

Small FP/TCP differences and incomplete declaration batches are normal. For compulsory preferential counts, TCP exceeding FP by **more than the larger of 10 votes or 5% of FP** makes the current account inconsistent and triggers recovery. Ordinary/PPVC TCP below 95% of FP retains the existing rule that it cannot supply comparable preference evidence. Declaration TCP can legitimately lag FP. For optional preferential voting, exhausted ballots must not be mistaken for missing TCP; set `options.optional_preferential` to `true` in the election's input prior. It defaults to `false` for existing compulsory-preferential inputs. These comparison allowances are named beside the validation rules in `LiveInputRecovery.h`.

A recount can temporarily reset FP while TCP still reports the larger earlier count. If current FP has fallen and TCP exceeds it beyond the allowance above, a **reconciliation hold** retains both vectors from the most recent compatible earlier accepted account with usable preferences. For example, an earlier 2,500 FP and TCP votes remain effective while current FP resets to 20 but TCP still reports 2,500. Both restored counts keep their original observation times and supply no fresh counting activity; the current source cannot make them final. A grouped warning gives the current and held totals. Once the current pair reconciles, it replaces the held account, even if both totals are lower. Normal FP additions ahead of preferences, small differences, coherent downward corrections and optional preferential counts retain their existing treatment. With no suitable earlier pair, independent record recovery still applies.

The small comparison allowances cannot authorize a district TCP total at or above enrolment. That independent parent contradiction also restores compatible earlier preferences or treats them as unreported, while preserving valid current FP.

The private cache at `forecasts/<election>/live-snapshots/accepted-counts/` stores integer candidate counts, identities, status, completion information and original observation times. It starts empty for a real election. Matching uses election, district, booth/category role and candidate identities, rather than array positions. Earlier forecast exports and aggregate turnout history cannot supply accepted candidate-count fallbacks. Repeated source times replace their cache record atomically; a content fingerprint distinguishes source revisions. Recovery reads only earlier sources and keeps only the needed count state in memory.

Fresh counting history omits restored or unavailable records. A category-group observation is also omitted if any of its available services needed recovery, so restored counts do not manufacture counting events or pauses locally or in measurements shared across districts. Restored FP retains its earlier validated completion status; an unavailable count cannot acquire completion from the rejected source. Accepted-count and turnout-history writes are queued during preparation and performed only after a successful main calculation.

The **Results** tab retains a non-modal warning panel, with one row per issue type and a worst example measured in votes. Selecting a row opens affected-record details, including rejected/effective counts and any restored source time. Routine incomplete counting is hidden behind an informational checkbox. Private snapshot metadata contains compact summaries for **Live Booths**; the analysis sidecar contains the detailed recovery records. Count history is never embedded in `.pol2` projects.

Unreadable XML, missing essential identities or required source-time metadata, and uninterpretable source structure stop the update. There is no numerical percentage cutoff for rejecting a source. The previous forecast and counted outcomes remain intact, and that attempt writes no new accepted history or snapshot exports. A replay continues after recoverable errors and stops after a failed update. Configuration errors and internal calculation failures keep their normal failure paths. Statistical detection of unusual but possible results is separate from this recovery mechanism.

Portable fictional fixtures exercise recovery, identity compatibility, count procedures, chronological history and failed-update rollback in `tests/LiveInputRecoveryTests.cpp`. Run the repository's portable test script to check them. The optional `LIVE_INPUT_PARSER_TESTS` build also exercises the commission parser when TinyXML is available; licensed feed replay checks remain private. `tests/LiveVoteMappingTests.cpp` is a separate component fixture requiring TinyXML and `LiveV2.cpp`; it checks historical candidate-level TCP and preservation of vote totals under separate or combined Liberal/National mappings without running a forecast.

The Results tab's **ALP swing** compares the live model's seat-wide TPP estimate with the configured previous margin. That estimate includes counted votes and the expected composition of outstanding votes. **Proj. Margin** reports the simulation's average forecast margin, with its change from the same previous margin in parentheses. The columns should converge late in the count; earlier differences can arise from simulation averaging and the weight still given to the pre-count forecast. Before any live evidence exists, ALP swing is blank. Weighted historical booth swings train the model but are not displayed as the seat-wide swing: after redistribution, those comparisons can cover only a small, unrepresentative part of the seat. Preference-flow estimation requires compatible FP and TCP accounts, but an unavailable preference-flow estimate does not discard an otherwise valid TPP comparison. Historical TCP is used where supplied, with FP-based estimates only where it is absent. Party mapping preserves the sum of candidate counts, including when a project intentionally groups Liberal and National candidates together.

The prior retains the enrolment used when its vote-count expectations were prepared. Authorities may revise published roll figures during or after counting; those revisions do not themselves report extra ballots and do not rescale the fixed prior. Differences between positive source enrolment and prior enrolment are recorded by district in the turnout diagnostic's `enrolment_revisions`, with a summary in the application log. A missing source figure is not recorded as a zero enrolment. Update the prepared inputs explicitly if their original enrolment was incorrect.

Prepare the prior from a normalized configuration with the maintained parameter preset suited to its category definitions. Run from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_live ../forecasts/2026sa/live-inputs/turnout-config.json
```

The configuration contains:

- `election`, `parameter_preset`, `preparation_samples`, `count_draws` and `seed`. Preparation samples are joint prior outcomes, not full forecast iterations. The usual setup uses 256 outcomes and eight additional count draws per outcome for diagnostics.
- `inputs`: ordered district and category names, subdivisions for regional pooling, enrolment, previous turnout and formality percentages, local uncertainty scales, converted controls and proportions within controlled and remaining groups. Its `identity` fixes the random streams. A group's indices select categories; its district weights sum to one within that group.
- `units`: the identities and expected weights of the reporting services. Each weight is a fraction of its district/category group. `matched` records a reliable historical comparison; `eav` identifies the separate small-service treatment. `closed` and `excluded` require explicit reasons. These records contain no current-election counted results.
- `schedule`: local-source-clock `postal_deadline`, `receipt_deadline` and `poll_close`, and whether delayed reporting should reduce unreported PPVC expectations. Dates use `YYYY-MM-DDTHH:MM:SS`; unavailable deadlines are null. Receipt is the last permitted arrival time, rather than the expected completion of counting.
- Optional `sources`: source identities and hashes documenting the pre-election inputs.
- Optional `inactive_contests`: named postponed contests with `status: "postponed"` and a reason. These remain seats in the ordinary forecast, but have no turnout units or weight in counted completion. A feed can omit them or report zero votes; any reported FP or TCP votes require reviewing this configuration before proceeding.

The command draws the distribution without fitting a model or reading retrospective reports, prototype allocations or archived snapshots. Its output records configuration, maintained-parameter and preparation-code hashes. Regeneration is appropriate when the election's input mapping or pre-election evidence changes; live observations never overwrite the prior.

The live source must also supply a timestamp for measuring counting activity. MediaFeed sources retain their `Created` time; Queensland retains `generationDateTime`, including fractional seconds. Zoned timestamps require normalization to the configured local clock before use. A source without a published time needs an explicitly retained collector timestamp; a saved forecast's old display date is not a substitute.

Victorian results retain the VEC's combined Early category rather than inventing individual early-voting centres. The loader also preserves the separately reported `MarkedAsVoted` candidate counts; historical `Marked As Voted Votes` records use the same vote type. These counts enter the normal declaration accounting and remain distinct from Provisional votes.

First preferences can arrive before their preferences have been counted. The live calculation separates those already reported voters from voters whose first preferences are still outstanding. Where a TCP total exactly matches a compatible, earlier accepted FP batch, that earlier candidate vector identifies the primary composition of the preference batch. Its observed preference flow adjusts the configured party-specific flows; the adjustment is then applied to the additional FP candidates, rather than copying the earlier batch's TCP share. For example, a batch with many Greens primaries may have a high Labor TCP share even without unusually strong preference flows. A later batch with more Liberal primaries keeps those Liberal votes when its missing preferences are estimated.

Only successful, strictly earlier received feeds in the private accepted-count cache can supply a historical cohort. The cache retains the latest identified pair when the current FP or TCP moves beyond it, including pairs assembled from two different received sources. This stores integer observations and their original times, not forecast estimates. Older cache entries without that field remain readable; their aligned FP/TCP accounts and matches to the current TCP remain available. Election, district, service and candidate identities must agree; neither finalist may receive fewer TCP votes than its corresponding primary count. Every candidate's earlier FP count must fit within the current FP vector before it can be subtracted as a cohort. An exact total match is evidence of a common batch, rather than proof of individual ballot identity. Downward candidate revisions or a changed candidate list prevent that subtraction. Optional preferential exhaustion can also reduce TCP totals, so the current matching rule is restricted to compulsory-preferential counts.

Preference-flow adjustments use log odds: `log(p / (1 - p))` for a flow fraction `p`. A local batch's adjustment receives weight `N / (N + 500)`, where `N` is its non-finalist primary count; the rest retains the existing historical and pooled flow estimate. Non-classic contests use their existing seat preference assumption in place of Labor/Coalition party flows. The latest compatible matched FP/TCP pair remains useful after another TCP batch arrives: subtract its FP vector from the current FP vector, then use that residual primary mix and calibrated flows to estimate the smaller number still awaiting preferences. The newer counted TCP remains fixed. Where no compatible contained cohort exists, estimate the unfinished portion directly from the current FP mix. Subtracting counted TCP from an estimated whole-category TCP would incorrectly assign no votes to a candidate whenever that estimate already lay below their counted total.

For voters whose first preferences have not yet arrived, each declaration category starts with its historical primary composition, adjusted by movement measured in ordinary booths and the existing pooled category biases. Its own reported primaries then gradually replace that starting estimate. Ordinary polling locations include individually identified PPVCs; aggregated Early/PrePoll services remain declaration categories. The category's partial declaration batch does not also train its ordinary-booth starting movement. Existing new-candidate and missing-history fallbacks remain available where a historical primary comparison is absent.

Let `n` be the category's counted FP votes, `T` its expected FP total and `r = max(T - n, 0)` its expected additions. Absent, Postal and other declaration categories use observed-mix weight `min(n / T, 1)`. Aggregated Early and declaration PrePoll use `n / (n + 4r + 300)`: their initial batches can cover quite different areas, so the estimate adopts their composition more cautiously while much of the service remains outstanding. The factor 4 multiplies expected remaining votes; 300 is an additional vote-sized allowance for uncertain representation. Both weights are zero before any primaries arrive. A 45-vote batch in a service expected to contain 2,700 votes consequently receives about 1.7% weight under the first rule. Blend each candidate's share on natural log odds and then normalize the resulting shares to sum to one. Only estimated additions change; reported integers remain fixed.

When estimating current primary proportions, use `(candidate votes + 0.5) / (total FP + 0.5 × number of candidates)`. This **half-vote smoothing** keeps genuine observed zeros finite without assigning a fixed percentage floor: a zero among four votes on a four-candidate ballot estimates 8.3%, but a zero among 1,000 estimates only 0.05%. It supplies no fictitious counted votes and includes only candidates actually contesting that count. Primary-movement evidence is also capped at the votes actually reported, before applying the booth's seat-relevance discount. A completed ordinary booth supplies its usual observed-size evidence; a tiny partial record cannot receive its much larger historical booth's weight. Completion retains its separate expected-size denominator.

In compulsory-preferential counting, new voters' TPP is calculated from this same future FP mix and configured preference flows. The future-flow prior includes historical and seat/region/election evidence, with the category's separate local preference deviation excluded. Its latest compatible matched cohort adjusts that prior once using the `N / (N + 500)` weight described above. Already-counted voters awaiting preferences keep their own cohort/residual calculation. Non-classic TCP with observed finalists uses its existing seat preference assumption with the shared future FP mix; unreported non-classic pairs retain their existing comparison methods. The Postal adjustment is −0.08 in natural log odds for Coalition primaries on the observed side of the FP blend, fading with that side's weight. It needs no second independent Postal TPP shift. Optional-preferential pair projection retains its existing exhaustion-aware boundary and continuing-vote extrapolation; it was not calibrated by these compulsory-preferential experiments.

The common parameters and numerical component are maintained directly in `LiveNewVotes.h`; matched-cohort evidence and its 500-vote weight are in `LivePartialCount.h`. Neither requires private replay outputs, election-specific code or activation variables. Preparation shares one compact, immutable starting FP composition per declaration service between scenarios. Each variability sample projects its own future composition, which FP and pair counts consume together. Analysis sidecars record the starting composition, future composition and observed weight under each declaration booth's `new_vote_composition` for subsequent inspection.

VEC result captures can omit ordinary booths before any votes report. The loader retains their preloaded identities and leaves their measured vote maps empty; absence from one capture does not establish a closure or a measured final zero.

### Victorian input adapter

The Victorian adapter prepares the normalized configuration from candidate and ordinary-booth preloads, the previous election's public district and booth results, and the current election's recorded pre-election early/postal observations. It does not read current-election final totals or live result snapshots. Election settings are maintained separately in `forecasts/<election>/live-turnout-settings.json`; authorised preloads and generated inputs remain operator-local.

Run from `analysis/`, after installing `downloads/<election>_candidates.xml` and `downloads/<election>_booths.xml`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_vic 2022vic
```

`--candidates` and `--booths` can select files in an external operator directory. The command writes `turnout-config.json`, `turnout-input-audit.json` and `turnout-prior.json` under the ignored election `live-inputs/` directory. The audit identifies district baselines, selected operational observations, booth matches and fallback size weights. Source-content hashes record which revisions were used.

District turnout and formality use the same-name previous district or its explicit `sPreviousName` predecessor. A party-share proxy (`sUseFpResults`) does not establish a turnout predecessor. Missing district rates use aggregate historical rates; missing category partitions use complete, reconciled historical partitions. Renamed or missing baselines have 1.5 times the normal local uncertainty. Same-name boundary changes are not reconstructed by this adapter.

Ordinary booth matching follows the C++ loader: globally unique current names can match historical names, repeated names need the same district, and conflicting historical matches are discarded. Matched formal counts provide relative size weights. Unmatched or ambiguous booths use the median matched size in their current district, falling back to the shared median when needed. These weights divide the district's ordinary allowance; they do not add votes to it. A transferred match can still supply starting size information. The audit records whether the match belongs to the district's chosen historical predecessor. The active C++ calculation does not learn a shared ordinary-booth size adjustment from these matches. The loader retains separate identities for historical districts removed by redistribution, so their names and counts remain available for predecessor lookup.

Early and Postal each have a district control. Absent, ordinary and the combined other category share the remaining allowance proportionally. Provisional and Marked as voted remain separate reporting units within the other category, using their previous split. Possible observed zeros receive a half-vote equivalent; unavailable or missing partitions are not filled with invented zero counts.

The 2022 settings exclude Narracan's postponed November contest from counting while retaining its seat forecast. They use the installed `2022vic/earlier_only` coefficients and the postal receipt deadline of 6 pm on 2 December, recorded in the [VEC's submission on the 2022 election](https://vec.vic.gov.au/-/media/2cf4c88beaaf4fcf983743da5ebe9d05.pdf). Receipt closure does not imply counting completion. A replay that ends on 5 December can be compared with reviewed final district totals, but its last captured feed is not treated as final merely because no later capture is available.

The `2022vic` party configuration follows the federal setup: Liberals and Nationals are separate parties in seat calculations and live counts, while the fitted `LNP` series remains the combined Coalition projection. The existing Nationals coefficients divide that projection in seats where both parties stand; known candidate lists determine whether only one stands. The six National incumbents and the five active contests with candidates from both parties are recorded in the seat parameters. Narracan retains its postponed-contest setup.

For this first configuration, both parties inherit the former combined party's non-classic preference estimates, including when a National candidate replaces a combined-Coalition preference target; these are maintained assumptions rather than newly fitted estimates. A project that retains a combined Coalition party can still use `NAT` as a feed alias, but that alias does not activate a second-party split or add the same projection target twice.

The fictional configuration in `tests/fixtures/turnout/live-inputs-example.json` demonstrates the format without requiring authorised feeds:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_live ../tests/fixtures/turnout/live-inputs-example.json --output ../downloads/turnout/example-prior.json
```

## Updating counts as results arrive

Each received source updates the same immutable prior afresh. Reported ordinary and PPVC booths are approximately complete; declaration counts are lower bounds. The calculation preserves counted first-preference votes, allocates nonnegative additions, and keeps all category and district totals consistent with their parent account. Minor ordinary-booth rechecks do not create a staged-addition expectation.

When every configured service in a district is complete or explicitly closed, its turnout estimate is exactly the counted total, even without a district-level finalisation flag. An open declaration service or unreported booth still allows additions. Missing service records do not establish completion.

Unreported matched PPVCs retain their own size expectations with conservative partial compensation for changes in other matched centres. Here **compensation** means that fewer votes than expected at reported centres can increase expectations at an unfinished centre, and vice versa. The coefficient is selected from the centre's mean starting expected count:

| Expected PPVC count | Compensation coefficient |
| --- | ---: |
| Below 2,000 | 0 |
| 2,000 to below 4,000 | 0.14 |
| 4,000 to below 8,000 | 1.17 |
| 8,000 or more | 0.63 |

These are coefficients of changes in log odds, rather than fractions of a vote-count shortfall or correlation measurements. The adjustment is weakened by the expected fraction of the other centres that has reported. The coefficients come from the conservative end of the Federal 2025 reliable-match comparison, excluding Brand's reporting consolidation; they have not been independently validated on another election. Size bands are discrete, while influence within a band changes continuously with the reported evidence. New or changed centres use their supplied starting weights; EAV services receive their separate small-service treatment. Redistribution effects beyond these input mappings are not inferred by this calculation.

A known closure contributes no expected additions or voter-behaviour evidence. In particular, its recorded zero does not suggest that other districts have more counting left. This exclusion also applies when all services in a category group are unavailable; a group with an open service still supplies progress evidence. An open service that has reported zero remains evidence that counting has not started.

When the ordinary/early categories largely finish, an open declaration category can initially receive a large **remaining allowance**: the difference between its group's expected final total and the votes already counted. As the group's counting settles, the model gradually gives more influence to the unfinished category's own size estimate and reduces an excessive group allowance. The district's projected formal total can decrease as a result. This uses actual group counts, including PPVCs within a historically combined early-vote group. Evidence that the whole group is settling is measured separately from evidence that one declaration category is slowing.

For example, a combined early-vote group expected to contain 20,000 formal votes might have 18,000 PPVC votes and 100 early-absent votes counted. Keeping the group expectation fixed would assign all 1,900 remaining votes to early absents. If that category's own expected final count is only 300, its own remaining estimate is 200. Repeated quiet group counts gradually move the remaining estimate towards that smaller amount, while preserving the 18,100 votes already counted. This is a smooth adjustment, not a rule declaring the category finished.

The model keeps two kinds of late-count uncertainty separate. Routine expected additions recede as counting slows and the receipt timetable passes. Exceptional additions remain possible with a low, gradually declining probability. There is also a district-wide outcome with exactly no new first-preference votes. Completion flags make that outcome certain; quietness alone does not.

Each unfinished declaration category combines previous expectations, small additions and exceptional late batches. Their probability weights change continuously with the evidence. They are **unconditional weights**: together they sum to one minus the district's no-addition probability. For example, if no additions have probability 60%, that category's other three weights together sum to 40%. When those other outcomes average 100 additional votes, its overall expected addition is 40 votes. Sampling within those outcomes divides each weight by 40%, so their conditional probabilities sum to one. The exceptional component is retained explicitly, including when none of the short diagnostic sample's draws selected it.

After receipt closes, the installed timetable allows 48 counting hours for processing and a smooth 12-hour transition away from routine expectations. Sundays contribute half as much processing time as other days. Recent local activity can extend routine processing; activity after the deadline gradually loses influence on other districts. Unfinished declarations retain a starting exceptional probability of 2%, which declines with time and remaining probability outside the no-addition outcome. This is an operating assumption, rather than an independently established frequency of completion.

## History during a live election and a replay

The live application retains checked cumulative declaration and category-group counts under `forecasts/<election>/live-snapshots/turnout/`, using the feed's source timestamp. This directory starts empty during a real election and grows only as feeds are received. The Federal replay script may also use a separate private `live-snapshots/feed-sources.json` manifest to locate hashed raw archives; it does not read the turnout prior for archive selection. It stores observations rather than posterior forecasts, and is separate from saved forecast outputs and any raw feed archive.

Each observation records the service-to-category mapping used to form its counts. Changed category definitions require rebuilding history from the retained sources; older group counts cannot silently become evidence of changed voter behaviour.

A source-time revision replaces that observation, including downward corrections. Repeating a run therefore does not count its evidence twice. Replaying an earlier feed reads only strictly earlier source times, then appends the current checked counts in memory; later observations in the directory cannot influence that replay. Deleting history removes progression evidence while leaving the pre-election prior intact. A fresh archive replay must process sources chronologically to accumulate its available history.

## How counts enter the forecast

`LiveTurnout::Prepared` owns the shared count distributions, booth means, counted-party constraints and cached remaining-voter composition. `LiveTurnoutPreparation.cpp` translates LiveV2's checked results into that account once per source. LiveV2 requests its count targets when preparing booth composition and uses the same account for completion and scenario draws.

Reduced preparation continues to measure party-composition uncertainty at the count means. Full simulation iterations select a joint prior outcome, a no-addition/late-batch outcome and the cached composition response. They do not reread history, refit a model or recompose every booth. Turnout, formality, early/postal conversion and category composition remain distinct uncertainty sources within the joint prior; their exported sensitivities are descriptions of those count responses, not additional errors to apply again. The overlapping legacy declaration-size variation is removed.

First-preference (FP), actual two-candidate-preferred (TCP) and estimated Labor-versus-Coalition two-party-preferred (TPP) counts share future-voter additions. Preferences still missing on already-counted FP votes remain outstanding even in a draw with no new voters. Existing counted candidate votes constrain each projection. Before any FP votes report, the standard simulation owns the forecast and receives no live count constraint.

Turnout is always part of the live calculation. It has no environment-controlled mode or alternative sizing path. Standard simulations without live results do not require these live-election inputs.

## Rechecking preferences on counted ballots

Preference rechecking allows a reported TCP allocation to be revised without
adding votes. It is independent of turnout and outstanding-vote uncertainty.
Raw feed counts and FP counts remain unchanged. A scenario can transfer existing
preferences between the reported finalists, preserving the booth preference
pool and the district pair total. The main simulator receives that scenario's
adjusted counted pair, so the correction remains possible even with no further
votes expected.

The comparison uses ordinary booths and PPVCs with exactly matching FP and TCP
totals, the district's reported pair, and possible preference gains. Unfinished
preferences, other services and exhausted OPV counts do not supply a measured
flow. The preference flow is a finalist's TCP minus their own FP, divided by FP
for all candidates outside the finalist pair. The regression relates its log
odds to that finalist's transformed primary share, the largest non-finalist
party's share of the preference pool, and PPVC status. These predictors have
different parent groups: they describe an association rather than interchangeable
shares. Every target booth is omitted from its regression. Robust weighting
limits unusual peers' influence, and the comparison scatter gives more weight
to booths of similar formal size. Sparse comparisons retain fitting uncertainty;
a booth with no comparable peer receives both reported-flow recheck components,
but no regression-directed correction.

The four possible outcomes distinguish uncertainty about a count from evidence
that its allocation is wrong:

1. The allocation remains exactly unchanged.
2. A routine small recheck is centred on the reported preference flow.
3. A larger background recheck is also centred on the reported flow. Its chance
   and count allowance grow smoothly with booth size and remain available even
   when the booth agrees with the regression. The chance of a recheck therefore
   does not itself imply that votes should move towards the regression.
4. A regression-directed correction moves towards the predicted flow. Its
   probability depends on booth size and discrepancy, and its conditional
   destination approaches the full prediction as discrepancy increases.

The discrepancy score is the absolute observed-minus-predicted preference log
odds divided by the allowance for peer variation, sampling and regression
uncertainty. It is not a calibrated probability that the count is wrong. All
three changing outcomes use Student-t distributions with five degrees of freedom
on the transformed preference share. Their centres specify medians rather than
exact mean counts; the bounded transform can produce a small difference between
the two. Corrections may overshoot or move away from a component's centre. A
quarter-vote equivalent permits behavioural zeros while the unchanged outcome
retains their exact reported value.

For reproducibility, let `R = formal votes / 1000`, `s` be the discrepancy score,
and `logistic(x) = 1 / (1 + exp(-x))`. The fixed shared parameters are:

| Quantity | Value |
|---|---|
| Directed-correction probability | `logistic(-7.809508178103534 + 3.20705363776961 * log(1 + s*s) + 0.520532187574425 * log(R) + log(1 - exp(-R)))` |
| Background chance, conditional on no directed correction | `logistic(log(0.10534213368051161 / (1 - 0.10534213368051161)) + 0.39602184615771785 * log(R))` |
| Routine chance, conditional on neither directed nor background rechecking | `0.5327639742322045` |
| Routine count-scale allowance | `exp(0.08737413053901756) * R^0.339131984114037` votes |
| Additional background count-scale allowance | `exp(2.329939422870341) * R^0.5629780364803446` votes |
| Conditional fraction of the transformed gap closed | `0.8093269894532144 + (1 - 0.8093269894532144) * s*s / (s*s + 2.25)` |
| Directed Student-t scale | `exp(-0.22719181582595513) * size-matched peer scatter` |

The background count-scale allowance is the routine allowance plus the additional
allowance in the table. Let `M` be the preference pool and
`p = (reported preference gain + 0.25) / (M + 0.5)` be its reported share after
the quarter-vote adjustment. Each count-scale allowance `A` becomes
`log(1 + A / (M*p*(1-p)))` on the log-odds scale.
These are Student-t scales, not standard deviations, interval widths or count
limits. The component probabilities are absolute weights after applying their
conditional chances. The remaining weight leaves the allocation unchanged.

The size term reduces the directed correction's odds for small booths, rather
than multiplying its probability by a value below one. Compelling evidence can
therefore overcome small size without a probability ceiling. All responses are
continuous; there are no eligibility thresholds in booth size or discrepancy.

The shared parameters were fitted offline to changes between the Federal 2025
20 May snapshot and the 31 May reference. Five groups of seats were used for
comparisons, with each test group's seats absent from its parameter training;
the installed values then use the complete calibration sample. Later counts
calibrated those values only: live regression inputs come entirely from the
current snapshot. This is evidence from one election rather than independent
validation across elections.

Preparation retains the four largest correction contributors per district as
explicit mixtures and combines the others into 1,024 cached aggregate outcomes.
Main iterations choose one aggregate outcome and draw the explicit components.
The largest corrections retain continuous tails beyond the preparation sample;
the smaller contributors' tails have the bank's finite resolution. This error
source is separate from the normal preparation for outstanding-voter composition.
For a classic pair the transfer changes TPP; for another pair it changes TCP and
does not invent an observed classic TPP.

The analysis JSON records `live_analysis.preference_rechecking`, with version
`live-preference-rechecking-2`, calibration identity, preparation time, exclusions,
and per-booth probabilities, relative discrepancy, conditional movement and
expected revision in candidate votes. `broad_probability` is the absolute
regression-directed probability; `background_probability` is the separate
reported-flow component. They are separate from `routine_probability` and
`unchanged_probability`, and the four weights sum to one. The three
`*_log_odds_scale` fields describe their Student-t scales. The diagnostic also
identifies the explicitly drawn contributors. Unrandomised node
counts still describe the reported allocation and its outstanding-vote estimate;
simulated forecast distributions include the separate rechecking draw. No
additional environment variables or replay artifacts are required.

To inspect the regression evidence and later revisions used by the calibration,
generate the booth audit from saved GUI analysis snapshots. Run from `analysis/`;
the final snapshot in the input series is the later comparison reference and is
excluded from the test dates. This command evaluates the contemporaneous booth
regressions, rather than fitting the four-component correction distribution:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.preference_flow_outlier_audit --series ../live_runs/2025fed-turnout2 --output ../downloads/turnout/preference-flow-audit/audit.json
```

## Diagnostics, parameters and verification

Saved analysis exports contain `live_analysis.turnout`. It reports the source time, pre-election provenance, history observations used, preparation time, district/category estimates, no-addition probabilities, conditional late-batch settings and booth allocations. `integrated_projections` shows the count means installed in FP/TCP/TPP and the outstanding preference gaps.

Count percentiles include the website's probability-bar breakpoints, alongside the central 95% interval. A percentile describes a count distribution, rather than a party-share interval or a winner probability. A conditional late-batch mean assumes that component occurs; it is not the expected remaining count.

Maintained prior coefficients and aggregate new-PPVC assumptions are in `analysis/lib/turnout/live_parameters.json`. Generic live count responses and timetable settings are in `TurnoutModel.cpp`; preference-rechecking coefficients are in `LivePreferenceCorrections.cpp`. Experimental results cannot silently change these parameters. Election-specific input priors and timetables remain distinct from these shared fitted assumptions.

The separate party model reads `analysis/Data/preference-estimates.csv`. Its columns are year, jurisdiction, party/measure, current preference-flow percentage and optional exhaustion percentage. An optional sixth numeric column supplies a different previous-election preference-flow percentage for live baseline interpretation; comment text starting with `#` is ignored. This keeps a known change in preferences in the input data without an election-specific code branch.

Live/media-feed snapshots and close derivatives, including operational configurations, priors and received history, remain private and gitignored. The repository supplies the method and installed coefficients, so operators can apply it to their own authorised files. Its historical live calibration cannot be reproduced by cloning the repository alone. Public final-result exports may be used by the separate historical analysis. Live calibration remains outside the regular historical provenance profiles.

Archive-reading Python tools default to ignored `downloads/turnout/feed-archive/`. Optional `POLLING_ANALYSER_FEED_ARCHIVE` and `POLLING_ANALYSER_FEED_DOWNLOADS` select external archive/incoming directories once across elections; they do not activate the turnout model or select its prior.

The portable `TurnoutModelTests` checks numerical Python/C++ agreement using fictional counts, finalisation, count preservation, joint/exceptional draws, the production composition cache and source-time history replacement and filtering. `test_turnout_prepare_live` checks the standalone input-preparation boundary without a private archive. Neither test runs the wxWidgets application. A GUI rebuild and snapshot run are required to verify the complete forecast and its runtime.

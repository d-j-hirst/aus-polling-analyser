# Live turnout model

This document explains how the live forecast estimates votes still to be counted, how those estimates affect its uncertainty, and how to supply the required election inputs.

The model combines pre-election expectations with reported first-preference counts and the history of counting received so far. It supplies one vote-count account to the live forecast: booth estimates, district totals, completion percentages and simulated additions all use that account. Party composition and preference rechecking are prepared separately and then applied to those counts.

## Combining seat forecasts into election totals

The live forecast gradually replaces historical seat-size weights with its
estimated current sizes when reporting national and regional vote shares. This
answers an accounting question: how many voters does each already-calculated
seat share represent? It changes aggregate FP and TPP means and uncertainty,
while retaining the calculated seat shares, preferences, winners and seat win
probabilities. Ordinary forecasts, odds calibrations and the pre-count live
baseline continue to use their existing historical weights and reconciliation.

The transition is a pragmatic compatibility measure. Requiring ordinary
forecasts to share a turnout model would also change how national/regional
polling targets are reconciled with seats and require inputs appropriate to
each forecast date. This live-only correction avoids introducing those broader
changes or additional routine enrolment-data maintenance. Historical booth and
category counts remain the reference for swing comparisons.

Let `c` be accepted published FP votes divided by the prepared mean expected
final formal votes across the election. This is counting coverage, not model
confidence or TCP completion. Every iteration for one snapshot uses the same
`c`; sampled turnout, count revisions and a change of TCP pairing do not alter
it. Define `x = clamp(c / 0.5, 0, 1)` and `a = x²(3 − 2x)`. The curve is smooth
at both endpoints: new weighting is negligible for a first tiny booth and
reaches full weight without a step at 50% counted.

| FP coverage | Current-turnout weighting `a` |
| --- | --- |
| 0% | 0% |
| 10% | 10.4% |
| 25% | 50% |
| 40% | 89.6% |
| 50% or more | 100% |

The 50% endpoint is an operating assumption, not a fitted relationship between
coverage and turnout accuracy. Each source recomputes its transition without
snapshot memory; a corrected count or revised expected total can move it back.
These maintained choices are defined in `LiveAggregation.h`.

For seat `i`, let `H_i` be its historical formal count and `N_i` its estimated
final current count. Its relative weight is
`(1 − a) H_i / sum(H) + a N_i / sum(N)`. Normalising before blending prevents a
larger overall election from receiving more influence than `a` specifies.
FP uses formal ballots; TPP uses continuing ballots after any OPV exhaustion.
Current totals come from the same simulated turnout/revision account as the
seat shares, without a second random draw. Unreported districts retain their
estimated final sizes; an explicitly inactive contest has zero current size.

The national histogram, regional histograms and legacy FP/Coalition/TPP summary
distributions use this policy. A region takes its subset of the election-wide
weights rather than starting a separate transition. No resulting aggregate is
fed back into seat calculations. Raw swing, the internal projected vote account,
count recovery and published counting progress keep their existing meanings.

The Joint Centre and its aggregate covariance diagnostics use the same policy
with prepared mean sizes. They still precede scenario count/revision draws and
count constraints, so they need not equal the final simulation mean or median.
The analysis archive records `tpp_integration.aggregation_weighting`: method
`live_turnout_phase_in_v1`, the curve, FP coverage, full-weight coverage,
transition fraction and normalised mean FP/TPP weights for every seat. Older
archives retain their original recorded values.

`tests/LiveAggregationTests.cpp` is a standalone, GUI-free component test. It
checks zero-count compatibility, the transition, independent normalisation,
same-scenario sizes, OPV denominators, inactive contests, regional accounting
and invalid inputs. It does not calculate forecasts or fit models.

From the repository root in a Visual Studio Developer Command Prompt, compile
and run only that component test:

```bat
cl /nologo /std:c++20 /EHsc /W3 /WX /wd4244 /wd4267 /I. tests\LiveAggregationTests.cpp /Focli-build\LiveAggregationTests.obj /Fecli-build\LiveAggregationTests.exe
cli-build\LiveAggregationTests.exe
```

## Final federal district totals

The federal reader uses published final district FP, TCP and Labor/Coalition
TPP totals instead of modelling a result that is already known. This matters
particularly in non-classic seats, where a notional TPP scrutiny can be published
without a booth-by-booth Labor/Coalition breakdown. TCP still identifies the
actual finalists; notional TPP does not replace them with the major parties.

Two kinds of publication qualify:

| Source evidence | Treatment |
| --- | --- |
| AEC `Results Phase="FinalResults"` | Use validated final district FP/TCP and any positive published TPP. |
| Declared non-classic contest with positive TPP whose total equals both its FP and TCP totals | Treat that district's reconciled FP/TCP/TPP account as final. Other districts remain live. |
| Positive classic TPP, a zero TPP placeholder, or a winner declaration alone | Retain normal live estimation; these do not establish completion. |

The AEC documents `FinalResults` as the phase in which results will no longer
change, and describes `Declared` as a winner declaration rather than a general
count-completion flag. See sections 9.7 and 9.9 of its
[Media Feed User Guide](https://www.aec.gov.au/media/mediafeed/files/media-feed-user-guide-v4.pdf).
The additional declared non-classic rule is an operational interpretation of a
completed, separately published TPP scrutiny, guarded by exact district accounting.
No elapsed-time or apparent percentage-counted threshold declares a result final.

Final summaries must have nonnegative candidate counts, a positive formal total
within enrolment, exactly two TCP finalists whose own primaries fit their TCP,
and matching FP/TCP/available-TPP totals. An inconsistent `FinalResults` account
stops the update instead of silently substituting estimates. An incomplete TPP
publication in an otherwise live feed does not finalise that district.

These totals are retained separately from recovered booth/category records.
The booth records remain available for inspection; the program does not invent
a booth allocation to reconcile a discrepancy with the final district summary.
At the forecast boundary the final account fixes FP shares, TCP finalists and
winner, published TPP, vote-count weights and progress. It receives no further
turnout additions, signed count revisions, preference-recheck draws or prior
shrinkage. Aggregate TPP uncertainty excludes the final district's conversion
error. Other districts and pre-election simulations retain their usual paths.
FP-moment preparation also suppresses variation for final districts. Their
final summaries can group unmodelled minor candidates into Others while retained
booth diagnostics still identify those candidates separately; these identities
are not instructions to vary an already final count.
Seat and booth input counts also retain matching live candidate identities.
An unconfigured final TCP candidate can occupy a generic emerging-party slot
in the simulator-facing final summary without replacing that candidate's
identity in the booth evidence. Final projected/count accounts and outcomes
use the authoritative summary; booth comparisons use the original identities.
Private analysis archives expose each qualifying seat's `final_result` with its
`basis` and mapped `fp`, `tcp` and `tpp` vote counts.

The inspected final Detailed-Light captures for federal 2013, 2016, 2019, 2022
and 2025 all contain complete `TwoPartyPreferred` district totals and the
`FinalResults` phase. Their early captures contain zero placeholders and then
progressive classic TPP counts; for example, 2016 already has a positive TPP at
18:12 on election night. Non-classic TPP can also be partial: Melbourne's
31 July 2016 capture has 12,809 TPP votes against 94,579 FP votes. These figures
are counts of ballots covered by each scrutiny, not vote shares. Older `CLR`
(Country Labor) candidates are recognised as Labor in classic-pair checks.
The final 2016 source's 13,541,101 formal votes give
49.6435038776% Labor TPP when aggregated directly. No
`DistributionOfPreferences` section was found in the inspected House records,
and the 6,230 stored feed archives contain no separate distribution XML file.
The AEC publishes full distributions separately; these are not part of this
reader's supported Detailed-Light XML format. Its
[counting FAQ](https://www.aec.gov.au/faqs/counting.htm) explains that the full
distribution starts after all ballot papers have been counted.

`tests/AecFinalResultsTests.cpp` exercises progressive and final parsing,
district finality after recovery, replaying an earlier source, rejected final
account contradictions and fixed turnout/revision draws. Its optional workspace
argument additionally checks local preloads against extracted final feed files
under `cli-build/federal-final-counts/<election>-final.xml`; these private inputs
are unnecessary for the synthetic tests. No forecast or Stan fit is run.

To compile just this standalone component test in a Visual Studio Developer
Command Prompt, set `AEC_TEST_XML` to the TinyXML2 source directory used by the
project, then run from the repository root:

```bat
set "AEC_TEST_XML=C:\C++ Libraries\TinyXml2\tinyxml2-master"
if not exist cli-build\aec-final-tests mkdir cli-build\aec-final-tests
cl /nologo /std:c++20 /O1 /EHsc /MT /W3 /wd4244 /wd4267 /Gy /I. /I"%AEC_TEST_XML%" ^
  tests\AecFinalResultsTests.cpp ElectionData.cpp LiveInputAdapter.cpp LiveTurnout.cpp ^
  TurnoutModelIO.cpp TurnoutModel.cpp LiveInputRecovery.cpp LiveInputRecoveryIO.cpp ^
  LiveInputHistory.cpp Date.cpp General.cpp Log.cpp "%AEC_TEST_XML%\tinyxml2.cpp" ^
  /Focli-build\aec-final-tests\ /Fecli-build\aec-final-tests\AecFinalResultsTests.exe /link /OPT:REF
cli-build\aec-final-tests\AecFinalResultsTests.exe
```

When filtering redundant captures, retain changes to finalisation phase,
declaration status or published TPP counts: they are meaningful forecast inputs,
even if the ordinary booth counts are unchanged.

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

Each election requires `forecasts/<election>/live-inputs/turnout-prior.json`. The live run selects this path using its own election code. A missing prior or incompatible mapping produces an input error. Election timetables, category definitions and service exceptions are data in this input. WA's explicitly mixed early/day places additionally select the two-batch counting rule described in `docs/live-wa-replay.md`.

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

Compulsory-preferential TCP must also accommodate each finalist's own primary votes. Before checking this, the model allows all ballots still awaiting preferences to belong to that finalist, then applies the same `max(10 votes, 5% of FP)` allowance. For example, 1,000 FP and 400 TCP votes leave 600 ballots unprocessed: a finalist's smaller TCP is normal within that gap. But with 1,000 FP and 1,000 TCP, a finalist with 500 primaries and only 400 TCP votes has a clear contradiction. The whole TCP record needs recovery; individual candidate entries are never patched to make it fit.

Where a feed explicitly identifies count rounds, a usable FP recheck takes priority over incompatible preliminary TCP. ECQ's selected official rounds provide this information; feeds and older caches without it retain an unspecified stage. A substantially smaller unfinished recheck still retains the existing hold until it catches up or a reconciled official pair confirms the correction. With no clear stage advantage, a candidate-level contradiction can hold the previous coherent FP/TCP pair even if the total FP count has not fallen. Any independently restored TCP must fit the effective FP, including its finalist primaries; otherwise TCP is unavailable and FP-based estimation supplies uncertain preferences.

An earlier compatible FP/TCP pair can remain evidence about preference flow after its TCP becomes unavailable. Its source times and measured vectors remain historical; it supplies no counted lower bound, completion or fresh progress. For a comparable-sized FP recheck, corrected primaries can replace the old composition while the measured flow retains its existing sample-size weight. A cohort that is no longer contained in current FP cannot be subtracted as a batch, and a changed finalist pairing cannot borrow the old pair's flow.

The private history at `forecasts/<election>/live-snapshots/accepted-counts/`
preserves accepted candidate counts so that later updates can recover compatible
evidence and distinguish new counting from corrections. It starts empty for a
real election. Matching uses election, district, booth/category role and candidate
identities, rather than array positions. Earlier forecast exports and aggregate
turnout history cannot supply accepted candidate-count fallbacks.

The authoritative store is `change-history-v1/` inside that directory. It records
timestamped changes separately for FP, TCP, retained preference pairs and other
record fields, grouped by district. Its manifest preserves capture metadata and
record ordering. A small `accepted-history-format` marker beside the directory
identifies this as the accepted record, rather than a disposable cache. Keep and
back up this directory and marker; they are needed even when old complete
accepted-count JSON files are removed. Original electoral-commission feed
archives remain separate inputs to each replay.

On first use, an existing valid `change-index-v1/` is adopted by copying its compact
district files into the authoritative directory. The old index and complete JSON
files are left in place. Missing or revised legacy captures before the requested
time are imported once. If there is no old index, earlier complete snapshots are
read in one traversal. Later legacy captures awaiting import are recorded in the
manifest's `legacy_sources` map and imported as the replay reaches them. An empty
map means the authoritative store no longer depends on those old complete files.
This migration does not generate another set of complete election snapshots.

An unchanged vote vector can still have a newer observation time. The history stores
that time as a reference to the source clock where applicable, preserving fixed
older clocks for restored counts and retained pairs. Queries reconstruct original
observations and apply the same compatibility, precedence and stopping rules as
the source-file reader. They retain unavailable/zero distinctions, candidate
identities, recheck authority, completion and transfers. Strict source-time cutoffs
exclude same-time and future observations during replay. Whole-district rollback
reconstructs a coherent historical account for that district at one capture time;
it does not combine incompatible records from different captures.

Numeric FP histories use a narrower query: read each distinct FP state once,
without rebuilding its TCP, preference metadata or original array position.
Lightweight capture clocks are retained even within an unchanged run. This matters
when a restored observation has an older clock between fresh observations: keeping
only the run's endpoints could lose a zero or a restoration in the final ordering.
For example, a restored 300-vote observation between two fresh zeros, ordered by
original observation time, must still yield `0, 300, 0`.
The numeric reader preserves the original source reader's newest-capture precedence
for conflicting versions of one observation time, then removes consecutive equal
totals. Zero withdrawals, later returns and unavailable records remain distinct.
The general compatibility searches retain their existing per-capture procedure.

Successful runs update the history directly from the accepted in-memory state.
An identical rerun reads and writes no complete accepted-count file and publishes
no new history generation. Equality checks cover accepted fields, observation
clocks, source metadata and record order, not just the raw feed fingerprint:
corrected recovery rules can change the accepted state of an unchanged feed.
Inserting or replacing an earlier capture preserves the next capture's original
values. For example, inserting B between two captures of A creates A, B, A,
rather than carrying B forward. Every capture clock and change is retained,
including withdrawals, genuine zeros, unavailable counts and restorations.

Normal reads do not scan the old complete files. Removing a legacy file therefore
does not erase accepted history, and editing one does not replace authoritative
evidence. An intentional correction must be committed through the accepted-history
writer, normally by rerunning that source. Explicit history-repair operations can
remove a capture without later importing it again from an old file.

Deferred legacy imports use one batch when a replay jumps over multiple later
captures. Only the latest field values and one incoming full source are held for
comparison; changes are temporarily grouped on disk by district. Each changed
district is written once, then a single manifest publishes the batch. Sources at
or after the replay cutoff are not imported by that query; already stored later
history is retained but cannot influence an earlier replay.

District files are written before an atomic manifest replacement publishes them.
Unpublished files are ignored and checksums detect damaged history. A missing or
damaged authoritative store stops preparation with a diagnostic; it never silently
substitutes stale legacy full files. The old disposable-index reader retains its
source-file fallback for migration comparisons only. A bounded memory cache keeps
recently used district views up to the replay's timestamp; oversized views are read
without retention. Searches skip unavailable accounts only when that account is
required for a match; measured zeros and older available states remain eligible.
Failed forecasts cannot add accepted observations. `PALog.log` records recovery,
preference-history and commit times, legacy source reads, adoption and unchanged
commits. Existing full files are retained for checkpoint comparison; this change
does not automatically delete them.

`LiveInputHistoryTests` checks exact reconstruction, observation-time matching,
incremental changes, batched catch-up, backfills, missing records, replacements,
deletions, backward replay, interrupted publication, restart reuse, repair and
cache eviction. The bulk test also checks one district write per batch rather
than per capture. `HistoryReadMode::SourceFiles`
retains the original reader for equivalence checks without running a forecast.

Fresh counting history omits restored or unavailable records. A category-group observation is also omitted if any of its available services needed recovery, so restored counts do not manufacture counting events or pauses locally or in measurements shared across districts. Restored FP retains its earlier validated completion status; an unavailable count cannot acquire completion from the rejected source. Accepted-count and turnout-history writes are queued during preparation and performed only after a successful main calculation.

The **Results** tab retains a non-modal warning panel, with one row per issue type and a worst example measured in votes. Selecting a row opens affected-record details, including rejected/effective counts and any restored source time. Routine incomplete counting is hidden behind an informational checkbox. Private snapshot metadata contains compact summaries for **Live Booths**; the analysis sidecar contains the detailed recovery records. Count history is never embedded in `.pol2` projects.

ECQ normally withdraws its indicative TCP round while official rechecks replace it. Once the district has official FP records and no indicative TCP records, missing TCP restored from compatible history does not generate a withdrawal warning. This source-specific publication status suppresses only that notice: the restored counts and their original times are unchanged, incompatibilities and malformed counts still undergo normal recovery, and other feed readers retain their existing warnings.

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
- `schedule`: local-source-clock `postal_deadline`, `receipt_deadline` and `poll_close`, optional category `processing_timetables`, and whether delayed reporting should reduce unreported PPVC expectations. Dates use `YYYY-MM-DDTHH:MM:SS`; unavailable dates are null. The legacy `receipt_deadline` field now controls postal processing only. It never supplies a non-postal processing date. See the independent timetables below.
- Optional `sources`: source identities and hashes documenting the pre-election inputs.
- Optional `inactive_contests`: named postponed contests with `status: "postponed"` and a reason. These remain seats in the ordinary forecast, but have no turnout units or weight in counted completion. A feed can omit them or report zero votes; any reported FP or TCP votes require reviewing this configuration before proceeding.

The command draws the distribution without fitting a model or reading retrospective reports, prototype allocations or archived snapshots. Its output records configuration, maintained-parameter and preparation-code hashes. Regeneration is appropriate when the election's input mapping or pre-election evidence changes; live observations never overwrite the prior.

The live source must also supply a timestamp for measuring counting activity. MediaFeed sources retain their `Created` time; Queensland retains `generationDateTime`, including fractional seconds. Zoned timestamps require normalization to the configured local clock before use. A source without a published time needs an explicitly retained collector timestamp; a saved forecast's old display date is not a substitute.

Victorian results retain the VEC's combined Early category rather than inventing individual early-voting centres. The loader also preserves the separately reported `MarkedAsVoted` candidate counts; historical `Marked As Voted Votes` records use the same vote type. These counts enter the normal declaration accounting and remain distinct from Provisional votes.

First preferences can arrive before their preferences have been counted. The live calculation separates those already reported voters from voters whose first preferences are still outstanding. Where a TCP total exactly matches a compatible, earlier accepted FP batch, that earlier candidate vector identifies the primary composition of the preference batch. Its observed preference flow adjusts the configured party-specific flows; the adjustment is then applied to the additional FP candidates, rather than copying the earlier batch's TCP share. For example, a batch with many Greens primaries may have a high Labor TCP share even without unusually strong preference flows. A later batch with more Liberal primaries keeps those Liberal votes when its missing preferences are estimated.

Only successful, strictly earlier received feeds in the private accepted-count cache can supply a historical cohort. The cache retains the latest identified pair when the current FP or TCP moves beyond it, including pairs assembled from two different received sources. This stores integer observations and their original times, not forecast estimates. Older cache entries without that field remain readable; their aligned FP/TCP accounts and matches to the current TCP remain available. Election, district, service and candidate identities must agree; neither finalist may receive fewer TCP votes than its corresponding primary count. Every candidate's earlier FP count must fit within the current FP vector before it can be subtracted as a cohort. An exact total match is evidence of a common batch, rather than proof of individual ballot identity. Downward candidate revisions or a changed candidate list prevent that subtraction. Optional preferential exhaustion can also reduce TCP totals, so the current matching rule is restricted to compulsory-preferential counts.

Preference-flow adjustments use log odds: `log(p / (1 - p))` for a flow fraction `p`. A local batch's adjustment receives weight `N / (N + 500)`, where `N` is its non-finalist primary count; the rest retains the existing historical and pooled flow estimate. Non-classic contests use their existing seat preference assumption in place of Labor/Coalition party flows. The latest compatible matched FP/TCP pair remains useful after another TCP batch arrives: subtract its FP vector from the current FP vector, then use that residual primary mix and calibrated flows to estimate the smaller number still awaiting preferences. The newer counted TCP remains fixed. Where no compatible contained cohort exists, estimate the unfinished portion directly from the current FP mix. Subtracting counted TCP from an estimated whole-category TCP would incorrectly assign no votes to a candidate whenever that estimate already lay below their counted total.

Previous-election booth flows normally supply their full comparison weight when estimating changes in preferences elsewhere. A nearly unanimous historical flow is unusually sensitive to small FP/TCP total discrepancies and becomes extreme on log odds. Its **extrapolation reliability** therefore stays at one through 92% to either finalist, then fades smoothly to zero at 98%. At a 95% historical flow, the comparison supplies half its usual evidence weight; the recorded 95% flow itself is not halved or capped. The same multiplier reduces both its contribution to the pooled estimate and its contribution to pooled confidence, so the remaining few booths cannot inherit the discounted record's confidence. Current counted TCP, the historical flow used for its own local prior, and current matched FP/TCP cohort measurements retain their existing treatment. Expected flows already account separately for the parties supplying preferences in each election.

Measure the historical flow after the existing small-total-discrepancy normalization, but before half-vote smoothing. For example, fictional FP counts of 600, 300 and 100 with TCP counts of 697 and 300 imply a 97% flow directly; normalizing the 997 TCP votes to 1,000 FP votes raises the inferred flow above 99%. That comparison remains measurable but supplies no extrapolation evidence. Between 92% and 98%, set `x = (max(p, 1 - p) - 0.92) / 0.06` and multiply evidence weight by `1 - 3x² + 2x³`; clamp `x` to zero or one outside that range. This gives a continuous value and slope at both limits. The thresholds are common safeguards, not election-specific settings or fitted claims about every high-flow booth.

The Results input-warning panel groups materially limited comparisons as **Historical preference evidence limited**. Selecting the row shows the previous FP/TCP figures, inferred transfer and retained extrapolation weight. Tiny genuine one-way preference pools stay quiet. Notice severity is the smaller of the previous/current non-finalist pools, multiplied by lost reliability, flow disagreement after accounting for the current party mix, and the normal seat-relevance modifier; a notice needs at least ten vote equivalents. This is a grouping/filtering measure, not an estimate of the forecast's vote change. The fade applies below that notice threshold too. Private analysis sidecars record each reduced comparison under the booth's `historical_preference_evidence`, alongside its existing historical counts.

For voters whose first preferences have not yet arrived, each declaration category starts with its historical primary composition, adjusted by movement measured in ordinary booths and the existing pooled category biases. Its own reported primaries then gradually replace that starting estimate. Ordinary polling locations include individually identified PPVCs; aggregated Early/PrePoll services remain declaration categories. The category's partial declaration batch does not also train its ordinary-booth starting movement. Existing new-candidate and missing-history fallbacks remain available where a historical primary comparison is absent.

Let `n` be the category's counted FP votes, `T` its expected FP total and `r = max(T - n, 0)` its expected additions. Absent, Postal and other declaration categories use observed-mix weight `min(n / T, 1)`. Aggregated Early and declaration PrePoll use `n / (n + 4r + 300)`: their initial batches can cover quite different areas, so the estimate adopts their composition more cautiously while much of the service remains outstanding. The factor 4 multiplies expected remaining votes; 300 is an additional vote-sized allowance for uncertain representation. Both weights are zero before any primaries arrive. A 45-vote batch in a service expected to contain 2,700 votes consequently receives about 1.7% weight under the first rule. Blend each candidate's share on natural log odds and then normalize the resulting shares to sum to one. Only estimated additions change; reported integers remain fixed.

When estimating current primary proportions, use `(candidate votes + 0.5) / (total FP + 0.5 × number of candidates)`. This **half-vote smoothing** keeps genuine observed zeros finite without assigning a fixed percentage floor: a zero among four votes on a four-candidate ballot estimates 8.3%, but a zero among 1,000 estimates only 0.05%. It supplies no fictitious counted votes and includes only candidates actually contesting that count. Primary-movement evidence is also capped at the votes actually reported, before applying the booth's seat-relevance discount. A completed ordinary booth supplies its usual observed-size evidence; a tiny partial record cannot receive its much larger historical booth's weight. Completion retains its separate expected-size denominator.

In compulsory-preferential counting, new voters' TPP is calculated from this same future FP mix and configured preference flows. The future-flow prior includes historical and seat/region/election evidence, with the category's separate local preference deviation excluded. Its latest compatible matched cohort adjusts that prior once using the `N / (N + 500)` weight described above. Already-counted voters awaiting preferences keep their own cohort/residual calculation. Non-classic TCP with observed finalists uses its existing seat preference assumption with the shared future FP mix; unreported non-classic pairs retain their existing comparison methods. The Postal adjustment is −0.08 in natural log odds for Coalition primaries on the observed side of the FP blend, fading with that side's weight. It needs no second independent Postal TPP shift. Optional-preferential pair projection retains its existing exhaustion-aware boundary and continuing-vote extrapolation; it was not calibrated by these compulsory-preferential experiments.

When a TCP contest shares only one finalist with the previous election, reporting booths can supply a movement for that shared finalist. This historical comparison competes with the estimate based on projected primaries and observed preference flows. Its confidence is `0.1 × sqrt(min(W / E, 1))`, where `W` is the existing prior-vote weight of compatible reporting booths after the seat-relevance discount, and `E` is the projected FP total of **all** locations in the seat. Unreported locations and locations without a comparable old pair contribute to `E` but supply no movement. For example, one 200-vote booth with a 300-vote prior comparison in an 80,000-vote seat gives confidence about 0.0061. The 0.1 cap retains the existing discount for a changed opponent; this confidence is a blending input, not a probability that the estimate is correct. Counted votes stay fixed.

The common parameters and numerical component are maintained directly in `LiveNewVotes.h`; matched-cohort evidence, its 500-vote weight and the historical-flow reliability thresholds are in `LivePartialCount.h`. Neither requires private replay outputs, election-specific code or activation variables. Preparation shares one compact, immutable starting FP composition per declaration service between scenarios. Each variability sample projects its own future composition, which FP and pair counts consume together. Analysis sidecars record the starting composition, future composition and observed weight under each declaration booth's `new_vote_composition` for subsequent inspection.

### Historical swings and absolute primary evidence

Historical swing and current FP composition answer different questions. A booth transferred into a new district can be a poor comparison with that district's previous result while still providing useful evidence of a candidate's current vote. The forecast therefore compares two estimates of the outstanding FP votes: the existing historical projection, and an estimate based on the current absolute shares. It retains counted votes exactly.

The absolute estimate uses reported ordinary locations as its starting composition, falling back to the available district counts when none have reported. Separate vote-type/booth-type groups blend their observations into that composition. Small aggregate Early/PrePoll batches retain the slower weighting above; other groups use their counted fraction. Half-vote smoothing keeps genuine observed zeros usable in logarithms without altering the published counts.

Let `C` be FP counted divided by expected final FP, and `H` the candidate's historically comparable counted votes divided by that same expected total, capped by the district's historical FP confidence. Only locations with a usable swing for that candidate contribute to `H`, with the existing transferred-location relevance discount. The absolute method's comparison strength is `A = 0.5 C`. Its weight is `G A^4 / (A^4 + H^4)`, where `G = N/(N+500) × B/(B+1)`, `N` is counted FP and `B = N² / sum(n_i²)` measures the number of effectively represented reporting locations. For example, ten equally sized reported booths give `B = 10`; one dominant batch gives a value nearer one. The gate `G` prevents a few small observations from abruptly replacing the starting projection. No observations means no change.

The two estimates are combined using a weighted average of their log proportions of the remaining votes, then normalised across candidates. Their weights are candidate-specific: a new independent does not inherit another party's historical comparability. With full comparable history (`H = C`), the absolute weight before the gate is about 5.9%; with `H = 0.1 C`, it is about 99.8%. The methods describe overlapping votes, so combining them does not create an extra independent sample.

Preparation balances the resulting seat estimate across the future pools of its locations. This preserves both each location's turnout total and the combined candidate totals. The existing declaration preference calculation then uses the adjusted future FP composition. Accepted FP/TCP cohorts and already-counted candidate votes stay fixed.

FP uncertainty retains normal local variation on the bounded, transformed share scale. Disagreement between the two methods is a separate choice: select the absolute estimate with probability `w`, or the historical estimate with probability `1-w`. Let `d` be absolute minus historical in whole-seat natural log odds. The disagreement contribution is `(1-w)d` when absolute is selected and `-wd` when historical is selected. Its average is zero and its variance is `w(1-w)d²`, as before. For example, with `w = 0.8`, the contribution is `+0.2d` in 80% of draws and `-0.8d` in the other 20%. This component stays within the gap between the descriptions; it cannot create an extreme result beyond both simply because the methods disagree. Normal local uncertainty still varies around either choice, so the combined forecast remains continuous and can extend beyond the two estimates. The rule applies candidate by candidate, using each one's existing evidence weight.

An early uncertainty allowance accounts for an unrepresentative set of reporting locations: standard deviation `exp(-C/0.1) / sqrt(B+1)` in log proportions of the remaining pool. This allowance fades as counting progresses; it does not leave a fixed percentage-point uncertainty after all votes are counted. The local normal variation, this allowance, the two-estimate choice and shared FP category variation are sampled without a count-progress phase-in. Their measured width instead determines how much local evidence enters the main forecast. TPP and TCP retain their existing progress weights.

The FP handoff compares the variance of the live baseline with the variance of the count-based estimate, both in natural log odds. Log odds are `log(p/(1-p))`, where `p` is a vote fraction; this expresses changes relative to how much support a candidate already has and keeps transformed draws inside the possible share range. Let `Vb` be baseline variance and `Vc` count-estimate variance. The count weight is `W = G Vb / (Vc + G Vb)`. For example, at `G = 0.5`, count uncertainty four times as large as baseline uncertainty gives `W = 1/9`; a count variance one tenth as large gives `W = 5/6`. An individual draw becomes `(1-W) * conditional-prior log odds + W * count log odds`. This is a weighting of two descriptions, not a second uncertainty distribution added to the prior. The historical-versus-absolute weight described above chooses the count estimate's centre; `W` determines its influence on the main simulator.

The conditional prior is the original correlated simulation draw with the prepared election-wide and regional FP movement applied. These accepted shared movements are frozen during preparation: recalculating them from random local draws would feed the same local error into the prior as well. The count estimate already contains the shared movement. Weighting their local difference therefore preserves movement supported across districts even when a particular district still has weak evidence. The previous FP baseline-confidence blend, local variability phase-in and residual-prior replacement weight are replaced by this one handoff. Independent pooling itself is unchanged.

Baseline moments are recorded from actual baseline simulation draws before histogram rounding, including zero shares when a candidate is absent. During preparation, 256 reproducible samples measure the same finite outstanding-vote response used by scenarios, including local, category and endpoint variation. Turnout is held at its prepared mean for this composition measurement; scenario turnout draws remain separate. These are initial common settings, not a claim of fully calibrated final-forecast intervals. Candidate groups retain share covariances; a grouped Others estimate uses a local linear conversion of its summed share variance to log-odds variance, while individual candidates use their directly measured log-odds variance.

After blending, candidate shares are normalized subject to each candidate group's counted lower bound. Usually the bounds require no adjustment. If a target would take away recorded votes, that group keeps its counted allocation and only the outstanding total is redistributed among the other groups. A fully counted account determines the entire result. Liberal and National counts are integrated separately, then recombined for the existing preference-allocation path; the simulator's independent and generic-candidate identities use the same partition for counts and distribution moments.

`LiveFpEvidence.h` maintains the historical/absolute parameters and numerical calculation directly. `LiveFpIntegration.h` maintains the precision handoff and finite-count constraints; `LiveFpPreparation.cpp` connects them to the live accounts. Only compact moments are retained, shared between scenarios; baseline moments are regenerated rather than serialized in `.pol2` files. Analysis sidecars include `fp_evidence` with historical and absolute remaining estimates, candidate weights, reporting breadth and combined composition. `disagreement_distribution` identifies `two_estimate_endpoints`; `disagreement_log_odds_gap` gives `d`, and `normal_additional_log_odds_variance` is the early normal allowance. The retained `additional_log_odds_variance` includes the separately drawn disagreement. `fp_integration` records baseline/count moments, accepted shared movement, the evidence gate and individual-candidate count weights. Simulator candidate grouping can change those weights for anonymous groups. No private archive is required to apply the settings to another election's feed.

VEC result captures can omit ordinary booths before any votes report. The loader retains their preloaded identities and leaves their measured vote maps empty; absence from one capture does not establish a closure or a measured final zero.

### Greens preferences between Labor and centrist or independent candidates

The simulation also needs to estimate who reaches the final two when no measured
TCP has yet settled the contest. Its general non-classic preference calculation
assigns weights to the surviving candidates. Where Labor and a centrist or
independent candidate both survive, a separate rule divides their combined
allocation of Greens preferences. It applies when the configured historical
Labor TPP margin is below −5 percentage points or the incumbent is a non-major
candidate; elsewhere the general weights apply. Configured final-two preference
flows take precedence over this fallback.

Ordinary centrist parties receive a uniformly sampled **40–75%** of that combined
allocation. Genuine and emerging independents retain **50–90%**. These are broad
modelling assumptions, not fitted confidence intervals. Moderating the centrist
range addresses the overly favourable independent-style treatment of NXT in the
2016 diagnostic replay; it is not a general overhaul of preference allocation.
If Labor and NXT together receive 80% of continuing Greens preferences and the
sampled NXT split is 60%, NXT receives 48% of all continuing Greens preferences.
Other surviving candidates keep their existing allocations and exhaustion is
unchanged by this split. It can affect elimination order and seat win chances in
both pre-election and live simulations, including their supporting calibrations.

A seat can explicitly retain the independent-like range for a party-labelled
candidate with `sIndependentLikePreferences=CA`. A list accepts comma- or
semicolon-separated configured party codes, and omission means no exceptions.
The setting changes only this preference assumption: party identity, reporting,
primary-vote modelling and configured final-two flows retain their own rules.
Lacking a separately modelled national trend does not automatically establish
quasi-independent preference treatment. In particular, NXT remains a centrist
party in 2016 despite lacking its own national trend.

The Mayo settings for 2019, 2022, 2025 and 2028 explicitly list CA for Rebekha
Sharkie. Other CA candidates are not covered, and Mayo in 2016 has no exception.
The seat text file stores this choice, so the `.pol2` format is unchanged. Edits
to the exception lists invalidate the reusable live odds calibration and
baseline; worker copies and completed publications carry the corresponding
runtime cache key.

### Sharing independent movement between districts

The live baseline uses the same candidate configuration as the normal forecast. Once `sRunningParties` is populated, a defeated historical principal independent supplies a returning-candidate prior only with `bPreviousIndRunning=1`. An `IND` entry, including entries with asterisks for additional candidates, establishes ballot presence but does not identify the previous campaign. New named independents and generic newcomers use their separate existing models. Independent incumbents retain their existing recontest and retirement handling. Before the ballot is configured, an unconfirmed historical return remains possible under the existing declining recontest probability. Historical election counts remain available for electorate-level comparisons and preference calculations; this rule controls whether the old candidate is projected to run again.

An independent's own counted votes inform its local primary estimate normally. Sharing that movement with other districts needs more care: the independent modelling slot represents different campaigns, and a rise from 1% to 3% is less informative about a competitive independent elsewhere than a similar proportional movement in an established campaign.

`LiveNewVotes.h` gives each seat's independent movement a smooth source weight `s*s / (s*s + k*k)`, where `s` is its separately modelled principal independent's pre-count baseline median share and `k = 0.05`. A 1% baseline receives about 3.85% of the usual evidence weight; a 5% baseline receives half; a 20% baseline receives about 94.12%. A generic emerging-independent or possible recontest distribution supplies no comparable campaign baseline and therefore cannot train this pool. It still receives normal local count updates. These are source weights, not changes to counted votes, predicted shares or local confidence. The baseline share is fixed for the run, so an unrepresentative first booth cannot increase that seat's influence on other campaigns. The 5% scale is an initial common setting; it is not a cutoff for recognizing a genuinely strong newcomer locally.

The effective observed votes survive region and election aggregation. Both the pooled mean and its confidence use this candidate-specific evidence; confidence is effective independent evidence divided by the full expected formal count for the area. A missing independent comparison supplies zero evidence even when major parties have reported. If a different independent overtakes the baseline candidate, that candidate's local votes remain usable but it cannot borrow the older campaign's baseline size for pooling. Local seat and booth residuals continue to use their usual confidence. Declaration-vote contexts use the same rule on ordinary-booth evidence.

For independents, the simulator's shared movement comes directly from this accepted election and regional observation, rather than changes in summed projected independent votes. Those predictions already contain inherited pooled movement and therefore cannot supply a second observation of it. Other parties retain their existing projection-based summary. Private analysis sidecars expose `independent_pooling_baseline_share` for seats and `fp_pooling_evidence_votes` alongside each node's deviations and the shared movement in `fp_integration`.

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

`fNationalsCoalitionShare` is the expected National proportion of the combined Liberal and National primary vote, conditional on both parties standing. It remains in use when `sRunningParties` confirms both candidates. That known ballot takes precedence over retained `sCoalitionCandidates` probabilities; those are sampled only before the ballot is configured. If the ballot contains only one Coalition party, it receives the whole Coalition allocation and the conditional split is unused. Mildura's 2022 setting is `12808 / (12808 + 3686) = 0.7765247969`, using the two parties' FP counts in the last preceding joint contest, [Mildura 2006](../analysis/elections/results_2006vic.csv). The common intra-Coalition variability still applies. This supplies both candidates with a genuine baseline rather than inheriting a Nationals-only split from 2018.

For candidates represented by generic Others before counting, the FP integration borrows that source group's width if their baseline shares are all zero. This is identified from actual shares, rather than exact equality of calculated log-odds moments: many identical zero observations can leave floating-point variance residuals. Small positive distributions and precise positive priors keep their own widths. A configured Coalition split should provide the normal Liberal/National prior; this fallback covers separately observed candidates without inventing precision from their historical absence.

For this first configuration, both parties inherit the former combined party's non-classic preference estimates, including when a National candidate replaces a combined-Coalition preference target; these are maintained assumptions rather than newly fitted estimates. A project that retains a combined Coalition party can still use `NAT` as a feed alias, but that alias does not activate a second-party split or add the same projection target twice.

The fictional configuration in `tests/fixtures/turnout/live-inputs-example.json` demonstrates the format without requiring authorised feeds:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_live ../tests/fixtures/turnout/live-inputs-example.json --output ../downloads/turnout/example-prior.json
```

## Updating counts as results arrive

Each received source updates the same immutable prior afresh. Reported ordinary and single-report PPVC booths are approximately complete; declaration counts constrain routine additions from below. This stage preserves counted first-preference votes, allocates nonnegative additions, and keeps category and district totals consistent with their parent account. The separate signed-revision stage below allows corrections to those counts. Minor ordinary-booth rechecks do not create a staged-addition expectation. WA's mixed early/day places use their explicit two-batch treatment.

When every configured service in a district is complete or explicitly closed, its routine turnout estimate is exactly the counted total, even without a district-level finalisation flag. Signed count revisions remain a separate uncertainty. An open declaration service or unreported booth still allows additions. Missing service records do not establish completion.

Unreported matched PPVCs retain their own size expectations with conservative partial compensation for changes in other matched centres. Here **compensation** means that fewer votes than expected at reported centres can increase expectations at an unfinished centre, and vice versa. The coefficient is selected from the centre's mean starting expected count:

| Expected PPVC count | Compensation coefficient |
| --- | ---: |
| Below 2,000 | 0 |
| 2,000 to below 4,000 | 0.14 |
| 4,000 to below 8,000 | 1.17 |
| 8,000 or more | 0.63 |

These are coefficients of changes in log odds, rather than fractions of a vote-count shortfall or correlation measurements. The adjustment is weakened by the expected fraction of the other centres that has reported. The coefficients come from the conservative end of the Federal 2025 reliable-match comparison, excluding Brand's reporting consolidation; they have not been independently validated on another election. Size bands are discrete, while influence within a band changes continuously with the reported evidence. New or changed centres use their supplied starting weights; EAV services receive their separate small-service treatment. Redistribution effects beyond these input mappings are not inferred by this calculation.

### Empty public PPVCs and Divisional Offices

An empty physical account can eventually supply a normal return or remain unused.
`live-counts-16` separates the probability of a return from its size if it occurs.
This replaces the old residual small-size PPVC allowance for eligible units and
prevents unused Divisional Offices retaining thousands of expected votes.
The change concerns formal FP quantities; the existing party-composition model
allocates any additions between candidates and completes their preferences.

The following are working probabilities of a subsequent positive return **given
that the unit has never reported positive FP at the current source time**. Days
are calendar days since polling close, on the feed's source clock. They are
conservative assumptions informed by the available 2013, 2016 and 2019 federal
archives, with 2022 checked as a slower-reporting comparison, rather than precise
fitted frequencies. Repeated observations of one unit are not independent tests.

| Days after close | Established public PPVC | Divisional Office |
| ---: | ---: | ---: |
| 1 | 95% | 15% |
| 2 | 85% | 8% |
| 3 | 70% | 6% |
| 5 | 50% | 5% |
| 7 | 40% | 4% |
| 10 | 30% | 2% |
| 14 | 15% | 0.5% |
| 21 | 5% | 0.1% |
| 28 | 1% | 0.02% |

Probabilities interpolate continuously on log odds. After day 28 the odds halve
weekly, without a permanent floor. A public listing without a reliable historical
match divides those odds by 20: for example, 85% becomes about 22%. The optional
unit field `return_confirmed: true` gives a confirmed new public centre the
established-centre curve; mere presence in a preload does not confirm operation.
During the first day a smooth transition retains the initial account. With no
positive counts nationally, or no usable polling/source clock, it stays unchanged.

A public return retains its venue-specific usual size, including the existing
size compensation. Delay reduces its return probability rather than shrinking
the conditional return itself. Local size variation targets a conservative total
SD of 0.6 in natural log odds of votes/enrolment, adding only the variance missing
from its original outcomes. Earlier matched-centre size errors in the three
archives had SDs around 0.36–0.47 on that scale; the delayed subset was around
0.51. Size widening preserves the conditional mean within physical bounds.
Genuinely new centres retain their supplied earlier-election size weights.

An Office return blends over the first two days towards a nominal median of 40
formal votes and natural-log-odds SD 1.4. In a district with ample space, this
gives a mean near 107, 95th percentile near 400 and 99th near 1,040. Fifteen final
positive Office accounts still empty at 48 hours in the three archives ranged
from 2 to 378 votes, with geometric mean 43 and natural-log SD 1.41. These broad
round settings are deliberately conservative. Large temporary Office accounts
later reclassified into declarations are not extra final physical turnout.

The two choices run after declaration allocation. Disjoint shares of unused
enrolment bound simultaneous returns. A no-return outcome reduces turnout; it
does not increase another booth or declaration category. Rare branches contribute
to the stored means even when the small diagnostic sample misses them, and main
iterations retain continuous size tails. A declaration finishing outcome does
not prevent a possible late physical return.

For preparation samples, each unit permutes the same midpoint quantiles. Their
normal values are computed once per snapshot and looked up at the original
positions. This saves repeated inverse-distribution calculations without changing
the sample values, random streams or any probability/size parameters.

Positive physical accounts keep the ordinary counting rules. BLV, EAV, mixed
early/day centres, excluded or closed services and finalised districts are outside
this procedure. Accepted history through the current source time distinguishes
never-reported accounts from positive accounts later withdrawn to zero; the latter
retain their previous treatment. WA's mixed-place procedure remains separate.

Operationally this follows the existing `ppvc_reporting_decay` opt-in. Optional
`options.unreported_places` explicitly enables or disables the new procedure;
disabling it retains the legacy delay treatment. Frozen prior draws and project
formats are unchanged. The standalone Python comparison path retains its legacy
calculation; the new portable native tests exercise this additional live stage.

Saved `live_analysis.turnout.units[].unreported_place` records each return
probability, role, confirmation, conditional mean and added size spread.
`seats[].physical_count_diagnostic` compares counted physical FP with the frozen
pre-election physical mean and SD. `shortfall` is their signed difference in
formal votes; `total_if_no_more_physical` adds the current declaration mean to
physical votes already counted. Neither measure adjusts the forecast. Canberra
2016's deficit did precede Woden's 10,584-vote return, but other divisions retained
deficits without a missing centre, so the diagnostic is not an automatic repair.

A known closure contributes no expected additions or voter-behaviour evidence. In particular, its recorded zero does not suggest that other districts have more counting left. This exclusion also applies when all services in a category group are unavailable; a group with an open service still supplies progress evidence. An open service that has reported zero remains evidence that counting has not started.

Turnout updating compares two explanations for a discrepancy between the expected district total and the sum of its service estimates. **Category transfer** restores the expected total by adjusting unfinished categories. **Turnout change** retains their own estimates and accepts a different district total. A completed booth contributes its measured count in both explanations. All services from a prior draw remain together, preserving shared turnout uncertainty. Existing bounded conditioning excludes estimates below received votes.

Historical groups containing both physical booths and declaration services are split using the supplied service weights. Their remaining votes are estimated separately. For example, a combined group might initially contain 18,000 expected booth votes and 300 expected provisional votes. If the booths finish at 16,000, increasing provisional votes by 2,000 is an extreme change; turnout change receives almost all the weight. If substantial early or postal votes remain, a much smaller proportional adjustment across those categories may instead be plausible. A partial count conditions that service above votes already received; completion removes its transfer capacity.

The transfer calculation is category-agnostic. Physical booths in the same kind/group form one category; declaration services use their explicit category identities. The gap is allocated once across all eligible remaining categories, using a common displacement in natural log odds of additions versus available electorate, multiplied by each category's response variance. The starting size spread comes from its prior outcomes. Its precision is combined with a common 0.25 log-odds transfer restraint, then floored at 0.20: `response_sd = max(0.20, 1 / sqrt(1 / max(0.20, prior_sd)^2 + 1 / 0.25^2))`. This restraint applies only to transfer, preserving the original size uncertainty. Otherwise an uncertain small service could absorb much more of a shortfall than a large, better-known category. The transfer cost is the remaining-vote-weighted mean squared displacement in units of the response spread. Small or nearly finished categories therefore have little capacity; subdividing a category into more booths does not increase its influence.

The competing turnout cost uses the log ratio of the own-estimate total to the expected total, divided by the prior log-total spread with a 0.15 floor. Starting odds favour transfer 3:1. Its score declines as `exp(-transfer_cost / 2)`; the turnout score follows the relative density of a Student-t distribution with four degrees of freedom. This heavier tail allows exceptional overall changes instead of forcing exceptional changes into unfinished categories. These are initial response settings, not coefficients fitted to election outcomes or additions to the original prior variance. In a fictional 3,000-vote district with a 1,000-vote unfinished category and 10,000 enrolled electors, required category increases of 10%, 25%, 60% and 120% give approximately 73%, 61%, 17% and 0.1% transfer weight. The percentages depend on the count account and its uncertainty; they are not universal thresholds.

Each district samples one explanation per joint preparation outcome. Fixed, independently permuted midpoint quantiles make repeated snapshots reproducible. Main simulation draws reuse those selected rows, retaining both explanations rather than replacing every outcome with their weighted-average count. Small mixture probabilities may not be represented by a finite preparation ensemble; diagnostics report both the average calculated probability and the fraction actually selected. This choice uses no additional snapshot history.

An optional nonempty `transfer_exclusion_reason` on a prepared service excludes it from this transfer calculation while retaining its own size distribution and actual votes. Set it on each affected service to exclude a category, for example one introduced without a comparable baseline. Absent fields default to normal eligibility. A category with no positive starting size is ineligible; an unmatched booth name with a positive starting size is not automatically excluded. No category names are excluded by default. Synthetic SA sizes retain the uncertainty in their supplied joint prior draws.

Turnout can move above or below its original formal-total estimate. Independently conditioned estimates that would exhaust enrolment are reduced in their additions only, reserving 1% of the still-uncounted electorate for numerical headroom in the late-count model. This physical guard is separate from the choice of explanation and never removes counted votes. Version-6 and version-7 frozen priors remain readable by the version-8 updater; no input regeneration or `.pol2` change is required. Snapshot diagnostics include `turnout_explanation_parameters`, district `category_transfer_probability` and `category_transfer_selected_fraction`, and service exclusion reasons.

Declaration-only groups still retain their supported aggregate distribution and internal allocation. Progress can gradually release an excessive allocation within such a group. Mixed booth/declaration groups already use service estimates, so they are excluded from this allocation-release calculation and its shared-capacity adjustment. Actual service counting activity, receipt deadlines and late-batch uncertainty continue to apply to them.

The model keeps two kinds of late-count uncertainty separate. Routine expected additions recede as counting slows and the applicable category processing timetable passes. Exceptional additions remain possible with a low, gradually declining probability. There is also a district-wide outcome with exactly no new first-preference votes. Completion flags make that outcome certain; quietness alone does not.

### Independent category processing timetables

Processing dates describe when counting is usually approaching completion, rather than when electors must return ballots. Absent and declaration pre-poll (`PrePoll`, including early-absent votes) default to 14 calendar days after polling closes; Provisional defaults to 18 days. These are working modelling assumptions. For a future election, override them using the previous comparable election's overall processing progression. Record any retrospective calibration explicitly. Physical ordinary and early-voting booths do not use these declaration timetables.

For example, an election's `schedule` can contain:

```json
"processing_timetables": {
  "Postal": {"days_after_poll_close": 13, "transition_days": 2},
  "Absent": {"days_after_poll_close": 16, "transition_days": 2},
  "Provisional": {"days_after_poll_close": 18, "transition_days": 2}
}
```

The configured day is the centre of a gradual transition, not a cutoff or a probability that counting is complete. With no recent activity, the routine remaining allowance is halved at that date. With the default two-day logistic scale, it retains about 73% two days beforehand and 27% two days afterwards. These proportions change the odds of remaining additions relative to unused enrolment; they are not percentages of ballots already counted. Recent local activity can delay the reduction and retain more of the allowance. Exceptional-batch probability fades separately over fourteen further days.

The same category date and width govern when counting activity becomes less representative of other districts and when small batches support a finishing interpretation. Thus a postal receipt date cannot make absent or provisional activity look unusually late. Shared observations are still compared within their own category. Unknown categories have no timetable unless explicitly configured; a null category entry disables its timetable. Without `poll_close`, all relative timetables are inactive. Those accounts still use observed activity and original-size evidence.

Without an explicit Postal entry, postal processing preserves its existing receipt-based 48-counting-hour allowance, 12-hour transition and Sunday-adjusted clock. An explicit entry instead uses calendar days after polling closes, just like the other categories. Its gradual processing phase also moderates quietness-based support for little or no remaining counting: before processing is mature, a quiet account may still be awaiting a large batch. Specifically, the local quietness weight and the additional positive-finishing support are multiplied by the processing phase, which is one half at the configured centre. Original-size evidence is unchanged. WA 2025 uses the illustrative Postal setting above; it is a retrospective replay assumption, not a general default.

The receipt-window safeguard and the size of the small positive postal remainder remain tied to receipt timing, independently of the processing timetable. A null Postal entry disables its processing clock without disabling those receipt safeguards. The old `receipt_deadline` field takes precedence over `postal_deadline` when resolving the receipt reference, as before. Older frozen turnout input files remain readable: their receipt reference is interpreted as postal-only, and the non-postal defaults apply where polling close is available. No `.pol2` migration is needed.

The `declaration_schedule.categories` diagnostic lists the resolved dates, widths and clocks. Each unfinished account in `late_components` reports its own processing phase, routine-size factor and exceptional-batch survival multiplier, replacing the former election-wide phase.

Each unfinished declaration category combines previous expectations, small additions and exceptional late batches. Their probability weights change continuously with the evidence. They are **unconditional weights**: together they sum to one minus the district's no-addition probability. For example, if no additions have probability 60%, that category's other three weights together sum to 40%. When those other outcomes average 100 additional votes, its overall expected addition is 40 votes. Sampling within those outcomes divides each weight by 40%, so their conditional probabilities sum to one. The exceptional component is retained explicitly, including when none of the short diagnostic sample's draws selected it.

The current count's position in its **original local size distribution** supplies additional evidence of nearing completion, even when the latest substantial batch has just arrived. This compares two explanations: the count is approaching its expected end, or the original size expectation was too low. Each declaration service's frozen formal-count draws are transformed as `logit((count + 0.5) / (district enrolment + 1))`. Let `z` be the current count's distance from their mean in standard deviations on that scale. The extra finishing weight is `NormalCDF(z) / (1 + (max(z, 0) / 2)^4)`. It is about 50% at the original transformed centre, 79% one SD above it, and 16% three SDs above it. The two-SD response scale is an initial conservative setting, not a fitted probability or a hard count cutoff. Unstarted services, completed or closed services, structural zeros and fixed size priors supply no extra weight.

This weight moves a fraction of the existing routine outcomes into a near-finished component. For non-postal declarations this retains the tiny-addition distribution. For Postal, main counting being largely finished still allows a positive remainder: receipt timing changes that remainder's size instead of discounting the size-based finishing weight. Existing quietness can still support completion when a category greatly exceeded its original size. The exceptional-batch and whole-district exact-zero probabilities are unchanged, including the postal receipt-window safeguard on exact completion. The calculation uses the frozen prior and the current service count; it does not learn this size-underestimate weight from other districts or reuse a previous snapshot's posterior.

The postal near-finished path starts from historical median **uncounted votes as a fraction of final formal postals** of 6%, 4% and 2% at four, two and zero calendar days before the configured receipt deadline, then recentres a broader distribution as described below. These rounded starting settings come from retained Federal 2022/2025 and Queensland 2024 histories. As a descriptive comparison, seats with counts at least 80% of their original postal expectation and absolute changes over the preceding 48 hours no more than 2% of the current count had election-balanced median remainders of 5.7%, 3.8% and 1.7%. Those selection cuts are **not forecasting rules**. Victoria 2022 and SA 2026 supplied no qualifying counts at these dates, illustrating why a substantial processing backlog must remain a separate possibility. The data are too limited to distinguish enduring jurisdiction differences from individual-election effects.

The original median curve is interpolated continuously in natural log odds of the remaining fraction. The early slope continues before day four; after the deadline the odds halve every two calendar days, without a sudden cutoff in remaining counts. With no applicable source-clock deadline, use the day-four curve value. The implemented total log-odds SD is **1.25**, with an election-wide shared SD **0.4** and independent local SD `sqrt(1.25² - 0.4²)`. The common draw represents return/processing differences that do not average away across seats. Compared with the original total variance of `0.4² + 0.4²`, subtracting half the variance increase (**0.62125**) from the curve's log odds preserves expected additions before allocation limits. The median is lower, allowing almost no further votes, while larger additions remain possible. These conservative settings were trialled on Queensland 2024; they are not evidence of calibration across elections. No jurisdiction-specific coefficients or newly downloaded receipt records enter the calculation.

If `q` of final postals remain, the addition is `counted * q / (1-q)`. For example, at four days before the deadline and 5,000 counted, broadening changes the approximate 5th/50th/95th-percentile additions from 126/319/809 to **22/171/1,340**, while preserving their mean of about **375** before allocation limits. Both shared and local draws vary this quantity. Source time, rather than wall time or later snapshots, determines the curve.

For started postals, gentle local quietness also supports this positive remainder without treating receipt as closed. Shared progress, observation coverage, starting support and small late-batch evidence retain their existing calculations; the receipt discount is omitted from this additional quantity support. If a separate Postal processing timetable is configured, its maturity moderates that support as described above. Three quarters of the unused support enters the size-based finishing fraction, then the odds of finishing rather than substantial counting are multiplied by **three**. For example, 50% becomes 75% before any early-count safeguard. Zero support remains zero. This additional positive-remainder path does not change exact-zero or exceptional-batch probability; the separate processing timetable can change them through the underlying progress evidence.

Postal finishing amounts enter the shared declaration allocation **before** other categories receive their shares. Each simulated outcome first supplies relative weights for declaration additions and unused enrolment, plus any selected postal finishing limit. If the ordinary allocation would exceed that limit, the model reserves the limited postal amount, then distributes the remaining capacity using the other categories' and unused enrolment's existing proportions. For example, a postal allowance cut from 2,000 to 100 no longer competes against absent votes as though the discarded 1,900 were still needed. The released space can become either other declaration votes or unused turnout; total turnout is not fixed. Multiple postal constraints are resolved in a finite number of passes, with each pass fixing at least one additional constrained service.

Non-postal local finishing limits retain their after-allocation treatment, releasing capacity to unused turnout. The absent/early-absent routine reference calculation uses the same postal-constrained allocator as the actual outcome, so a discarded postal allowance cannot suppress its reference either. Python preparation, C++ preparation and simulation draws use equivalent calculations. This allocation correction does not alter measured counts or impose an administrative completion flag.

The competing substantial-count and exceptional-batch postal possibilities receive additional local uncertainty **after final allocation**, including all finishing limits. Changing a postal distribution inside the common allocation denominator would also move other categories. Instead, every other category's allocated count stays fixed while Postal varies within its own addition plus unused enrolment. Multiple postal services reserve disjoint equal shares of that unused capacity, preventing their combined upper possibilities from exceeding enrolment. Unstarted postal services and selected near-finished outcomes receive no extra widening.

This final postal widening preserves the expected quantity for **each individual allocated draw**, rather than fitting an offset to the small diagnostic sample. Express the postal addition as a fraction of its available capacity and move its natural log odds both down and up by a local normal magnitude. Let the resulting quantities be `lower` and `upper`. Select `upper` with probability `(original - lower) / (upper - lower)`, otherwise select `lower`. Their weighted average is exactly the original addition. For example, possibilities 60 and 180 around an allocated 100 give the larger quantity probability one third, retaining a mean of 100. Averaging over normal magnitudes retains that property. Exact zero/full-capacity outcomes stay fixed. There is no iteration-time numerical solver, and the extra local draws do not change another category's random stream or its counts.

The extra magnitude scales retain the earlier conservative trial's nominal variance increases: routine uncertainty uses `sqrt(1.5² - 1)` times the measured standard deviation of allocated routine log odds; the batch magnitude scale is `sqrt(2² - 1.5²)`. Among continuous small-addition/batch draws, the batch's share of their combined normal density smoothly weights its extra variance. Tiny additions therefore receive little extra spread, without a vote threshold. These values control the **additional magnitude**, not a claim that the final bounded distribution is normal or has SD exactly 2. The resulting distribution has smaller/larger conditional possibilities, with the original expected count retained near both physical bounds. This is a modelling assumption, not an across-election calibration.

Local completion is applied **after** the ordinary bounded allocation. A selected routine addition is reduced to the smaller of its allocated amount and its near-finished draw; other categories retain exactly their original allocations. Consequently the postal percentages describe the proposed remainder before this limit, not a floor that forces additional votes into an almost-complete account. The removed votes become unused enrolment and lower the formal turnout total. Applying completion inside the allocation's common denominator would instead inflate sibling categories. Counted votes remain lower bounds; this component does not introduce small downward revisions. The non-postal tiny distribution retains its existing quarter-vote median and log-odds SD of one, with the continuing and exceptional branches preserving larger additions.

Both diagnostic count samples and main simulation draws use these same local rules. Diagnostics expose `original_size_z`, `local_completion_weight`, effective `probabilities`, and `allocation_probabilities_before_local_completion` for each unfinished declaration service. Effective probabilities include the transferred routine weight, with `near_finished_postal` identifying its postal branch separately from `small`; conditional small/batch means describe the underlying distributions before final postal widening. `postal_remainder_median_fraction` and the two `postal_remainder_*_log_odds_sd` fields describe the near-finished remainder before allocation limits (null for non-postal categories). `postal_finishing_support` describes supplementary quantity support, and `batch_log_odds_sd` identifies the underlying batch spread. `postal_widening_enabled`, `postal_routine_extra_log_odds_sd` and `postal_batch_extra_log_odds_sd` identify the final neutral spread. Displayed unit, category and district means use its known expectation, avoiding added sampling noise; percentiles use the broadened samples. Main simulation averages still have ordinary sampling error. `local_completion_parameters` records the original-size response scale and half-vote transform. Count version **15** uses the existing frozen-prior format, so version 6–14 priors do not need regeneration. Count preparation uses fixed-size sampling and local calculations, without a fitted joint count sampler.

Absent and early-absent declaration counts also have a **positive-remainder path based on recent processing**. This addresses counts that are quiet or receiving small revisions but remain well below an oversized original estimate. A small update should not by itself restore a large routine allowance. Early-absent means the `PrePoll` declaration category, not an ordinary early-voting centre. Postal and other declaration services keep their existing treatment. This additional path requires the scheduled progress measurements; missing history or an absent receipt timetable supplies no supplementary support.

The extra support uses a gentler response to the size of this seat's count changes: an update of `sqrt(10² + (0.05 * current count)²)` votes supplies half an activity event before time decay. This is a smooth response scale, not a threshold for ignoring changes. The pooled activity keeps its existing 2% scale, and pooled starting support, coverage and late-small-batch evidence are retained. The original progress weight still determines allocation, exact-zero and exceptional-batch probabilities. Only support beyond that weight diverts additional routine outcomes, excluding those already selected by original-size completion. For example, if original quietness support is 50% and the gentler response supports 90%, 80% of the still-routine portion is eligible for the new positive remainder.

Its median proposed amount is two daily quantities, each the average of local recent volume and pooled relative volume multiplied by this seat's current count plus half a vote. Volume is the sum of absolute counting/rechecking changes with a 24-counting-hour exponential decay, rather than newly arriving envelopes; missing earlier counts are not treated as zeros. The pooled measurement retains the existing robust averaging and regional blending. Two days specifies the quantity scale, not a forecast finish date. A quarter-vote minimum keeps negligible-volume outcomes positive. These are modelling assumptions evaluated in Queensland 2024, not parameters fitted across elections.

The proposed remaining/final category share varies in natural log odds with total SD **1.75**, comprising an election-wide shared SD **0.5** and independent local SD `sqrt(1.75² - 0.5²)`. The shared variation is separate from the postal draw. If median remaining votes are `M` at current count `C`, their median odds are `M/C`, so additions are `M * exp(variation)`. Before allocation limits, the 90th-percentile amount is about 9.4 times the median. The final addition is the smaller of that proposed remainder and the original allocation. Other categories retain their allocations; released votes become unused enrolment. Existing exceptional-batch draws remain intact, although some large routine outcomes are reduced.

In count-only comparisons using received Queensland 2024 history through 4 and 5 November, the additional 77 early-absent votes in Ipswich raised the expected final count by about 101 rather than 791; Keppel's 22-vote update raised it by about 50 rather than 648. On 4 November, 3 of 93 early-absent final seat totals exceeded the new 95th percentile, compared with 23 in a matching experiment assigning almost no remaining votes to these extra outcomes. This single-election comparison supports retaining a broad positive quantity, not a claim of general calibration. A long pause still conceals Morayfield's substantial later batch, which this rule underestimates. The 12 November captured totals were used only to assess these predictions, never as forecast inputs.

Diagnostics record `positive_remainder_support`, the prepared-routine fraction `positive_remainder_weight` (after the early guard below), and `positive_remainder_median_votes` before allocation limits. The `positive_remainder` entry in effective probabilities is its unconditional chance, including the district's exact-zero outcome in the denominator. The two `positive_remainder_*log_odds_sd` fields describe the proposed spread. A zero weight means this path is unused; its median is not an unconditional forecast of additional votes.

All three forms of **local near-completion support**—tiny additions, original-size finishing and the additional positive remainder—are restrained when the count is both young and far below its original expectation. With `z` defined above and `d` calendar days after the explicit polling close time, retain the fraction `1 - (1 - exp(-max(0, -z)² / 2)) / (1 + (max(0, d) / 7)^6)`. A two-SD shortfall initially retains about 14% of that support, 57% after seven days, and 99% after fourteen days. At or above expectation there is no restraint. Removed probability returns to substantial counting; exceptional-batch and whole-district exact-zero probabilities remain unchanged. This smooth seven-day scale is a modelling safeguard against a pause concealing unprocessed batches, not an asserted completion date. Missing dates or missing original uncertainty leave the guard inactive. The latest received source supplies the date, never wall time or a future snapshot. Diagnostics record `days_after_poll_close` and `early_completion_retention`.

After receipt closes, the installed timetable allows 48 counting hours for processing and a smooth 12-hour transition away from routine expectations. Sundays contribute half as much processing time as other days. Recent local activity can extend routine processing; activity after the deadline gradually loses influence on other districts. Unfinished declarations retain a starting exceptional probability of 2%, which declines with time and remaining probability outside the no-addition outcome. This is an operating assumption, rather than an independently established frequency of completion.

### Modest ongoing absent and early-absent counting

The preceding mixture can place most probability near zero additions while leaving a large allowance in its unfinished scenarios. Reducing only that allowance can improve the aggregate mean but exclude modest delayed batches across too many districts. A final **three-part treatment** therefore revises absent and early-absent amounts after their original allocation. It leaves postal receipt, ordinary/early booths, other declaration categories, explicit counted-only outcomes and exceptional-batch probabilities unchanged.

The positive target uses **four days** of the existing processing-volume proxy: the average of local recent absolute count movement and pooled relative movement scaled to this district's current count plus half a vote. It includes rechecks and is not a count of envelopes known to be awaiting processing. Variation on natural log odds has total SD **1.25**, including election-wide shared SD **0.5** and independent local variation. Subtract half the log variance so the supplied quantity represents an approximate **mean**, rather than a median with a much larger expected addition. Finite capacity and the original routine allowance can further limit it. These settings were explored in Queensland 2024; they are assumptions rather than a calibration established across elections.

The three parts are:

1. **Smaller routine additions.** In 80% of the otherwise unchanged routine scenarios, blend from the original remaining-vote log odds towards the positive target when that target is smaller. Retain the original allowance in **20%** as an unresolved-backlog possibility. Existing positive processing outcomes retain their earlier treatment.
2. **More modest positive additions.** Transfer some existing tiny-addition probability, including non-postal size-based finishing, towards the positive target. The amount cannot be lower than that scenario's original tiny addition or higher than its counterfactual routine allowance with sibling odds held fixed.
3. **Retained large possibilities.** The unchanged routine-backlog fraction is additional to the existing exceptional-batch path. A pause in processing therefore does not establish a hard ceiling on further votes.

Observed history coverage, counting having started and the existing early/below-prior safeguard determine local support. That support also fades in with category maturity. Let `m` be the received category total divided by its **original pre-election expected total**; multiply support by `m² / (m² + max(0, 1-m)²)`. This supplies about 16%, 50% and 90% of the adjustment at 30%, 50% and 75% counted. It controls reliance on processing evidence, not a probability of completion; even at or above the original expectation, large additions remain possible. Missing history, unstarted categories or missing original size evidence supply no adjustment. No earlier posterior or final captured total enters the calculation.

Probability transfer also depends on the distinction between the old and new amounts. If `R` is the mean counterfactual routine allowance and `A` the processing-based expected amount, the transfer fraction is `support * (R / (R+A))²`. When processing itself still implies a large allowance, this restrains moving many tiny outcomes to that large quantity. Only the strength of this local map is pooled: category sizes are not jointly refitted or forced to a turnout target.

All original allocations and postal output draws finish first. Shape calculations use unused enrolment measured before postal widening, keeping neutral postal variation out of their shape parameters. Eligible declarations reserve disjoint equal shares of the actually unused enrolment for possible positive increases. Only a binding physical limit can restrict the modest alternative after a very large postal draw consumes that room. A routine reduction becomes unused turnout and cannot inflate another category. Postal counts and their known expected values remain unchanged by this stage.

Diagnostic means integrate the new choice between original, reduced and modest amounts for each prepared outcome. Percentiles sample that choice with independent unit-keyed streams. Main iterations use the same rules with directly selected component types; there is no per-iteration fitted offset, inverse-mixture search or joint count solver. `routine_remainder_parameters` records the shared assumptions. Per-service `routine_remainder_support`, `routine_remainder_maturity`, `routine_remainder_mean_votes`, `routine_remainder_reference_mean_votes`, `routine_remainder_bridge_weight` and `routine_remainder_mean_adjustment_votes` expose the local calculation. The last quantity is the expected change in additional votes within the positive-count branch; the displayed count also includes the separate counted-only probability. Effective `reduced_routine` and `modest_remainder` probabilities identify the transferred mass; original allocation probabilities remain separately available.

## History during a live election and a replay

The live application retains checked cumulative declaration and category-group counts under `forecasts/<election>/live-snapshots/turnout/`, using the feed's source timestamp. This directory starts empty during a real election and grows only as feeds are received. The Federal replay script may also use a separate private `live-snapshots/feed-sources.json` manifest to locate hashed raw archives; it does not read the turnout prior for archive selection. It stores observations rather than posterior forecasts, and is separate from saved forecast outputs and any raw feed archive.

Each observation records the service-to-category mapping used to form its counts. Changed category definitions require rebuilding history from the retained sources; older group counts cannot silently become evidence of changed voter behaviour.

A source-time revision replaces that observation, including downward corrections. Repeating a run therefore does not count its evidence twice. Replaying an earlier feed reads only strictly earlier source times, then appends the current checked counts in memory; later observations in the directory cannot influence that replay. Deleting history removes progression evidence while leaving the pre-election prior intact. A fresh archive replay must process sources chronologically to accumulate its available history.

## How counts enter the forecast

`LiveTurnout::Prepared` owns the shared count distributions, booth means, counted-party constraints and cached remaining-voter composition. `LiveTurnoutPreparation.cpp` translates LiveV2's checked results into that account once per source. LiveV2 requests its count targets when preparing booth composition and uses the same account for completion and scenario draws.

Reduced preparation continues to measure party-composition uncertainty at the count means. Full simulation iterations select a joint prior outcome, a no-addition/late-batch outcome and the cached composition response. They do not reread history, refit a model or recompose every booth. Turnout, formality, early/postal conversion and category composition remain distinct uncertainty sources within the joint prior; their exported sensitivities are descriptions of those count responses, not additional errors to apply again. The overlapping legacy declaration-size variation is removed.

First-preference (FP), actual two-candidate-preferred (TCP) and estimated Labor-versus-Coalition two-party-preferred (TPP) counts share future-voter additions. Preferences still missing on already-counted FP votes remain outstanding even in a draw with no new voters. Existing counted candidate votes constrain each projection. Before any FP votes report, the standard simulation owns the forecast and receives no live count constraint.

After the simulation selects a final pair, Labor versus either Liberal or National uses the prepared TPP estimate. Other pairs, including Liberal versus National, use the existing live TCP blend and counted-vote constraints when the live account identifies the same two parties. A preliminary published TCP pair does not itself determine the simulated finalists: an account for a different pair cannot supply their preference totals. This routing does not change candidate elimination or pre-election forecasts. Authoritative final district results retain their separate override of the pair and vote totals.

Turnout is always part of the live calculation. It has no environment-controlled mode or alternative sizing path. Standard simulations without live results do not require these live-election inputs.

### Joint TPP uncertainty as counting progresses

The automatic live forecast updates all seat TPP estimates together. This lets
ordinary-booth evidence constrain the overall result while leaving uncertainty
about unreported early and declaration votes. It also recognises that a common
error affecting many seats does not disappear merely because many seats report.

The baseline retains the mean and covariance of the actual pre-election seat
TPP draws. Covariance measures whether two seats tend to move together. These
runtime statistics are regenerated with the live baseline, not stored in project
files. Each snapshot conditions that same baseline on the current cumulative
received counts; the previous snapshot's posterior is not used as another prior.

The joint state contains seat totals and shared category contrasts.
FP-to-TPP conversion uncertainty reduces observation precision instead of
introducing another fitted signed correction. A contrast describes the change in a category's
swing relative to ordinary booths, after accounting for its historical lean.
Separate local category differences allow each district to depart from that
shared pattern, even when its ordinary count is complete.

Category leans are recentred using expected counts so they reproduce the seat
prior on the vote scale. Changes in category sizes supply no deterministic
political adjustment. The shared category effects use a common reference mix,
with district mixture departures sharing a compatible portion of the seat
prior's covariance. The original seat covariance remains unchanged; category
uncertainty is allocated within it. This avoids treating geographical differences
in postal fractions as direct evidence explaining regional polling misses.

Calculations use `25 * log(p / (1-p))`, where `p` is Labor's vote fraction.
Around 50:50, one unit is approximately one percentage point. The conditioning
uses derivatives of the count-scale account around its prepared centre, so it
is a local linear approximation. Actual baseline draws are retained, including
their non-Gaussian features. Category draws are conditional on those draws,
using the prepared covariance rather than independent category normals.
See [live TPP category composition](live-tpp-composition.md) for the accounting,
calibration, grouped-category mappings and remaining approximations.

Each district/category supplies one cumulative observation from received booth
comparisons, before the live hierarchy's prior shrinkage. Comparable booths use
historical TPP plus the baseline swing; new locations use the seat baseline at
one-tenth comparison quality. Redistribution relevance still applies. Partial
classic TCP keeps the forecast's matched-cohort preference allocation for the
received FP awaiting preferences. Votes yet to arrive are not observations.
Missing records supply no zero measurement; explicit closures have no expected
weight. Accepted restored counts remain usable cumulative evidence, without
becoming fresh counting events. Under optional preferential voting, a reported
classic TCP uses continuing votes rather than treating exhaustion as pending
preferences.

Observation uncertainty includes reporting-location variation, not just the
number of ballots. A location's initial SD is `4.5 + 7 * exp(-expected_votes /
10000)`, retaining the existing unreported-booth TPP scale. The observation uses
quality-weighted received votes. Squared weights determine how many independent
locations contribute: many votes from one centre are not equivalent to many
centres. Weighted scatter between their observed deviations can increase the
allowance. A finite-sample term is approximately `2500 / received_votes` at
50:50 and increases near either endpoint on the log-odds scale; quarter-vote
smoothing keeps possible observed zeros finite. This prevents a tiny report
from appearing precise. The variance of the weighted mean is multiplied by the
uncovered fraction of expected category votes, with a numerical floor of
`0.0001` squared transformed units. These are pragmatic representativeness
assumptions, not newly fitted coefficients; geographic imbalance remains a
separate calibration problem.

Shared category covariance now uses the regularised historical estimate from
14 usable election comparisons. Early-booth, early-absent, day-absent and postal
contrasts have shared SDs about 1.83, 1.76, 1.83 and 2.50 transformed units,
with their correlations retained. Local SDs are about 3.4–3.5. Unknown grouped
splits retain uncertainty rather than pretending that the underlying counts
are equal. Uncalibrated channels retain a broad SD of 5. Covariance-budget
limits may reduce the shared scale; that scale is exported for inspection.
Local effects are integrated as correlated within-seat observation errors whose
weighted sum cancels. The existing FP-to-TPP conversion already adjusts flows
using observed FP/TCP evidence; its point estimate is retained. The joint update
adds no new signed conversion-bias estimate. FP-derived comparisons retain
half-strength averaging weight and now explicitly receive half the precision
for reporting-location/sampling variation, so the discount cannot cancel out
when averaging similar booths.

Conversion uncertainty is separated into district-local and shared-category
parts. Both start at SD 2.5 transformed units and decline as comparable current
FP/TCP locations support the existing preference correction. A location's
contribution is its reliability times `P / (P + 500)`, where `P` is its measured
non-finalist preference pool. Each allowance becomes `2.5 / sqrt(1 + N)`, using
the effective matches in the district/category or election/category respectively.
Only the remaining shared part is enlarged by the district precision budget;
local uncertainty is not multiplied by the number of other districts.

Wholly measured locations and locations still requiring conversion form separate
category observations, with common category uncertainty and a scatter allowance
covering both groups. Receiving FP-only booths cannot raise the conversion error
of the wholly measured group. Partial TCP retains its existing pending-FP
allocation. Direct classic TPP receives no conversion allowance; non-classic
TCP does not constitute a measured TPP pair. These working uncertainty settings
need replay calibration, and require no new cross-snapshot history or signed
mean correction. See the [conversion uncertainty explanation](live-tpp-composition.md#uncertainty-in-fp-derived-tpp)
for the formula, budget interpretation and limitations.

Comparison references are aligned at booth level before categories are combined.
The same shift applies to the historical baseline and its FP-derived TPP
estimate, so a synthetic historical TPP discrepancy cannot masquerade as new
matched swing. Recorded classic TCP is not shifted; for partial TCP only the
pending-preference estimate changes reference. These aligned comparisons are
private to the joint update and do not alter received counts or raw swings.

Category comparisons retain their source: actual historical counts, an explicit
synthetic category allocation, or no category baseline. A seat-wide fallback
cannot teach a category swing. Unsupported categories contribute received votes
locally after joint conditioning, without extrapolating their bias across the
seat or election. Synthetic comparisons retain shared and local baseline error
even at completion. See [category baseline provenance](live-tpp-composition.md#category-baseline-provenance)
for the accounting example, uncertainty scales and exported diagnostics.

The update applies the same covariance calculation to means and uncertainty.
Election/regional movements are propagated through baseline cross-seat covariance,
not added a second time from the already-blended live hierarchy. There is no
separate TPP prior-fade or early-count noise phase-in. For fixed assumptions,
additional observations reduce covariance; revised counts, changing category
weights, observed heterogeneity, nonlinear shares and sampled turnout can still
produce some non-monotonicity between snapshots. No interval-width cap is applied.

After conditioning, sampled turnout/category-size and preference-rechecking
changes remain separate. Counted TPP lower bounds constrain the resulting share;
when no votes remain, the counted account determines it exactly. FP and
non-classic TCP retain their existing integration paths. Results' `Est. Swing`
uses the joint update's prepared centre with those same count limits; `Raw Swing`
continues to describe the received booth comparisons.

The analysis sidecar's `tpp_integration` section records prior/posterior means
and variances, the cumulative observation values and residual variances, and
shared-factor loadings. `weighted_prior_sd` and `weighted_posterior_sd` use the
national report's historical FP turnout weights, on the transformed scale,
before finite-count constraints and sampled turnout. Compare the actual report
quantiles as well. Older `category_biases`, `estimated_tpp` and sensitivity fields
remain intermediate composition diagnostics; they no longer describe additive
noise applied at the TPP simulator handoff. Sensitivities still count each
eligible centre once after the final FP projection refresh.

## Signed revisions to reported vote quantities

The source reports what is currently counted; it does not guarantee that every
number will survive a recheck. `LiveCountRevisions.h` supplies a separate,
general allowance for positive and negative corrections at each reported booth
or declaration-category account. It also applies when routine counting is
finished. It does not alter accepted feed records, their history or warnings.

Initially, each account has a 90% probability of no revision. Otherwise a centred
signed normal has SD 1% of that account's reported formal FP total. WA mixed
early centres use a wider 5% SD to cover the inspected late corrections. Before
bounds, the central 99% intervals of those mixtures are approximately +/-1.64%
and +/-8.2% respectively. These are starting assumptions, not estimates fitted
across jurisdictions; unusually large WA corrections do not determine the size
of general corrections elsewhere. Individual
accounts are independent; a seat with several large centres can have a higher
probability of at least one revision than a seat with one centre.

A candidate is selected in proportion to their current FP votes. The signed
batch changes that candidate's primary and the corresponding pair votes. A
finalist's primaries stay with that finalist; other primaries use the booth's
existing estimated preference-pool split. This allows a candidate-specific FP
counting error rather than only scaling every candidate proportionally.
Bounds apply symmetrically to both signs, retaining nonnegative candidate
counts, nonnegative outstanding pools and room within district enrolment.
Symmetric bounds preserve centring while reducing the available spread near
an account boundary. Missing TCP remains estimated: a revised inferred pair
does not become an observed pair.

These revisions are applied after ordinary composition variation and existing
preference-only rechecking. Counted constraints, projected votes, scenario
completion and the final simulator handoff then use the revised account
together. For example, removal of 100 reported ballots reduces the scenario's
counted FP and projected FP by 100 and removes their corresponding preferences;
it is never represented as minus 100 votes outside a polling place. The joint
TPP handoff also retains that scenario change. A preference-only transfer still
moves votes between finalists without changing the number of voters.

The turnout export's quantity percentiles continue to describe routine future
voters, before signed corrections. The separate `count_revisions` diagnostic
records the correction policy and number of eligible accounts. Final simulation
vote shares and winner probabilities include both uncertainties. Tests exercise
completed/partial/missing pair counts, replay isolation, centring, deterministic
draws and candidate constraints. The live application must still be rebuilt and
replayed to assess election-level effects.

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
flow. When FP has been revised but TCP still belongs to a retained original
matched batch, that original pair continues to supply the outlier comparison.
Every predictor, including the minor-party mix, uses the original FP account;
combining old TCP with revised FP would invent a measured preference flow.

The correction transfers only surviving, allocated preferences. Its limits are
the revised counted lower bounds minus each finalist's current FP votes. The
separate revision model still owns uncertainty about changed transfers, so the
outlier correction cannot remove a finalist's own primaries or double-count
the changed batch. These limits apply both to diagnostic moments and actual
draws, and the opposing transfer updates counted and projected accounts
together. For example, Cooper's 25-vote FP revision retains protection against
its original anomalous preference count; removing the entire non-finalist pool
instead leaves no surviving preferences for this correction to move.

The preference flow is a finalist's TCP minus their own FP, divided by FP
for all candidates outside the finalist pair. The regression relates its log
odds to that finalist's transformed primary share, the largest non-finalist
party's share of the preference pool, and PPVC status. These predictors have
different parent groups: they describe an association rather than interchangeable
shares. Every target booth is omitted from its regression. Robust weighting
limits unusual peers' influence, and the comparison scatter gives more weight
to booths of similar formal size. Sparse comparisons retain fitting uncertainty;
a booth with no comparable peer receives both reported-flow recheck components,
but no regression-directed correction.

When both effective FP and TCP records are explicitly marked rechecked, and
pass these same reconciliation checks, the directed correction is disabled.
That status travels with a retained matched pair through later FP revisions;
a later FP-only recheck cannot certify a preliminary TCP. Older accepted-count
caches remain readable, but a missing pair-status field supplies no evidence
of an official recheck. A newly reconciled current pair uses its current stages.
This currently uses Queensland's explicit official-count stages; a late date,
100% completion or repeated unchanged count does not establish recheck status.
The booth remains in other booths' comparisons. Routine and background components
retain their reported-flow centres, scales and conditional probabilities. Restored
counts retain their original stage, so official FP paired with preliminary TCP
does not qualify. Feeds without explicit stages retain the usual behaviour.

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
`live-preference-rechecking-4`, calibration identity, preparation time, exclusions,
and per-booth probabilities, relative discrepancy, conditional movement and
expected revision in candidate votes. `broad_probability` is the absolute
regression-directed probability; `directed_correction_suppressed_rechecked_pair`
identifies reconciled rechecked pairs for which that probability is zero.
`uses_retained_tcp_basis` identifies temporarily revised accounts: `formal_votes`
then describes the original measured batch, and `current_fp_votes` the current
FP total. `revision_lower_votes` and `revision_upper_votes` give the permitted
opposing transfer to the first finalist after accounting for surviving votes.
`background_probability` is the separate
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

Simulation preparation also builds run-local lookups for fixed seat configuration
and region membership, plus historical aggregation weights and the snapshot's
turnout-weight transition fraction. Region seat lists retain project order so
totals use the same sequence of additions. These inputs are rebuilt after seat
import for each main or supporting run; background jobs bind seat references to
their own project context. No cache is saved in the project file. Live name
lookups reuse the turnout account's shared index, retaining empty results for
unknown or inactive districts where the corresponding getter already did so.
Projected counts, confidence, live deviations and scenario aggregation weights
are read or calculated separately for every scenario. In particular, a shared
seat index does not mean sharing one scenario's result with another.

Local uncertainty preparation retains all 288 deterministic composition samples.
Each worker slot reuses one private working election within that preparation,
instead of copying the whole election for each sample. Before a sample starts,
it restores every booth's temporary FP/TCP/TPP projections, new-voter primary
composition, and TCP/TPP completion and confidence from the prepared model.
Restoration covers all booths before any projection runs, because one booth's
calculation can read evidence from another. Accepted counts, historical evidence,
turnout targets and parent seat/region inputs remain fixed. The existing batch
joins keep each working copy private to one worker at a time; all copies are
destroyed on completion or failure and are not retained for another snapshot.
Sample indices, keyed random draws, booth order and sorted reduction remain
unchanged. The preparation uses at most the configured number of simulation
threads, capped at 24, and retains its existing progress and failure handling.

`SimulationSeatLookupTests` checks order, IDs with gaps, empty regions and binding
to a replacement project context. `LiveAggregationTests` checks that preparing
fixed aggregation inputs still reads each scenario's varying seat sizes. These
portable tests need no feeds, model fitting or forecast-application run:

```powershell
New-Item -ItemType Directory -Force cli-build\seat-lookup-tests | Out-Null
cl /nologo /std:c++20 /O2 /EHsc /I. /Fo"cli-build\seat-lookup-tests\\" /Fe"cli-build\seat-lookup-tests\lookups.exe" tests\SimulationSeatLookupTests.cpp
.\cli-build\seat-lookup-tests\lookups.exe
cl /nologo /std:c++20 /O2 /EHsc /I. /Fo"cli-build\seat-lookup-tests\\" /Fe"cli-build\seat-lookup-tests\aggregation.exe" tests\LiveAggregationTests.cpp
.\cli-build\seat-lookup-tests\aggregation.exe
```

`LiveVariabilityWorkingCopyTests` checks restoration to non-empty prepared values
and isolation between working copies. `LiveVariabilityTests` checks all 288
deterministic sample comparisons and their RMS (root mean square, the size of
the sampled differences used as the uncertainty scale). From the repository root
in a Visual Studio developer shell:

```powershell
New-Item -ItemType Directory -Force cli-build\variability-tests | Out-Null
cl /nologo /std:c++20 /O2 /EHsc /I. /Fo"cli-build\variability-tests\\" /Fe"cli-build\variability-tests\working-copies.exe" tests\LiveVariabilityWorkingCopyTests.cpp
cl /nologo /std:c++20 /O2 /EHsc /I. /Fo"cli-build\variability-tests\\" /Fe"cli-build\variability-tests\samples.exe" tests\LiveVariabilityTests.cpp Config.cpp Log.cpp
Push-Location cli-build\variability-tests
try {
    .\working-copies.exe
    .\samples.exe
} finally { Pop-Location }
```

The isolated execution directory keeps the component tests' `PALog.log`
separate from the application's run log.

`UnreportedPlaceCountingTests` additionally checks the installed probability
knots, smooth transitions, positive-return sizes, rare-return means, enrolment
bounds, unchanged sibling declaration draws, withdrawn-account exclusions and
the shared FP/TCP/TPP composition boundary. From the repository root in a Visual
Studio developer PowerShell, compile and run only this portable component test:

```powershell
New-Item -ItemType Directory -Force cli-build\unreported-place-tests | Out-Null
cl /nologo /std:c++20 /O2 /EHsc /I. /Fo"cli-build\unreported-place-tests\\" /Fe"cli-build\unreported-place-tests\tests.exe" tests\UnreportedPlaceCountingTests.cpp TurnoutModel.cpp TurnoutModelIO.cpp LiveTurnout.cpp Date.cpp
.\cli-build\unreported-place-tests\tests.exe
```

`LiveInputRecoveryTests` checks indexed and original-file history agree through
a withdrawal to zero and excludes a later restoration from earlier queries.
`LiveInputHistoryTests` also checks authoritative adoption, cold reads after
removing full files, identical commits with no writes, middle insertions and
replacements, deferred legacy imports, coherent district rollback, damaged-history
errors and interrupted publication. The original full-file reader remains an
independent comparison for these reconstruction and matching tests.
These controlled tests require no private feed archive or model fitting.

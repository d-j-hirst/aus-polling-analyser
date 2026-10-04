# SA 2026 live vote-count prototype

This report assesses whether a single district vote-total estimate can improve estimates of votes remaining as election results arrive. It replays archived SA 2026 counts, preserves the votes already counted, and compares estimated final counts with the reviewed final results.

The prototype estimates vote counts only. It does not update party projections or winner probabilities. An optional C++ shadow calculation reproduces the count updater alongside the existing forecast; see [its reproduction instructions](../turnout-cpp-shadow.md). An outcome is one possible set of final counts across all 47 districts; the replay uses 1024 such outcomes at each snapshot in the initial comparisons. The counting-progress comparison below additionally varies declaration additions and can reduce the district total as counting slows.

A **coherent allocation** means that all estimated counts agree with each other: counted votes are preserved, estimated additions are nonnegative, the units within each voting group add to that group’s total, and the groups add to one district total. For example, if a district is expected to finish with 25,000 formal votes and 20,000 are counted, its unfinished booths and declarations must receive exactly 5,000 additional votes between them. “Coherent” describes this accounting consistency; it does not mean the estimate is necessarily accurate.

## Inputs and count treatment

The frozen pre-election distribution comes from the saved SA count prior: previous turnout and formality, current enrolment, and district early-vote and postal-application measurements converted into expected formal votes. Its three supported groups are **early** (ordinary PPVC plus early declarations), **postal**, and **remaining** (polling-day ordinary plus other declarations). PPVC means a pre-poll voting centre.

The first, no-results `sa2026-test2` export supplies relative weights within those groups. These finer weights are existing forecast assumptions, not observed historical SA declaration splits. They are held fixed throughout the replay. Current counts come from LiveV2’s exported booth/party account, checked against its district account and the matching ECSA XML detailed first-preference entries. XML district summaries can lag those entries and are diagnosed separately. The existing Narungga correction in the archived C++ loader is retained and identified; it changes the raw early row’s total by 17 votes. Embedded source timestamps, rather than archive capture filenames or simulation run dates, identify the observations.

Reported ordinary booths, including PPVCs, are treated as approximately complete. Their counted votes receive no additions. Routine small rechecks are not given extra uncertainty here. Partial declarations provide lower bounds; their count does not by itself establish completion. A district is fixed only when its ECSA first-preference finalisation flag is true. The two-candidate flag is not used for that purpose.

The three known cancelled Giles booths have no future return under their old identities. Their expected votes can move to other unfinished units within the district. Unexplained missing booths, including the Port Augusta centre listed in Stuart, retain an allocation. A positive count under a closed identity is preserved and flagged.

## Updating a snapshot

Each update starts again from the same pre-election outcomes and uses all counts in the current snapshot. It does not carry an earlier updated distribution forward. This avoids counting a cumulative return twice and allows official downward revisions.

There is no downward district-total adjustment from reported-booth shortfalls in the broad prediction used by the initial comparisons. Lower totals in early-counted categories can be compensated by later categories. Where a prior total is below counted votes, a normal approximation in log odds maps the prior probabilities into the range above that count. This raises the distribution smoothly without collecting impossible outcomes at exactly zero remaining votes. Log odds means log(p / (1 − p)) for a share p; converting back keeps that share inside its parent’s bounds without capping it. Formal votes remain below enrolment.

The bounded calculation applies each supported aggregate group’s counted minimum. Those groups divide the district remainder in their existing proportions. Within a mixed group, unfinished booths start from their own expected sizes; a smooth reservation leaves a positive declaration remainder. Declarations divide that remainder according to their count-conditioned estimates. Finer assumed booth sizes cannot override the supported aggregate early/postal evidence. Counted party records remain unchanged.

## Does the district-total estimate improve?

This compares the mean predicted final formal total with the reviewed final total in each district. **Mean absolute error** is the average absolute difference in votes over the same 47 districts. “Original” is the archived `sa2026-test2` projection; “Prototype” uses the coherent turnout prior and counted minimum. Booth-weight adaptation leaves these total estimates unchanged. “Actually remaining” is final votes minus votes counted statewide, and is scoring information only.

| Source time | Votes counted | Actually remaining | Original error | Prototype error |
| --- | --- | --- | --- | --- |
| 21 Mar 18:06:32 | 0 | 1,115,864 | 6,341 | 371 |
| 21 Mar 18:57:46 | 2,725 | 1,113,139 | 6,280 | 371 |
| 21 Mar 20:39:32 | 200,707 | 915,157 | 3,430 | 371 |
| 21 Mar 21:12:09 | 303,184 | 812,680 | 1,212 | 371 |
| 21 Mar 21:34:04 | 372,562 | 743,302 | 1,277 | 371 |
| 22 Mar 00:32:11 | 641,952 | 473,912 | 1,108 | 370 |
| 25 Mar 12:34:47 | 834,062 | 281,802 | 1,107 | 370 |
| 27 Mar 12:58:30 | 928,843 | 187,021 | 1,106 | 358 |

At the latest tested snapshot, the mean district-total error changes from 1,106 to 358 votes. Much of this improvement is already present before any results arrive: it comes from replacing the original independent sizes with the calibrated turnout prior. Counted minimums cause a smaller subsequent adjustment. This comparison does not establish that turnout has been learned from reporting booths.

## Are the total intervals useful?

A nominal 95% interval contains the middle 95% of possible counts. **Coverage** is the percentage of the 47 final district totals that fall inside their intervals. **Width** is the average district interval width in votes. **Interval score** adds a penalty of 40 votes for each vote by which the final total misses the interval; lower scores favour useful precision without rewarding missed outcomes. These repeated snapshots are one election, not independent election tests. The archive does not provide a comparable original count interval.

| Source time | Coverage % | Mean width | Mean interval score |
| --- | --- | --- | --- |
| 21 Mar 18:06:32 | 97.9 | 2,242 | 2,325 |
| 21 Mar 18:57:46 | 97.9 | 2,242 | 2,325 |
| 21 Mar 20:39:32 | 97.9 | 2,242 | 2,325 |
| 21 Mar 21:12:09 | 97.9 | 2,242 | 2,325 |
| 21 Mar 21:34:04 | 97.9 | 2,242 | 2,325 |
| 22 Mar 00:32:11 | 97.9 | 2,238 | 2,322 |
| 25 Mar 12:34:47 | 97.9 | 2,238 | 2,321 |
| 27 Mar 12:58:30 | 97.9 | 2,169 | 2,253 |

## Does adaptation improve the unfinished allocation?

Ordinary and PPVC changes are estimated separately from complete booths with an exact previous-election name match, a same-seat match and an agreeing previous count. Each booth contributes one log count ratio against its frozen expected size. **Pooled** means that these observations are combined across eligible booths throughout the election to estimate one shared adjustment for each booth type, rather than a separate adjustment for each district or booth. A **multiplier** is that adjustment expressed as a factor. It is applied in log odds only to reliable unfinished non-EAV booths; 0.9 corresponds approximately to a 10% decrease for a small share. It never changes counted votes. In this SA grouping it changes finer allocations while preserving the supported aggregate group totals. The average is shrunk towards no change using 8 equivalent unchanged booths, an assumed strength rather than fitted uncertainty.

The retained 2022 SA source has no observed individual PPVC counts: LiveV2 reconstructs them from combined declaration votes. Those reconstructed values are excluded as empirical matching evidence. Consequently this SA replay has no eligible PPVC multiplier observations. Reported current PPVC counts are still treated as complete.

The next table compares still-unreported current booths with their final counts, using matching district/name identities and booth types in the reviewed final source. This current-to-final match can score PPVC forecasts even though their previous counts cannot train a multiplier. All-zero final booth placeholders are omitted pending review; they remain open prediction units unless a closure was known. Errors are mean absolute differences in votes per booth. “Balanced” starts from the fixed pre-election sizes and counted minimums. It reserves plausible unfinished-booth amounts within the supported aggregate group, then assigns the rest to declarations. “Adapted” additionally applies the shared booth-type factor to reliable unfinished booths. Both preserve the same district and aggregate group totals here. A dash means no eligible booth remains.

The shared updater also supports partial compensation among matched non-EAV PPVCs with genuine historical centre counts. It uses conservative prediction slopes from Federal 2025 research. The reconstructed SA PPVC baselines are not eligible, so this adjustment has no effect in this replay. Unmatched centre estimates are retained, rather than inventing historical sizes or borrowing the Federal relationship for them.

| Source time | Booth type | Booths | Original error | Balanced error | Adapted error |
| --- | --- | --- | --- | --- | --- |
| 21 Mar 18:06:32 | ordinary | 682 | 272 | 135 | 135 |
| 21 Mar 18:06:32 | ppvc | 58 | 2,799 | 1,219 | 1,219 |
| 21 Mar 18:57:46 | ordinary | 668 | 275 | 136 | 198 |
| 21 Mar 18:57:46 | ppvc | 58 | 2,799 | 1,219 | 1,219 |
| 21 Mar 20:39:32 | ordinary | 350 | 295 | 149 | 190 |
| 21 Mar 20:39:32 | ppvc | 57 | 1,986 | 1,229 | 1,229 |
| 21 Mar 21:12:09 | ordinary | 234 | 291 | 153 | 191 |
| 21 Mar 21:12:09 | ppvc | 54 | 1,269 | 1,243 | 1,243 |
| 21 Mar 21:34:04 | ordinary | 172 | 277 | 145 | 188 |
| 21 Mar 21:34:04 | ppvc | 52 | 1,188 | 1,182 | 1,182 |
| 22 Mar 00:32:11 | ordinary | 24 | 269 | 151 | 160 |
| 22 Mar 00:32:11 | ppvc | 27 | 1,272 | 1,193 | 1,193 |
| 25 Mar 12:34:47 | ordinary | 0 | — | — | — |
| 25 Mar 12:34:47 | ppvc | 0 | — | — | — |
| 27 Mar 12:58:30 | ordinary | 0 | — | — | — |
| 27 Mar 12:58:30 | ppvc | 0 | — | — | — |

Category allocation is also checked using the three supported groups. The nine districts with reviewed missing category counts are omitted from this comparison, leaving 38 districts × 3 groups = 114 district-group counts. Their district totals remain in the preceding comparisons. Each group count has equal weight. Interval scores have the same definition as above. Booth adaptation changes only allocations within these groups, so both variants have the same group scores. Mean absolute error is the average absolute difference between the predicted and final group count. These broad-group results do not validate the assumed finer declaration splits or their uncertainty.

| Source time | Mean absolute error | Coverage % | Mean interval score |
| --- | --- | --- | --- |
| 21 Mar 18:06:32 | 183 | 97.4 | 1,680 |
| 21 Mar 18:57:46 | 183 | 97.4 | 1,680 |
| 21 Mar 20:39:32 | 183 | 97.4 | 1,680 |
| 21 Mar 21:12:09 | 183 | 97.4 | 1,680 |
| 21 Mar 21:34:04 | 183 | 97.4 | 1,673 |
| 22 Mar 00:32:11 | 181 | 97.4 | 1,663 |
| 25 Mar 12:34:47 | 180 | 97.4 | 1,669 |
| 27 Mar 12:58:30 | 181 | 97.4 | 1,536 |

## Could reporting order bias the multiplier?

Small and large booths can report at different times. The following diagnostic splits eligible ordinary booths and PPVCs at the median expected size of each type. **Reported change** is the geometric mean count ratio for currently reported booths, expressed as a percentage change. **Final change** applies the same calculation to all matched final booths in that size group. The final column is a retrospective reference, never a prediction input. Differences suggest that the reporting subset does not yet represent the full group.

| Source time | Type | Size | Reported / eligible | Reported change % | Final change % |
| --- | --- | --- | --- | --- | --- |
| 21 Mar 18:06:32 | ordinary | smaller | 0 / 290 | — | -6.9 |
| 21 Mar 18:06:32 | ordinary | larger | 0 / 289 | — | -10.5 |
| 21 Mar 18:06:32 | ppvc | smaller | 0 / 0 | — | — |
| 21 Mar 18:06:32 | ppvc | larger | 0 / 0 | — | — |
| 21 Mar 18:57:46 | ordinary | smaller | 11 / 290 | -18.6 | -6.9 |
| 21 Mar 18:57:46 | ordinary | larger | 1 / 289 | -66.9 | -10.5 |
| 21 Mar 18:57:46 | ppvc | smaller | 0 / 0 | — | — |
| 21 Mar 18:57:46 | ppvc | larger | 0 / 0 | — | — |
| 21 Mar 20:39:32 | ordinary | smaller | 171 / 290 | -10.3 | -6.9 |
| 21 Mar 20:39:32 | ordinary | larger | 112 / 289 | -13.4 | -10.5 |
| 21 Mar 20:39:32 | ppvc | smaller | 0 / 0 | — | — |
| 21 Mar 20:39:32 | ppvc | larger | 0 / 0 | — | — |
| 21 Mar 21:12:09 | ordinary | smaller | 202 / 290 | -8.4 | -6.9 |
| 21 Mar 21:12:09 | ordinary | larger | 177 / 289 | -11.9 | -10.5 |
| 21 Mar 21:12:09 | ppvc | smaller | 0 / 0 | — | — |
| 21 Mar 21:12:09 | ppvc | larger | 0 / 0 | — | — |
| 21 Mar 21:34:04 | ordinary | smaller | 216 / 290 | -8.2 | -6.9 |
| 21 Mar 21:34:04 | ordinary | larger | 212 / 289 | -11.3 | -10.5 |
| 21 Mar 21:34:04 | ppvc | smaller | 0 / 0 | — | — |
| 21 Mar 21:34:04 | ppvc | larger | 0 / 0 | — | — |
| 22 Mar 00:32:11 | ordinary | smaller | 273 / 290 | -7.0 | -6.9 |
| 22 Mar 00:32:11 | ordinary | larger | 282 / 289 | -10.8 | -10.5 |
| 22 Mar 00:32:11 | ppvc | smaller | 0 / 0 | — | — |
| 22 Mar 00:32:11 | ppvc | larger | 0 / 0 | — | — |
| 25 Mar 12:34:47 | ordinary | smaller | 287 / 290 | -6.8 | -6.9 |
| 25 Mar 12:34:47 | ordinary | larger | 289 / 289 | -10.7 | -10.5 |
| 25 Mar 12:34:47 | ppvc | smaller | 0 / 0 | — | — |
| 25 Mar 12:34:47 | ppvc | larger | 0 / 0 | — | — |
| 27 Mar 12:58:30 | ordinary | smaller | 287 / 290 | -6.9 | -6.9 |
| 27 Mar 12:58:30 | ordinary | larger | 289 / 289 | -10.7 | -10.5 |
| 27 Mar 12:58:30 | ppvc | smaller | 0 / 0 | — | — |
| 27 Mar 12:58:30 | ppvc | larger | 0 / 0 | — | — |

## Accounting, timing and reproduction

All categories sum to their district total, with a maximum floating-point difference of 1.5e-11 votes. Reported complete booths and counted party records are preserved; open allocation units receive positive additions. Rare substantial official corrections are diagnosed by the replay and are not used to broaden the ordinary-booth completion assumption.

This run took 3.51 seconds for 8 snapshots. The slowest pair of balanced/adapted count updates took 0.287 seconds for all 47 districts and their booths. This measures the Python count prototype only; it excludes party-share preparation and full simulation iterations. It does not demonstrate the complete application’s timing.

From the repository’s `analysis` directory:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_live_prototype --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_live_prototype
```

The default uses `live_runs/sa2026-test2`, source XML archives in Downloads, the saved `docs/turnout-prior-prototype/fixtures-v3.json`, and reviewed final evidence in `analysis/Data/Turnout/2026sa.json`. Override `--archive`, `--sources`, `--fixture` or `--final-counts` for another retained location. The matching reviewed raw final source must also be retained under `downloads/turnout/2026sa`. These large local inputs are not distributed in Git.

The default selects eight count-progression checkpoints through 27 March, using the closest available source update to each target time. `--all-snapshots` replays every retained update within `--through`. `--prior-booths` changes the explicit shrinkage assumption. The separate `Replay-Sa2026LiveSnapshot.ps1 -List` command lists available raw archives without installing a feed or changing replay state.

The generated local `analysis.json` records source timestamps, archive and final-source SHA256 hashes, the frozen prior version, current counts, estimated additions, shrinkage settings and scores. Normalized input hashes ignore JSON formatting changes. A changed source input or sampler requires a refreshed prior fixture; final scoring follows the exact retained revision named by the normalized source. The public Markdown report is separate from this local data export.

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

The SA comparison repairs two reviewed live-feed category errors, in Croydon and Taylor, without changing any candidate's counted total. It restores the complete polling-day absent batch once the combined absent/provisional candidate counts contain that batch, leaving further votes as provisionals. The early batch is relabelled only when its full candidate vector matches the reviewed final record. No general merging of categories is applied.

**These are retrospective repairs using reviewed final candidate records.** They let the replay examine the forecast separately from known feed mistakes; they do not demonstrate automatic correction in a real-time forecast. Original feed captures remain unchanged, and the JSON records each repair and the exact reviewed source hashes. Unidentified partial batches remain unchanged.

Flinders and Kavel's zero early-absent entries are treated as missing reporting, including in the reviewed final data. The other previously reviewed missing-category exceptions also apply to the replay. Missing units supply no progress evidence and are omitted from the detailed prediction account. Counted votes and broader group priors remain; no category is declared complete and no replacement split is invented. Their final category partitions are excluded from scoring, while district totals remain in the total-count comparison. This also marks the final split as unsuitable for a later election's detailed baseline. The remaining broad-group allocation can still appear in another open category; missing-data exclusions cannot recover the true split, so these seats remain flagged when interpreting individual category estimates.

### Do the paired remaining-vote predictions improve?

Each row compares the same districts against the same final formal totals. **Broad MAE** and **Progress MAE** are mean absolute errors in votes for the broad and progress-aware predictions. **Mean additions** is the progress-aware expected additional vote per district. **95% width** is the average width of its 95% prediction interval. **Interval score** is that width plus forty times any distance by which the final result lies outside the interval; lower is better. It penalizes premature confidence as well as unnecessary uncertainty. Broad samples are repeated onto the same count-draw grid before scoring, so repetition alone cannot change the paired interval endpoints. Final rechecks can leave the reviewed final total below the counted account, which this count-preserving prototype cannot predict. The explicit no-addition branch permits an interval to include the exact current total. These intervals are not established as calibrated 95% probabilities.

| Source update | Broad MAE | Progress MAE | Mean additions | 95% width | Interval score: broad → progress |
|---|---:|---:|---:|---:|---:|
| 2026-03-21 18:06:32 | 368 | 368 | 23,934 | 2,245 | 2,385 → 2,385 |
| 2026-03-21 23:58:30 | 368 | 368 | 10,945 | 2,240 | 2,379 → 2,379 |
| 2026-03-27 17:59:15 | 359 | 359 | 2,887 | 2,008 | 2,148 → 2,148 |
| 2026-03-29 17:59:34 | 625 | 624 | 693 | 1,312 | 6,887 → 6,860 |
| 2026-03-30 17:42:53 | 619 | 604 | 536 | 1,253 | 5,654 → 5,058 |
| 2026-03-31 16:26:37 | 622 | 467 | 391 | 1,166 | 5,822 → 4,245 |
| 2026-04-01 16:00:48 | 622 | 186 | 111 | 737 | 5,804 → 3,772 |
| 2026-04-02 14:32:11 | 622 | 132 | 57 | 500 | 5,804 → 3,535 |

At the last retained source, district-total error falls from 622 to 132 votes. These are repeated observations of one election; they do not establish performance in another jurisdiction.

Category errors check whether a smaller total is also allocated more accurately, rather than merely moving the error elsewhere. Each observation is one district/group with reviewed final data. SA uses the three supported early/postal/remaining groups and excludes unreliable category partitions; federal uses six groups. These comparison units differ from the district-total table. The following averages use the last retained source, with equal weight per known district/group.

| Known district/group observations | Broad category MAE | Progress category MAE | Interval score: broad → progress |
|---:|---:|---:|---:|
| 108 | 202 | 50 | 1,916 → 1,323 |

Residual additions in seats with missing or inconsistent categories require care: a repaired replay is not evidence that the original detailed source split was usable. Unknown reporting is kept separate from genuine zeros and authoritative completion.


### Preparation, continuity and reproduction

The comparison uses 256 frozen preparation outcomes for the whole election and 8 count draws per outcome, producing 2048 updated count outcomes per snapshot. These additional draws reuse the preparation inputs; they do not represent extra independent party-composition samples. Both conditional distributions are also evaluated explicitly, so a rare batch remains represented without requiring a random branch selection. The core also returns the per-preparation reference, small-addition and batch parameters separately, alongside the joint sampled category/total outcomes. They are not reduced to a single standard deviation.

For Adelaide, the continuity check sweeps evidence strength from zero to one and perturbs each point by 1e-06. The largest change in the mean or 95% interval endpoints was 0.002343 votes. This is a numerical continuity check, not evidence that the chosen response speed is optimal. The largest category-to-total accounting discrepancy across snapshots was 1.46e-11 votes.

The JSON also includes a mechanical first-preference illustration using the currently observed candidate mix within each source category. Districts with unsupported outstanding-category mixes are omitted. It does not run the C++ party forecast, estimate new late-voter preferences, or calculate winners.

First reproduce the base count replay described above to create its local `analysis.json`. That file supplies the frozen no-results allocation and metadata paths. The progress comparison also requires the retained daily source snapshots; these large inputs are not distributed in Git. Then run from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_late_batch --elections 2026sa
```

Use `--dry-run` to calculate without writing outputs. `--samples` controls preparation outcomes and `--count-draws` controls their additional count evaluations. Retained feed locations can be supplied with `--archive` and `--downloads`. `--postal-transition-hours` changes the smooth postal deadline response scale for the federal experiment. The command writes `late-batch-analysis.json` beside this report and updates this section. Its provenance records the frozen origin, fixture, current code and exact source revisions. Python replay timings include this count calculation; they do not establish whole-application C++ runtime.

Reusing the last broad preparation, one count draw per preparation took 0.153 seconds; 8 draws took 1.571 seconds for the whole election. This times count evaluations only. It includes no additional party-composition preparation, feed parsing or main simulation iterations.

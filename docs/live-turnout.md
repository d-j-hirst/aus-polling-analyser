# Live turnout model

This document explains how the live forecast estimates votes still to be counted, how those estimates affect its uncertainty, and how to supply the required election inputs.

The model combines pre-election expectations with reported first-preference counts and the history of counting received so far. It supplies one vote-count account to the live forecast: booth estimates, district totals, completion percentages and simulated additions all use that account. Party composition and preference rechecking are prepared separately and then applied to those counts.

## Inputs and preparation

A **prior** is the fixed pre-election distribution of possible final formal vote counts. Each **prior outcome** contains totals and categories for every district together, preserving relationships between districts. Enrolment, previous turnout and formality, and available early-vote and postal-application counts determine its centre. Turnout and formality use bounded transformations; application-to-vote conversion remains a linear factor. A **control** is a reported early/postal count already converted to expected final formal votes, together with its conversion uncertainty.

An input **unit** is an ordinary booth, a pre-poll voting centre (PPVC), or a declaration category such as Postal. Its metadata identifies its district, category group, expected share of that group, historical match, electoral-assistance voting (EAV) service status and any known closure or calibration exclusion. A **category group** is a division supported by historical results; finer splits that were not reported historically must not be invented as observed voter behaviour.

Each election requires `forecasts/<election>/live-inputs/turnout-prior.json`. The live run selects this path using its own election code. A missing prior, incompatible mapping or changed enrolment produces an input error. Election timetables, category definitions and service exceptions are data in this input, rather than election-code branches in the turnout calculation.

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

The command draws the distribution without fitting a model or reading retrospective reports, prototype allocations or archived snapshots. Its output records configuration, maintained-parameter and preparation-code hashes. Regeneration is appropriate when the election's input mapping or pre-election evidence changes; live observations never overwrite the prior.

The live source must also supply a timestamp for measuring counting activity. MediaFeed sources retain their `Created` time; Queensland retains `generationDateTime`, including fractional seconds. Zoned timestamps require normalization to the configured local clock before use. A source without a published time needs an explicitly retained collector timestamp; a saved forecast's old display date is not a substitute.

The fictional configuration in `tests/fixtures/turnout/live-inputs-example.json` demonstrates the format without requiring authorised feeds:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_live ../tests/fixtures/turnout/live-inputs-example.json --output ../downloads/turnout/example-prior.json
```

## Updating counts as results arrive

Each received source updates the same immutable prior afresh. Reported ordinary and PPVC booths are approximately complete; declaration counts are lower bounds. The calculation preserves counted first-preference votes, allocates nonnegative additions, and keeps all category and district totals consistent with their parent account. Minor ordinary-booth rechecks do not create a staged-addition expectation.

Unreported matched PPVCs retain their own size expectations with conservative partial compensation for changes in other matched centres. Compensation depends continuously on expected centre size. New or changed centres use their supplied starting weights; EAV services receive their separate small-service treatment. A known closure contributes no expected additions or voter-behaviour evidence. Redistribution effects beyond these input mappings are not inferred by this calculation.

When the ordinary/early categories largely finish, an open declaration category does not automatically inherit the whole district shortfall. Evidence about progress in its supported category group gradually releases that imposed allowance. This uses actual group counts, including PPVCs within a historically combined early-vote group. It is distinct from evidence about whether that declaration category itself is slowing.

The model keeps two kinds of late-count uncertainty separate. Routine expected additions recede as counting slows and the receipt timetable passes. Exceptional additions remain possible with a low, gradually declining probability. There is also a district-wide outcome with exactly no new first-preference votes. Completion flags make that outcome certain; quietness alone does not.

The count distribution combines previous expectations, small additions and exceptional late batches. Their weights change continuously with the evidence. For example, a 60% probability of no additions and 100 additional votes on average in the other outcomes gives an unconditional expectation of 40. The exceptional component is retained explicitly, including when none of the short diagnostic sample's draws selected it.

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

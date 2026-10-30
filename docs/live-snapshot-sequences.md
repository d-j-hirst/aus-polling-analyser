# Live snapshot sequences

Snapshot sequences let you compare a live forecast at several points in an election count without selecting and running each feed manually. The Results tab saves your chosen timestamps in the project and runs their feeds in order through the normal live simulation.

Each completed snapshot produces a retained result that the Live Booths tab can display, including its forecast tables and graphs. Feeds and diagnostic results remain private files; the project stores the selection and folder settings, without embedding the feeds.

## Prepare the inputs

Set up and run the election's automatic live simulation normally first, including its previous-election results, preload data, base projection and turnout inputs. A sequence uses those same settings and iteration count. All participating live simulations must use the same election. Manual live simulations are not supported by the current live pipeline.

Put retained feeds in `forecasts/<election>/live-snapshots/feeds/`, for example `forecasts/2026sa/live-snapshots/feeds/`. This directory is gitignored. The initial implementation recognises:

- SA XML captures named `el2026260321183853.xml`, where the election year precedes the 12-digit timestamp code `260321183853`.
- Federal Detailed or Detailed/Light ZIP captures named `aec-mediafeed-Detailed-Light-<event-id>-20250503195904.zip`, and corresponding timestamped results XML files. Their code is the 14-digit timestamp `20250503195904`.
- Victorian booth-level lower-house ZIP captures named `State2022mediafilelitepplh_20221126_180329.zip`, and corresponding XML files. Their code joins the date and time as `20221126180329`. Aggregate-only lower-house and upper-house feeds are excluded.
- Queensland ZIP captures named `20241026180624_publicResults.zip`, and corresponding timestamped XML files. Their code is the leading 14-digit timestamp. Replay reads `publicResults.xml` from each ZIP and checks the election identity against the preload before applying counts; concurrent council-election accounts are not used.

Retain one source file per timestamp. A fixed-name working XML is not a timestamped capture. ZIP input is read locally; replay does not download the selected current feed or invoke the manual selection scripts. The feed's own clock continues to govern counting history, independently of the capture's filename code.

Federal replay can also use a local preload when an operator needs an explicit
normalized roster rather than a remote ZIP cache. Set the preload source to
`local:downloads/example-preload.xml` and provide the companion
`downloads/example-preload@polling_places.xml`. Absolute paths are accepted too.
Both files are checked before preparation; a missing companion produces the
normal missing-input explanation. Remote preload URLs retain their usual
download/cache behaviour. A normalized preload must preserve its source
evidence and must not borrow current-election counted votes or final sizes to
choose starting expectations.

## Choose and save a sequence

On Results, select **Configure snapshot sequence...**. The dialog immediately reads the standard election folder, or the folder previously saved for this project. Browse or edit the folder only if you want to use a different collection.

If the folder is missing or cannot be read, a message explains the problem and the editor stays open. Create and populate the folder, or use **Browse...** to choose an existing one. Errors while editing the path appear within the dialog rather than repeatedly opening message boxes.

The left list shows all recognised codes in chronological order, with readable dates and filenames. Adding a selected code inserts it at its chronological position, including between existing entries. Selecting a code already present leaves the sequence unchanged. You can also enter codes in the sequence box, one per line. Gaps are allowed, but manually entered codes must be increasing and unique.

Pasted lists accept Windows CRLF, LF-only, lone CR and Unicode next-line,
line-separator or paragraph-separator characters. The editor displays these as
line breaks and the parser uses the same boundaries. Pasting replaces the
selected text and supports Undo; it does not reorder codes or discard invalid
text. This also handles LF-only lists copied from code editors that otherwise
appear as a single line in a native Windows text box.

**Add next X** appends up to X consecutive available codes after the last code already entered. It excludes the starting code. For example, entering the first code and adding the next five creates a six-snapshot sequence if five later captures are available. If fewer remain, it adds all of them; at the final capture it leaves the sequence unchanged without reporting an error. The resolved preview shows the actual files selected.

Select **Save sequence**, then save the project to retain it in `.pol2`. The shortcut is saved as explicit timestamps, so adding more files to the folder does not change an existing selection. Older projects load with no sequence selected. A configured project can be opened without the private feeds; running it requires those files to be present.

When debugging with break-on-throw enabled, routine timestamp and replay-setting validation uses `LiveSnapshotSequence::ConfigurationError`. Exclude that specific exception from debugger breaks to continue editing normally while retaining breaks on unexpected runtime errors. Missing folders and reaching the end with **Add next** do not throw exceptions.

## Run and inspect the results

Select **Run snapshot sequence**, or Shift-click **Run Live Simulations**. Each timestamp supplies one explicitly selected local feed to the same simulations used by the normal Run control. Saved current-feed URLs and directories are not changed.

Both live-run controls start immediately in the background. The progress window remains movable and resizable throughout preparation, calibration and calculation. The application continues showing the last completed results while the next snapshot runs; intermediate calculation reports are not displayed.

Preparation and simulation use the configured simulation-thread limit. The background context reserves one logical processor for browsing where possible. Completed analysers are released after their detached outputs have been captured, so retained reports do not require a second full analyser in memory.

Routine preparation writes a compact seat/booth summary and timing information
to `PALog.log`; warnings and analysis sidecars remain available. It no longer
dumps every booth's internal state into the text log. To restore that dump for
a debugging session, set `bLiveDetailedLogging=1` in the workspace's
`config.cfg` and restart the application. The default is `0`.

Uncertainty preparation retains all 288 FP/TCP samples, caching their unchanged
baseline booth totals and transformed shares once per snapshot. The earlier
TPP RMS diagnostic is optional because the joint TPP calculation now supplies
the forecast's TPP uncertainty. Set `bLiveLegacyTppDiagnostic=1` in `config.cfg`
and restart to calculate it. By default, its analysis field
`tpp_all_booths_std_dev` is `null` (not calculated), rather than a misleading
zero. This switch does not disable or alter the active joint TPP calculation.
Both switches default off when absent from older configuration files and
require no project-file format change.

After preparation, simulation draws share one immutable set of booth data.
Each draw still owns its seat, region and election projections, including its
sampled preference corrections. The 288 preparation samples retain independent
writable booth copies because they recalculate booth projections. Diagnostics
and booth views read the same prepared data through shared ownership; they do
not depend on the original analyser remaining alive. This reduces copying
without changing count accounts, iteration keys or forecast calculations.

You can scroll and copy Results cells with **Ctrl+C**, change the All/Latest/Significant/Key filter, use every Live Booths output type, switch simulations or saved reports in Display, and browse the other tabs. Editing, downloads, other calculations, macros, configuration, report saving/deletion, uploads/exports and project Save/Save As/New/Open are disabled until the live job finishes. Ordinary selection changes do not re-enable these actions.

Results separates **Raw Swing** from **Est. Swing**. Raw Swing averages each booth or declaration category's Labor–Coalition swing in percentage points. Counted classic TCP stays fixed; FP still awaiting preferences uses the forecast's existing preference-flow allocation, including any retained matched FP/TCP cohort. Thus 100 TCP votes arriving after 10,000 FP votes leave 9,900 votes estimated from FP, with a combined 10,000-vote weight. FP-only booths keep contributing independently. A complete TCP account needs no inferred addition. Before any preferences arrive, Raw Swing uses the existing booth-level 2PP estimate from received FP votes. Historical FP can also supply a previous-election comparison when classic TCP is unavailable. It makes no adjustment for which vote types have reported and does not blend with the seat-wide forecast or add outstanding votes. It stays blank without received votes or a usable historical comparison. Est. Swing retains the live model's seat-wide estimate, including outstanding votes, relative to the configured previous margin. Both use the same party colours. The summary's **Projected 2PP** uses the completed forecast's mean TPP distribution, matching Display and Live Booths.

The **Raw 2PP deviation** in Internal 2PP Metrics is a model diagnostic: it measures departure from the pre-count baseline in transformed share units, `25 × log(p / (1 − p))`, where `p` is Labor's vote proportion. It is not the same statistic as the Results table's observed percentage-point swing.

The Results summary shows separate **Counted: FP** and **TCP** percentages.
Each divides accepted reported candidate votes by the same estimated final
formal turnout for the election. Counts are added across districts before
dividing; the percentages are not averages of district percentages. For example,
6 million FP votes and 4 million TCP votes against an expected 10 million formal
votes give 60% FP and 40% TCP coverage. The denominator comes from the prepared
turnout estimate, so it can change between snapshots; neither value is a measure
of forecast confidence.

TCP includes classic TPP and non-classic counts, counted once, but excludes
FP-derived estimates. When the commission withdraws or replaces a district's
TCP pairing, counts retained for the old pairing remain available to the model
as reference evidence and do not contribute to this counter. A replacement pair
with zero votes therefore restarts that district's TCP progress at zero. In
optional-preferential elections, exhausted ballots are not included in the two
surviving candidates' published totals, so TCP coverage can remain below FP
coverage after counting ends. Older saved reports without these separate counts
show dashes. New reports save the counts and their denominator together with the
forecast; diagnostic JSON includes `count_progress`, `total_fp_percent_counted`
and the existing `total_2cp_percent_counted` field.

The same view (the last dropdown entry) includes the joint 2PP centre, prior and updated SD, variance retained, FP-derived evidence percentage, starting and remaining shared conversion SD, effective matched preference locations, and category covariance scale. See [the column definitions](live-tpp-composition.md#verification-and-diagnostics) for their units and denominators. New snapshots carry these small summary fields; older snapshots show blanks where they were not recorded. The snapshot column stays visible when scrolling horizontally.

In **Internal 2PP Metrics → Graph View**, the **Metric** selector switches between
**Internal projected 2PP** (the default) and **Raw 2PP deviation**. The Y-axis fits
the selected metric's values, so changes around 50% are not compressed by a
deviation series near zero. The selection is retained across incoming snapshots
and switches between table and graph views; the table still shows all metrics.

Live Booths graphs begin on the first date with any received counting progress, retaining that day's pre-count baseline. Older pre-election captures remain available in tables and the snapshot selector. Before any votes arrive, graphs show the latest available pre-count day. Archives without progress metadata retain their existing range. This prevents a preliminary feed from weeks earlier compressing election-night updates; the existing compression of post-election-night time is retained.

The progress window shows the running code, completed count, last published code and calculation stage. Uncertainty preparation reports how many of its preliminary samples have finished. Calibration and baseline sub-runs can log completion before the main snapshot is ready; the published count advances only when the whole snapshot has completed.

After each snapshot, its completed reports, counted outcomes and diagnostics are published together and the views update before the next snapshot starts. Existing selections and the Results viewport are preserved where possible. **Stop after current snapshot** finishes that snapshot's participating simulations before stopping; it does not interrupt an iteration. Closing the progress window requests the same stop, and closing the application waits asynchronously for publication and worker cleanup. Routine feedback goes to `PALog.log` in the application's working folder. A simulation or action-required export failure stops the sequence and displays the explanation in the progress window, without a message box. Earlier successful simulations retain their reports if a later simulation in that snapshot fails.

Every automatic simulation retains its configured diagnostic output folder under `live_runs/`. If that setting was empty, configuring the sequence assigns a separate default folder named `<election>-snapshot-sequence-<simulation-id>`. Save the project to retain these settings. Outputs retain the usual snapshot and execution timestamps, so reruns produce separate records.

Open **Live Booths** and use its **Simulation** selector to choose the forecast series. Its saved runs remain available after closing and reopening the GUI, as long as the private output files are retained. No extra saved-report entry is added to `.pol2` for each run.

Live Booths loads history and prepares its selected table or graph on a separate worker. Unchanged files are retained in its compact cache, and new completed records are added without rereading the complete history. Selecting another simulation series releases the old history before loading that series. The GUI receives only run metadata and the requested presentation; Node Inspector does not construct the other views. Full reports and analysis archives remain available on disk.

**Follow latest** starts enabled. Selecting an older run switches it off; enabling it again selects the newest completed run. Mode, seat, party, filters and scrolling remain under your control. A history table follows appended rows only if it was already at the newest edge. “Updating view...” indicates that a replacement presentation is being prepared; switching views does not stop the simulation.

In **Graph View**, **Seat Expectations** and **Vote Shares** each offer a party
selector, defaulting to **All parties**. Selecting one party displays only its
series and scales the graph to that series. Each view remembers its selection
as snapshots arrive and when switching between graph and table views; tables
continue to show all parties.

Graphs show the counting period through the end of the day three weeks after
election day (for example, through 11 June for an election on 21 May). The first
day with received votes identifies election day, as it does for the existing
pre-election cutoff. Very early feeds and later archival updates remain in the
tables and run selector. Older archives without count-progress metadata keep
their full graph span because its election day cannot be identified reliably.

Replay uses the existing received-count history: earlier observations already saved for the election may inform a snapshot, while observations from later source times are excluded. Listing available feeds does not ingest them into the model. This is the same history policy used by individual manual replays; a sequence is not an isolated fresh-history experiment.

## Inspect detailed analysis archives

Each retained run also saves the intermediate calculations needed to investigate its forecast. These diagnostics let you examine how observed counts, turnout estimates and preference adjustments contributed to a result. They remain separate from the main snapshot JSON used by Live Booths.

New diagnostics are saved as `.analysis.json.gz`. The archive uses gzip compression, stores party, seat, region and booth identities once, and replaces repeated field names with short codes. Each file contains its own identity and field tables, so it can be read independently. All intermediate fields are retained, including empty or unavailable values. Compression and packing affect only the export, not the calculations performed by the simulation.

Most non-integer diagnostic values are written to six significant figures. Estimated vote counts and geographic coordinates use eight significant figures; integer values remain exact. Saved parameter tables retain their full precision. Extra digits are retained when rounding a value strictly between zero and one would make it exactly zero or one. These rules reduce storage while preserving useful precision for comparisons.

The Python analysis readers accept both these archives and older `.analysis.json` files. If both formats exist for the same run, they prefer the compressed archive. To produce a readable copy for manual inspection, run this from `analysis/`, replacing the input path with a retained archive:

```powershell
.\.venv-win\Scripts\python.exe -B -m lib.live_analysis_archive "..\live_runs\<series>\<snapshot>.analysis.json.gz" --output "..\cli-build\inspect-analysis.json"
```

The command restores descriptive field names and repeated identities. It creates a new file and refuses to overwrite an existing one. It cannot restore digits removed by export rounding. The source archive is preserved; existing archives are neither converted nor deleted automatically. Both `live_runs/` and `cli-build/` are gitignored.

# Live snapshot sequences

Snapshot sequences let you compare a live forecast at several points in an election count without selecting and running each feed manually. The Results tab saves your chosen timestamps in the project and runs their feeds in order through the normal live simulation.

Each completed snapshot produces a retained result that the Live Booths tab can display, including its forecast tables and graphs. Feeds and diagnostic results remain private files; the project stores the selection and folder settings, without embedding the feeds.

## Prepare the inputs

Set up and run the election's automatic live simulation normally first, including its previous-election results, preload data, base projection and turnout inputs. A sequence uses those same settings and iteration count. All participating live simulations must use the same election. Manual live simulations are not supported by the current live pipeline.

Put retained feeds in `forecasts/<election>/live-snapshots/feeds/`, for example `forecasts/2026sa/live-snapshots/feeds/`. This directory is gitignored. The initial implementation recognises:

- SA XML captures named `el2026260321183853.xml`, where the election year precedes the 12-digit timestamp code `260321183853`.
- Federal Detailed or Detailed/Light ZIP captures named `aec-mediafeed-Detailed-Light-<event-id>-20250503195904.zip`, and corresponding timestamped results XML files. Their code is the 14-digit timestamp `20250503195904`.
- Victorian booth-level lower-house ZIP captures named `State2022mediafilelitepplh_20221126_180329.zip`, and corresponding XML files. Their code joins the date and time as `20221126180329`. Aggregate-only lower-house and upper-house feeds are excluded.

Retain one source file per timestamp. A fixed-name working XML is not a timestamped capture. ZIP input is read locally; replay does not download the selected current feed or invoke the manual selection scripts. The feed's own clock continues to govern counting history, independently of the capture's filename code.

## Choose and save a sequence

On Results, select **Configure snapshot sequence...**. The dialog immediately reads the standard election folder, or the folder previously saved for this project. Browse or edit the folder only if you want to use a different collection.

If the folder is missing or cannot be read, a message explains the problem and the editor stays open. Create and populate the folder, or use **Browse...** to choose an existing one. Errors while editing the path appear within the dialog rather than repeatedly opening message boxes.

The left list shows all recognised codes in chronological order, with readable dates and filenames. Add a selected code, or enter codes in the sequence box, one per line. Gaps are allowed, but codes must be increasing and unique.

**Add next X** appends up to X consecutive available codes after the last code already entered. It excludes the starting code. For example, entering the first code and adding the next five creates a six-snapshot sequence if five later captures are available. If fewer remain, it adds all of them; at the final capture it leaves the sequence unchanged without reporting an error. The resolved preview shows the actual files selected.

Select **Save sequence**, then save the project to retain it in `.pol2`. The shortcut is saved as explicit timestamps, so adding more files to the folder does not change an existing selection. Older projects load with no sequence selected. A configured project can be opened without the private feeds; running it requires those files to be present.

When debugging with break-on-throw enabled, routine timestamp and replay-setting validation uses `LiveSnapshotSequence::ConfigurationError`. Exclude that specific exception from debugger breaks to continue editing normally while retaining breaks on unexpected runtime errors. Missing folders and reaching the end with **Add next** do not throw exceptions.

## Run and inspect the results

Select **Run snapshot sequence**, or Shift-click **Run Live Simulations**. Each timestamp supplies one explicitly selected local feed to the same simulations used by the normal Run control. Saved current-feed URLs and directories are not changed.

A ten-second countdown lets you move or resize the progress window before calculations begin, so it can stay out of the way of the results. Select **Start now** to skip the delay, or **Cancel sequence** to cancel before the first snapshot. The project's other controls remain disabled during the countdown and replay. While an individual simulation is calculating, window interaction pauses until it finishes.

The progress window shows the current code and completed count. After each completed snapshot, the Results, Seats, Display, Map and Live Booths views refresh before the next run starts. Configuration is disabled while the sequence runs. **Stop after current snapshot** is handled between simulations; it does not interrupt an iteration already running. Routine feedback goes to `PALog.log`. A simulation or action-required export failure stops the sequence and displays the explanation in the progress window, without a message box.

Every automatic simulation retains its configured diagnostic output folder under `live_runs/`. If that setting was empty, configuring the sequence assigns a separate default folder named `<election>-snapshot-sequence-<simulation-id>`. Save the project to retain these settings. Outputs retain the usual snapshot and execution timestamps, so reruns produce separate records.

Open **Live Booths** and use its **Simulation** selector to choose the forecast series. Its saved runs remain available after closing and reopening the GUI, as long as the private output files are retained. No extra saved-report entry is added to `.pol2` for each run.

Live Booths retains only the data used by its displays. Seat win probabilities
and mean primary vote shares are stored as numeric arrays with party IDs;
missing values and diagnostic errors retain their separate meanings. The tab
releases its old history before reloading and builds only the selected table or
graph. Node Inspector does not construct the other views. These memory savings
affect the viewer's cache; the complete reports and analysis archives remain
available on disk.

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

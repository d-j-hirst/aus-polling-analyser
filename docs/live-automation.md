# Automated live forecasts and replay operation

This guide explains how to deliver election feeds to Polling Analyser, run unattended forecasts, and prepare identified reports for Australian Election Forecasts. The calculation still uses the existing live simulation. Automation adds a durable queue around it: every valid count is retained, while only the newest accepted capture is selected when the calculation becomes free.

Feed acquisition is external. The supplied replay feeder copies existing captures; authenticated VEC SAS and commission FTP downloaders are separate. The upload client requires the versioned server support described in [the publication contract](live-publication-v1.md). It refuses to send automated packages through an older server's ordinary report endpoint.

## Set up a run

1. After opening the project, run the base model for the automatic live simulation and then its projection. Opening a `.pol2` restores the saved results for display. The model also needs its inputs loaded to generate fresh forecasts. For **SA State Election 2026 live testing.pol2**, press **F1** (**Macros → Run Macro**), enter `run-model:0;run-projection:0`, and wait for the success message. Other projects may use different model and projection numbers.
2. Prepare the election's existing preload, previous results, count prior and one automatic live simulation. Federal URL-based support files must already be cached, or configured as local files, before watching starts. Use the normal iteration count required for publication. Do not change simulation inputs while the watcher is active.
3. In **Results**, choose **Configure watching**, then a new replay or official live run. Choose a run ID using lowercase letters, digits and hyphens, for example `2026sa-pipeline`. Keep its default inbox at `live_runs/<run-id>/inbox` or select an existing delivery folder. Set the election date, optional display name, visibility and commentary preset. Saving makes the run available to the uploader and feeder.
4. Choose **Watch / Resume**. The program calculates and saves a separate ordinary pre-count reference with the main simulation's settings and iteration count. It then accepts incoming feeds and forecasts the newest available observation. If it pauses because the base model or projection is unprepared, complete step 1 and click **Watch / Resume** again on the same run.
5. Start the uploader with the run ID, before or after **Watch / Resume**. Use `--prepare-only` to inspect packages on disk, or `--local` to publish them to the supporting website on your computer. The worker continues watching for each new forecast.

Example from the repository root on Windows:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B uploads\upload_manager.py --watch RUN_ID --prepare-only
```

For a supporting local server, replace `--prepare-only` with `--local`. Existing `--test`, `--remote` and `--all` selections still work; **`--all` includes local, test and production**. Existing one-off upload commands continue to read `uploads/latest_json.dat`. Automated reports have separate files and never replace that manual export.

You can create `live_runs/<run-id>/` in advance and keep your `.pol2` project, feeder script and capture list there. You can also prepare the feed inbox. When a run has already been configured, the GUI offers to open it. If only partial run outputs remain, the GUI identifies a conflicting path so you can inspect it or choose a fresh ID.

The uploader resolves a bare run ID from its repository, independently of the working directory. Existing commands can also pass an explicit directory. The run ID appears in the saved watcher settings and its folder under `live_runs/`. A new run has a new history and publication URL. Restarting the same run retains its ID. Election identity, live/replay designation, election date, timezone, display name and initial visibility belong to that identity; create a new run to change them. Inbox changes are permitted after pausing.

Use **Pause after current forecast** before changing inputs. **Stop / disarm** also finishes the current calculation, but records an explicitly disarmed session. Reopening an armed project resumes forecasting; a paused/disarmed session stays paused. Closing an active project records an armed continuation after the calculation finishes. This does not start the operating system or reopen the GUI, and the upload/reporting worker must also be restarted after a computer restart.

Project format 70 saves watcher settings. Operational state is separate in the run directory. The local `run.json` is the reporting worker's configuration; **Configure watching** saves it. Arming records the forecast input revision. The comparison cutoff defaults to `night_end_hour: 6`; commentary deadlines default to `commentary_timeout_seconds: 90` for states and `30` for federal elections. These values are selected in the watcher configuration, not electoral model parameters.

## Deliver files safely

Deliver one complete timestamped XML or ZIP at a time. An automated downloader writes a temporary filename ending in `.part`, closes it, then renames it to the final XML/ZIP name in the same directory. The observer ignores temporary files. Manual copies must have the same size and modification time on consecutive scans, and must parse completely before acceptance. The collector rechecks the copied contents and metadata before retaining them.

The inbox can receive fixed-name files, but keep each delivery available for at least two scans (normally a few seconds) before replacing it. The collector archives each observed version under its content identity, including while a forecast is running. No folder watcher can recover a fixed-name version that was overwritten before it was observed; timestamped immutable deliveries are therefore preferable.

The feed must identify the configured election and contain the expected candidate/booth structure. AEC, VEC, ECQ and WA source identifiers are checked against the preload. SA's published schema lacks that event identifier; its current candidate/district structure and observation clock are validated by the existing SA reader. Do not place synthetic practice feeds or captures from another election in the trial inbox.

NSW watching is explicitly disabled until its snapshot identity reader is validated. The initial engineering trial uses SA; new 2026 Victorian formats still require genuine feed/preload examples before deployment. Existing supported readers are reused, rather than assuming that a newer commission schema is identical to an older one.

Identical source observations are not forecast again. An observation with a new source time remains distinct even when its votes are unchanged, because elapsed counting time can alter the expected vote remaining. Conflicting contents at the same accepted observation time are retained for inspection and rejected, rather than replacing accepted evidence silently. Correct incomplete copies by replacing the delivery with the complete original. Other conflicts require inspection; do not edit the accepted history to force them through.

## Replay feeder

Inspect a short selection before delivery:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B live_scripts\replay_feeder.py --run-id 2026sa-pipeline --codes-file sequence.txt --dry-run
```

Remove `--dry-run` and add `--step` to deliver one capture. Single-step delivery chooses the capture after the latest selected file already in the inbox, including manually copied files. An empty inbox receives the first capture; an inbox containing the last selected capture receives nothing further. Conflicting contents under an existing filename are refused. Use `--speed 1` for original intervals, or `--speed 60` for one hour of election time per minute. Timed delivery resumes from `feeder-state.json`. All modes preserve the original observation clocks. A fresh inbox starts a new delivery sequence; a fresh GUI run starts a separate accepted history.

The collector retains one additional original-feed copy for each distinct delivery. The feeder also makes inbox copies. `--dry-run` reports their prospective volume; **do not point a trial at an entire large archive without checking that number**. Accepted counts remain in authoritative change-only history. Full per-capture `accepted-counts` JSON is not recreated. Forecast diagnostics use the existing compressed archive; compact explanatory evidence is also compressed and stored once per successful forecast. Nothing deletes existing archives automatically.

## Step through a local SA website replay

This rehearsal follows a feed from delivery to the private replay page. Each press of the feeder script supplies one capture. The GUI, commentary worker and local website then operate as they would during counting.

1. In the local website's backend environment, apply `python manage.py migrate forecast_api` through `0018_live_publication`. Set `DJANGO_LOCAL_REPLAY=1` and restart the development server on loopback at port 8000. Start the local frontend with `REACT_APP_BASE_BACKEND_URL=http://localhost:8000`. A browser on localhost can then open the private replay directly. The uploader uses its own configured account with forecast submission permission.
2. Open a copy of **SA State Election 2026 live testing.pol2** and run the model and projection macro in [Set up a run](#set-up-a-run). Under **Results → Configure watching → New replay run**, enter **2026sa-pipeline**, keep the default inbox, set **21 March 2026** (`2026-03-21`) and choose private visibility. Keep the 90-second deadline and choose **SA trial** for Terra Standard/low with a US$1 run limit. Choose **Off** for a numerical-only trial.
3. Start the uploader, then click **Watch / Resume** in the Analyser. The `--local` flag sends forecast packages to localhost. Enabled commentary uses the OpenAI API.

   ```powershell
   .\analysis\.venv-win\Scripts\python.exe -B uploads\upload_manager.py --watch 2026sa-pipeline --local
   ```

   From **WSL in `uploads/`**, use the activated uploader environment. Install `requirements.txt` once for Python's timezone support, then run:

   ```bash
   python3 -B upload_manager.py --watch 2026sa-pipeline --local
   ```

4. Once `receipts/local.json` in the run folder shows `origin` accepted, deliver the first two feeds one at a time from PowerShell at the repository root. Wait for each forecast's local receipt before sending the next feed. The empty update gets local no-count commentary. The first counted feed adds 2,725 primary votes.

   ```powershell
   .\live_scripts\Step-SaCommentaryTrial.ps1 -RunId 2026sa-pipeline
   ```

5. Open `/2026sa/replays/<run-id>` on the local frontend. Compare the observation time, mechanical changes and commentary with the same forecast's `packages\<forecast-id>\publication.json`. The zero-count reference is a comparison anchor. The first delivered forecast becomes the first entry in replay history. For a restart check, restart the worker with the same run ID and verify that the existing forecasts appear once.

The step script covers the first four captures. To continue, run the general feeder with `--run-id 2026sa-pipeline --step`. It resumes from the files already delivered. **Allow roughly 3–5 GiB on the Analyser side before running the whole archive**, plus space in the website database. The US$1 commentary limit is for the short rehearsal.

### Full SA replay with local publication

This rehearsal gives each of the 153 retained SA captures its own forecast and publishes every applicable comparison to the local website. Use a fresh private replay ID and its empty default inbox. The watcher builds the accepted count and turnout histories inside that run. It calculates a new start-of-counting reference. In **Configure watching**, choose **SA full run** for Terra Standard with low reasoning, one attempt per distinct comparison and a US$10 limit for the whole run. You can also select the same file through **Choose a commentary preset JSON file**. [Full SA preset](../uploads/sa-full-local-replay-commentary.json) · [Cost estimate](live-commentary-costs.md#full-sa-local-publication)

For visual review, deliver one capture each time you want to see the next forecast. The prepared project is `live_runs/2026sa-manual-40k/sa-full-replay.pol2`. Its start-of-counting reference and each live forecast use 40,000 simulated elections. Open the project, run the saved model and projection macro with **F1**, wait for completion, then choose **Results → Watch / Resume**. Start the local uploader and leave it watching this run.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B uploads\upload_manager.py --watch 2026sa-manual-40k --local
```

Once the uploader reports that it has uploaded `origin`, run the following command **once** in a second terminal at the repository root. The feeder delivers one capture and exits. Wait for the upload, then inspect `/2026sa/replays/2026sa-manual-40k` on the local frontend. Repeat the command when ready for the next capture.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B live_scripts\replay_feeder.py --run-id 2026sa-manual-40k --step --codes-file live_runs\2026sa-manual-40k\captures.txt
```

The prepared run's `captures.txt` fixes the selected archive timestamps, starting with the genuine pre-count feed. For another run, omit `--codes-file` to select all timestamped feeds at the top level of that election's archive. Use `--dry-run` to check the selection and storage before delivery. After a break, the same single-step command picks up from the latest capture in the inbox. For an explicitly unattended rehearsal, `--after-local-upload` advances through the sequence automatically as each local upload succeeds.

### Review one capture remotely

This procedure lets someone away from the replay computer inspect the same update a website reader sees. Keep the GUI, local backend and frontend running. Use one upload manager for the run. When sharing operation, agree who owns its terminal and record its exact command, including the destination and any `--commentary-config` override.

1. On each request for the **next** update, run the replay's `.\live_runs\RUN_ID\Next.ps1` **once** from the Windows repository root. If that run has no step script, use the general feeder with `--run-id RUN_ID --step`. Record the delivered capture. If publication takes longer than expected, check that capture's progress before doing anything with the feeder.
2. Wait for the corresponding forecast ID to be accepted in `live_runs/RUN_ID/receipts/local.json`, or for its `Uploaded RUN_ID/FORECAST_ID to local.` console line. Check the comparison statuses in `packages/FORECAST_ID/publication.json`. An upload can succeed with a commentary deadline notice.
3. Open `/2026sa/replays/RUN_ID` on the local frontend (substitute the election code for other replays). In **Recent Changes**, select **Period → Previous Update** and **Display → Text**. Match the displayed observation time to this forecast, including the timezone. Capture the rendered card at mobile width (about 414 pixels), splitting long reports at a heading or bullet boundary. Save screenshots under the run's `mobile-review/` directory with the forecast ID in each filename. Include a visible deadline or validation notice if that is what the reader sees.
4. Send the screenshots with a brief account of the main seat movement, median statewide TPP, primary votes and seat totals. Give each statewide preference-flow median and its middle 50% range, naming the source party and both finalists. Include the FP and TCP counts and their percentages of expected formal votes. Wait for the reader's next instruction before delivering another capture. Other page views can be captured from the same forecast.
5. If the commentary worker needs restarting, finish the current upload and pending requests first. Restart the manager for the same run, destination and commentary preset. When handing back the terminal, stop any background manager you started for the review and confirm its lock has been released. In WSL, from the repository root, `flock -n live_runs/RUN_ID/publisher.lock true` returns success when the lock is available and releases it immediately. The lock file can remain in place between sessions.

## Understand the comparisons

Winning chances and vote shares are percentages; their changes are **percentage points**. A rise from 40% to 55% is a 15-point rise. A TCP estimate always names its two candidates: a Labor–Liberal margin is not compared as though it were a Labor–National margin. The output separately records pairing probabilities, candidate changes, FP and TPP distributions, counting progress, expected seats and parliamentary outcomes.

Every calculated forecast compares with the previous successful calculation in the same run, whether or not that earlier calculation has reached a server. The since-start reference is the original pre-count forecast and remains fixed after input changes. That reference has no source observation time; its elapsed interval is explicitly unavailable rather than assigned an invented count timestamp.

Before 6 am on the day after election day, the rolling comparisons use the latest forecast at or before 15 minutes and one hour earlier. That election-night cutoff still uses the election's named timezone. Afterwards, **Today** replaces the rolling 24-hour comparison: it uses the last forecast before **5 am Sydney time** as its starting point. A forecast stamped exactly 5 am belongs to the new day. Between midnight and 5 am, the previous reporting day remains in progress; the exported report date identifies it explicitly.

The **last three hours** comparison is included only from **1 pm Sydney time** through the remainder of that calendar evening. Before 1 pm, the post-count comparisons are previous forecast, today and since counting began. A missing three-hour key means it is not scheduled, rather than that an API request failed. `Australia/Sydney` supplies AEDT during daylight saving and AEST otherwise, independently of the feed timezone. For example, the afternoon window begins at 12:30 pm in South Australia during these March replays. Advanced run settings may override `reporting_timezone`, `day_start_hour` and `three_hour_start_hour`; defaults require no project changes.

Every comparison records its actual reference and elapsed interval. For example, a 9:31 forecast whose latest eligible one-hour reference is 8:24 reports 67 minutes. Today likewise shows the last available pre-5-am forecast, which can be from the previous evening. Missing history is labelled unavailable; no interpolation or substitute origin is used for today. Replays use feed observation time, not delivery speed or the computer's current clock. Daylight-saving transitions use calendar boundaries rather than assuming every day lasts 24 hours.

**Yesterday's results** retains the last forecast's daily report from the preceding 5-am-to-5-am period. For the first election night, its since-start report supplies that account. The worker saves a small reference in `daily-reports/`, preserving the original comparison, commentary, evidence and coverage times in the existing immutable publication. This does not generate another LLM request or copy diagnostics. The selected forecast is fixed when the next day's first package is released; later arrivals do not rewrite it. If its publication is still pending, a subsequent package can expose the same selection once ready. Missing days and missing daily comparisons remain explicitly unavailable, and failed commentary is not silently regenerated.

Existing released packages remain unchanged. Older prepared comparisons can still contain the former `24h` window; newly prepared post-count comparisons use `today`. The initial-count benchmark uses election-night windows; its selected consecutive comparisons are independent of this reporting-day change.

Mechanical winning-chance changes are ordered by absolute movement. Within-window minima and maxima expose reversals which endpoint differences can hide. Input revisions are explicitly marked. Commentary must not describe an input/configuration change as incoming votes.

Explanatory evidence includes changed booth counts and projections, preferences learned locally and across seats, regional/statewide estimates, outstanding-vote estimates, pairing changes, recovery notices and use of manual preference samples. A new count's difference from an earlier expectation is descriptive evidence. It is **not** an exact decomposition of the change in winning probability.

<a id="two-capture-sa-commentary-check"></a>

<a id="three-capture-sa-commentary-check"></a>

## Four-capture SA commentary check

This check uses the genuine empty count and the next three results captures to examine commentary quality and cache use. It requires access to the four SA feeds. In the GUI, configure a private replay as described in [Set up a run](#set-up-a-run), select commentary **Off**, then choose **Watch / Resume**. Start the reporting worker with `--prepare-only` to save numerical forecasts, mechanical comparisons and benchmark evidence locally.

After the start-of-counting reference finishes, deliver one capture from the repository root. Substitute the replay ID you saved in the GUI.

```powershell
.\live_scripts\Step-SaCommentaryTrial.ps1 -RunId RUN_ID
```

Wait for each forecast to finish before running the command again. The script only delivers files; the GUI calculates the forecasts. It stops after these four selected captures, even if run again:

| Capture code | Observation time in SA | Recorded votes |
| --- | --- | --- |
| `20260321183853` | 21 March, 18:06:32 | No FP or TCP votes |
| `20260321192846` | 21 March, 18:57:46 | 2,725 FP votes across ten districts; 376 TCP votes in the replay export |
| `20260321193347` | 21 March, 19:00:39 | 3,136 FP votes; new FP in Bragg and Narungga, new TCP in Hartley, and a one-vote correction in Narungga |
| `20260321193448` | 21 March, 19:03:09 | 3,932 FP votes; new FP in Flinders, Hammond, Kaurna and West Torrens, plus TCP in Flinders |

The capture filename records a later collection time; comparisons use the observation clock inside the feed. The earlier synthetic test XML is excluded. `-Inbox PATH` selects another inbox; `-DryRun` checks selection and volume without copying. These captures require about **20.1 MiB** of inbox copies plus the same volume of retained original files, followed by the normal forecast exports.

The no-results capture receives local no-count text. To compare models on a counted update, use the [benchmark dry run](#configure-commentary) with the two forecast IDs from your run. The benchmark command requires `--confirm-paid` for API requests.

## Full SA two-model commentary check

This trial compared Terra Standard/low and Sol Standard/low throughout the
retained **2026 SA** count. It extended the four-capture check to late counting,
changed TCP pairings and manual preference-count evidence.

The original whole-count comparison used a locally prepared project. Operators with the SA feeds can use the GUI and [replay feeder](#replay-feeder) to collect forecasts in their own run and benchmark selected pairs using the [saved-evidence command](#configure-commentary).

The original trial delivered each of the **153 retained genuine captures**, from
`20260321183853` to `20260401180121`, only after the previous numerical
forecast succeeded. The first capture is genuinely empty; synthetic and
redundant files in archive subdirectories are excluded. The new run identity,
`2026sa-commentary-full-20261118`, has its own inbox, accepted history and
outputs. Each capture gets a forecast rather than being skipped by the normal
newest-feed policy. The separate zero-count origin also uses the main run's
settings and iteration count.

With paid commentary selected, the two model groups start once all forecasts
and mechanical comparisons are ready. They run concurrently, each processing its updates in order. There is one
API attempt per model per **previous-update comparison** that passes the
activity threshold, with no paid warm-ups or retries. Unchanged/sub-ten-vote
updates receive local notices. Since-start and other reporting windows are
prepared mechanically but do not add paid cases in this test. The archive
estimate is about **147 comparisons / 294 requests**, approximately
**US$9–11** with similar prompt and response lengths. The explicitly configured
**US$15 aggregate limit** applies to both models combined; reaching it leaves
unavailable receipts for further commentary and preserves numerical results.
An interrupted run reuses completed/started receipts rather than paying twice.

Both models use `service_tier: default`, `reasoning_effort: low`, an
8,192-token output ceiling and a 180-second request timeout. These are in
[`sa-commentary-full-benchmark.json`](../uploads/sa-commentary-full-benchmark.json).
The helper reports progress after each response. All outputs stay local;
no upload process starts.

**Allow roughly 3–5 GiB of additional disk space for the full run.** The
archive contains about 776 MiB of selected feeds; inbox and retained-original
copies together use about 1.52 GiB. Diagnostics, change-only history and
comparison evidence add further storage. The total is an estimate from the
small trial, not a hard disk limit. Preparing the project adds only about
15.5 MiB and copies no feed archive. No full accepted-count files are created.

Results are under `live_runs/2026sa-commentary-full-20261118/`:

- `snapshot_*.json` and compressed analysis sidecars provide the normal
  diagnostic exports for Live Booths.
- `benchmark/review.html` displays the comparisons, commentary and validation
  outcomes; `benchmark/benchmark.json` contains timings, usage and cost receipts.
- `benchmark/calls/` retains each response as it completes, including rejected
  responses. The combined review is refreshed as model groups finish.
- `trial-status.json` records forecast/commentary progress and actionable errors.

To compare Sol 6.2 with the completed SA trial, run the following from the
repository root. The first command rechecks the saved 6.1 responses locally.
The second starts or resumes the paid comparison using the configured API key.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B live_scripts\compare_sa_sol.py
.\analysis\.venv-win\Scripts\python.exe -B live_scripts\compare_sa_sol.py --start
```

Both commands use the 146 forecast pairs in the original benchmark manifest.
New paid calls in this model-only comparison require the original prompt revision.
The paid step requests only Sol 6.2 at Standard speed and low reasoning, with
a US$7 aggregate ceiling. [OpenAI lists the same Sol rates for both versions](https://openai.com/index/introducing-gpt-6-2-astra-sol/).
The original forecast and comparison inputs already contain the evidence
needed, including the input revision at restart. Results for the three models
are grouped by update in `model-comparison/review.html` in the full replay
folder. Original receipts and the separate resumable Sol 6.2 receipts remain
available in `benchmark/` and `benchmark-sol62/` respectively.

For a comparison of **commentary revisions**, the following command starts or
resumes Terra 6.1 and Sol 6.2 on the same 146 successive forecast pairs. Both
use Standard speed and low reasoning. The combined spending ceiling is US$15.
Omit `--start` for a local check of the saved inputs and settings.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B live_scripts\compare_sa_sol.py --revised-prompt --start
```

The script rebuilds the writing evidence from the saved counts and forecasts.
It preserves the reference time, count activity and any reversals recorded in
the original comparison. It uses the existing API key and writes a separate
ledger, receipts and `review.html` under `benchmark-<prompt-version>/` in the
full replay folder. The earlier paid trials used revision 6. Their costs and
prose reflect the instructions and evidence available in that revision.

For a pause or restart, keep the project and its entire run folder together.
Resume the same project to continue. A deliberately new paid run needs a new
identity; do not delete individual receipts to force repeat requests.

## Configure commentary

In **Configure watching**, choose **Off** or **Choose a commentary preset JSON file**. For `2026sa`, the menu also offers **SA trial** (US$1) and **SA full run** (US$10). A custom preset can start from `uploads/live-commentary.example.json`. Select the model, verified token prices and a finite run spending limit. The program copies settings into `run.json`. Later edits to the preset file apply when you select it again. Keep `OPENAI_API_KEY` in the environment or `uploads/.env`.

Pause the forecast watcher and stop the uploader before changing the preset for an existing run. Resume forecasting and restart the uploader with the same run ID to apply the choice to future forecasts. Earlier releases retain their commentary and spending receipts. Scripts can still use `--commentary-config` as an explicit override. `--prepare-only` prepares numerical packages unless an explicit override enables commentary.

### Compare the two commentary writers

This trial lets a reader compare two accounts of the same update. In **Recent Changes → Text**, choose **AI commentary** or **Rule-based commentary**. The website remembers the choice across comparison windows and forecasts. Quiet windows share their usual counting notice. Where the AI misses its deadline, a rule-based report can still be read.

Both editions put a close district's counted TCP lead in the context of the estimated votes still to come when every reporting unit has a projection for the same two candidates. This can use the booth TPP projection in a Labor–Coalition count when it covers exactly those opponents. A district can have a measured lead even when its remaining vote cannot be estimated this way. The rule-based edition also gives the previous lead and the projected final share when the continuing contest is close.

An election can supply the commission's names for vote categories in `forecasts/<election>/commentary-vote-types.json`. Each entry maps an internal category to its full public name, such as `"PrePoll": "Early Voting Absent Ordinary Votes"` in the 2026 SA feed. This is a separate count from both early voting centres and Early Voting Declaration Votes. The upload manager reads the file on startup and applies the names to both editions when releasing a package. Original evidence and local model responses retain their source identifiers.

The SA full-run preset enables the trial. For another election, add `"compare_writers": true` to its commentary preset. With an uploader already using a preset-file override, restart the uploader with its usual command before feeding the next capture. This reads the updated file. With GUI-managed settings, select the updated preset again so that the run saves the new option. The uploader prints `rule-based comparison on` at startup when it will publish both editions.

The local writer reads the same frozen comparison as the AI and makes no API requests. It prioritises changes in who is likely to win, uses matching booth estimates where available and puts a counted lead in the context of the votes still to come. Its headline can also identify a large recheck or a transfer between counting categories. It selects up to three district sections. This makes omissions and editorial choices visible when comparing the two accounts. The usual prices, spending limits and publication deadline still apply to the AI edition during the trial.

District headlines give the size of a win chance movement in percentage points. The district section gives the resulting chance and the supporting figures. The rule-based headline can name a booth or declaration category when the section compares its result with the earlier estimate and that count represents at least a quarter of the district's changed ballots. In both editions, a headline that singles out one such result also mentions other local counting when another booth or declaration category changed in the same district. FP and TCP from one reporting unit count as a single update.

A one-seat change in a statewide median can occur when the chance of reaching a seat total moves from just below 50% to just above it. The rule-based headline includes this change when the simulated chance of reaching the higher total moved by at least five percentage points in the same direction. For example, a move from 43% to 56% for six or more seats supports reporting a median that rose from five to six. The writer can report a movement of two or more seats directly from the published medians. It names up to two parties in one statewide bullet.

If the rule-based report would otherwise be quiet, it can give one brief local update. A win chance must move by at least half a percentage point, with a matching earlier estimate for a booth or declaration count where at least 100 ballots changed. These are editorial thresholds for choosing what to mention.

In a district already selected for coverage, the writer can also explain why a change of likely opponent mattered. For example, in Ngadjuri the chance of Penny Pratt and David Paton reaching the final two together fell from 5.1% to 0.3%. Both forecasts favoured Pratt in that contest. Paton therefore gained as this route to a Pratt win became rarer.

The writer uses the simulated results for the same two candidates at both ends of the comparison. It measures how much the overall win chance moved because the pair met more or less often, holding the win rates in that matchup and in all other simulations at their two-forecast averages. It also measures changes in those win rates. To earn a sentence, the frequency effect must point in the direction of the overall movement and be at least twice as large as the other two effects combined in absolute size. The overall change must be at least two percentage points. Both forecasts must give the same candidate at least a 60% chance of winning the matchup, with at least 20 simulations inside and outside it at each date. The writer also checks that this candidate's chance of making the final two moved with the matchup, allowing for rare pairings omitted from the export. The sentence explains how the forecast changed. Identifying the booths responsible requires separate count evidence.

New presets use `live-changes-37`. When two candidates together have at least 95% of the projected wins, both writers are directed to lead with the current favourite's movement. In a wider contest the subject is the candidate whose win chance moved most in either direction. Districts still earn coverage on the size of their news, including reversals and substantial local counting.

When commentary gives a counted lead or a booth result, it also gives the district forecast so readers can judge what that count means for the outcome. For a continuing TCP with complete turnout coverage, the writer can estimate the remaining split from the median district share against the same opponent. It applies that share to expected final turnout and subtracts votes already counted for the candidate. Both the amount outstanding and how it will divide are estimates and can include revisions. Where published district counts identify an earlier change of opponents, further counting for the revised pair in a close seat can be reported with the net votes added and the number of booths or declaration categories reporting. Both candidates' published tallies must agree with the booth totals at both dates.

Describing a preference flow as stronger or weaker than the forecast had expected compares it with the earlier central estimate. A flow can differ from that estimate while falling comfortably within the simulated range. When commentary describes a change in seats, it uses the median shown in Seat Totals for each forecast. This is the middle seat count across simulated elections. Party references carry the same election-specific badges as the forecast display.

When a likely final pairing becomes more favourable to one candidate, commentary can identify a district whose counts helped shift estimated preferences. It compares the shared ordinary-booth estimate with the result of holding that district’s evidence at its earlier level. A source is named only when the calculation reproduces both forecasts and its direction agrees with the estimated allocation of outstanding preferences and the change in win chance against that opponent. This check measures influence on the preference estimate. Several changes can affect the same seat’s overall win chance.

When the SA feed explicitly reports zero TCP votes in a district at both comparison times, the mechanical comparison records changes to previously usable TCP ballots separately. These changes can affect a forecast while published progress stays the same. For example, a first-preference recheck can leave fewer booths with usable earlier counts for a likely final pairing. If the revised booth estimates help explain a material shift in that contest, the writer receives the booth names, the remaining coverage and, when available, the local estimate of how other candidates’ preferences split. For explanations based on counts in another district, the writer identifies where preferences were counted and whose forecast they affected. A conditional win chance names both candidates in the sentence giving the figure.

When a district starts counting for different TCP opponents, the commentary gets a combined count across every reporting unit showing votes for the new pair. To judge whether that count differs from expectations, the earlier forecast must cover the same two candidates in every contributing unit. Each unit’s earlier projected share is weighted by the TCP votes now counted there, so a booth supplying a quarter of the counted ballots also supplies a quarter of the comparison estimate. If any unit lacks that projection, the commentary compares the earlier and revised whole-district forecasts for the pair. It can separately report the partial count, including its size and measured share. Those booths may be more favourable to one candidate than the rest of the district.

When FP and TCP totals agree at every contributing unit, and both finalists have identifiable primary counts, subtracting those primaries gives an estimate of how all other first preferences split between the finalists. Individual source parties cannot be distinguished this way.

An individual booth earns the same weight for 500 ballots on a replacement pair as for 500 first TCP ballots. After one booth is selected from a district, its next booth needs 15% more ballots to outrank an equally placed district’s first example. After two it needs 30% more. Several large changes in the same district can take consecutive places. The allowance is 9,000 UTF-8 bytes for detailed evidence, including the figures attached to each booth, with up to 2,400 of those bytes for shared diagnostics. District summaries are supplied separately. Booth details omit projections already represented by the checked comparison and counts for an unchanged ledger. A small FP recheck alongside a TCP return can also be omitted from booth detail when it affects at most 50 ballots and less than 5% of both the earlier FP count and the TCP update. It still appears in the full comparison.

To inspect what the writer received, open the request’s `packages/<forecast-id>/commentary/<request-id>.input.json`. New files labelled `commentary-messages-v1` contain the actual message text, including numerical precision and cache boundaries. Older files at that path contain the local working evidence. The comparison’s working records remain in `requests.json` and the full forecast evidence in `evidence.json.gz`. Measured votes remain whole counts. Projected vote totals are rounded to whole votes for the request, and diagnostic proportions carry up to three decimal places. Quotable forecast shares and movements retain the display precision used by the validator.

For SA House of Assembly detail feeds, commentary compares the candidates and TCP totals published for each district at the two forecast times. When a previously positive district count goes back to zero with the same candidates listed, a planned change of pairing is the likely explanation. The replacement pairing is confirmed only when the feed names different candidates. A correction at a single booth can reduce the statewide count without establishing a district-wide reset. Earlier usable ballots can still inform the forecast for the original pair, including a Labor–Coalition TPP estimate, while the reported progress follows the current count. The comparison checks the booth records before saying earlier counts are still in use.

When the affected districts account for the entire statewide drop, the statewide headline gives the number of votes removed from reported progress and the remaining total. It explains what the feed shows about pairing changes and how earlier counts still inform estimates for the original candidates. Any individual booth correction appears under its district.

A booth’s first preferences normally occupy one bullet. Readers see the size of the count once, alongside the noteworthy candidates’ shares and how far each was above or below the earlier estimate for that booth. The same applies to a declaration category. For example, “In the Postal count” introduces the district’s postal results, even when the feed lists “Postal” as a booth name. A correction can warrant its own explanation. A separate TCP statement names the opponent and compares the observed split with the earlier estimate for that pair. A gap that rounds to zero is described as matching the estimate. Where that pairing had no booth estimate, the writer can still describe first preferences or a significant count correction.

When a headline links a district forecast movement to local counting, the writer selects a booth or declaration category, a candidate and either FP or TCP. The exporter uses the same comparison window to name the location in the headline and give the candidate’s measured share, the size of the count and its difference from the earlier estimate in the district section. A TCP comparison names both candidates. If the selected comparison is missing or fails validation, the headline gives the change in outlook on its own.

The writer attributes a correction to the named booth or declaration category where the evidence allows it. A booth can revise its primary total and report its first TCP count in the same update. Each tells readers something different about the ballots counted there. District totals are useful when several places contribute, the district lead changes, or only the aggregate figure is available.

A statewide trend gets its own headline with the current estimate and signed change for each movement mentioned. District sections explain significant changes in win chance. A candidate's projected lead and the range of plausible results can both narrow as more votes arrive. The writer checks the middle 90% of projected results against the same opponent in both forecasts and uses the reported win chances to say what that meant for the race. The leader can become more likely to win, stay about as likely, or lose little ground in win chance even as the expected margin falls substantially. A tiny movement rarely warrants a story by itself. When it helps explain a larger development, the writer gives its size and direction alongside the current level. For example, a three-point fall in first preferences can be worth reporting alongside a win chance of 99.2% (+0.2 pp). For a district with unchanged counts, a movement elsewhere in the same direction can help explain why the local outlook shifted. The writer can qualify that explanation when the evidence is limited. The exporter checks figures and supplies the headings and lists. Saved versions 9 through 22 retain their earlier bullet prompts and version 8 retains paragraphs. [Website rendering contract](live-publication-v1.md)

Settings record the prompt revision, model, requested service tier and output limits alongside each request. Use `service_tier: "default"` for Standard, `"fast"` for Fast, or `"ultrafast"` where supported. Standard is explicit so an account-level default cannot silently select a premium speed. Actual response tiers, cache reads and cache writes are logged. Cost estimates include write rates and use the served tier's documented price multiplier when a Fast request is served as Standard.

Requests put reusable editorial instructions, election labels and table definitions first. The all-seat forecast overview follows. Only the final section depends on the comparison window. It groups checked figures by district and booth, and includes the selected candidate counts. The unrounded booth projections used to derive those figures stay in the local evidence. The worker requests a 30-minute cache entry for the overview only when distinct comparisons can share it. An adjacent-update benchmark therefore caches only the election reference and instructions. The receipts show actual cache reads and writes. [OpenAI prompt caching guide](https://developers.openai.com/api/docs/guides/prompt-caching)

Credentials are never written into requests or publication packages. The spending limit is **per run**; the benchmark has its separate aggregate ceiling. Failed requests without a usage receipt retain a conservative maximum-cost reservation. To avoid an accidental duplicate paid call, restart marks an interrupted request unavailable instead of submitting it again automatically. The worker uses the Responses API with [Structured Outputs](https://developers.openai.com/api/docs/guides/structured-outputs); incomplete responses and refusals are treated as unavailable commentary, subject to bounded retries.

Each comparison checks observed FP/TCP counts before requesting commentary. Unchanged counts produce **“No new counting occurred…”** locally. Changes below `minimum_count_change` (default **10 votes**) produce **“There were no significant changes to the results…”**. Forecast movements caused solely by elapsed time do not trigger a request. Configuration revisions remain visibly marked. Missing count diagnostics are treated as unknown, not as a zero count.

The size measure includes candidate corrections: moving ten votes between candidates counts as ten affected ballots, even when the total stays unchanged. At each booth it takes the larger FP or TCP change, then adds across booths, avoiding counting the same ballots twice. Published manual 3CP/4CP allocations also count as new information when FP/TCP feed totals stay unchanged; changes in simulated use of an unchanged sample do not. Those separate counts do not represent extra formal turnout. This is an editorial size measure because actual overlap is unknown. Corrections followed by restorations between saved forecasts also count as activity. The filter operates separately on each window: the previous forecast can have no new counting while the since-start or longer-window comparison still warrants commentary.

For an individual update the writer usually aims for 100–160 words. It first considers districts where local counts changed, ordered by their largest comparable candidate win chance movement. A booth result can help explain a movement, but the timing alone does not establish its cause. A final-pairing change is worth including when it helps explain a competitive race. Several booths showing the same pattern can share a bullet. A statewide trend appears near the top, with a district example when that adds information. A major correction or a shrinking counted lead can still be the main story even if win chances barely moved. Longer windows can cover more distinct developments.

The request has room for detailed evidence on ten districts. Up to three places go to the largest candidate win chance changes of at least one percentage point in districts with new or revised counts. For the remaining places a one-point win chance movement earns one point of editorial priority. A booth update affecting 100 ballots earns two points, 200 earns three and 400 earns four, up to a maximum of ten. Each district receives its strongest score, so a large booth and the movement it explains do not count twice.

Changes in a possible final pairing earn at most eight points. A 95% favourite at both endpoints reduces that weight to a tenth because a change of opponent usually has little bearing on the outcome. When a projected TCP share is near or crosses 50%, movement in its median and the width of its middle 90% interval can also earn up to eight points, scaled by the likelihood of that contest. All of these weights guide the choice of stories. The mechanical report retains the full numerical changes.

For a declaration category that loses votes, the worker checks every other category in that district before selecting booth evidence. A unique, equal and opposite change in the same candidates' allocations across the FP and TCP accounts is labelled an apparent transfer. For example, if 500 absent votes appear under Postal and another 30 provisional votes arrive, the transfer contributes zero to district growth. The district has gained 30 votes. Partial or ambiguous matches remain individual changes. Readers can still see each changed record in the mechanical report.

For a close TCP the commentary can use the counted lead, the net addition for each finalist, and estimated votes still to come. When the candidates switch places, the commentary names the candidate who moved ahead and says how many votes they were behind and how many they now lead by. Both margins come from counted votes for the same pair. A race qualifies for this extra detail when either the counted share or the forecast median is within five percentage points of half at either endpoint. The worker sums all booths for the same pair and compares the estimated final TCP total with the votes counted. A missing projection for a booth with expected votes makes the remaining count unknown. In a quiet update, the writer can give a single sentence saying there were no significant changes to the forecast. The mechanical report still lists the changes.

Distinct comparisons run independently with bounded concurrency. Identical current/reference requests share a response. The worker uses numerical evidence, not previous commentary. It validates seat identities, evidence references and numerical facts before acceptance. Numbers appear directly in prose, in bold, using prepared display figures such as **47.9% (+7.5 pp)**. Here pp means percentage points, not a relative percentage change. Candidate, party and seat placeholders let the website insert names and badges. The response contains only a summary, paragraphs with their associated seat lists, and an insufficient-evidence flag; numerical and identity references are checked locally without making the LLM repeat a separate facts list. These checks catch unsupported numbers, units and identities, but do not establish that a causal explanation is correct. Explanatory judgement still requires benchmark review.

For a booth result the writer receives the candidate's observed percentage and the number of formal votes counted so far at that booth. For example, 42 of 93 first preferences is a 45.2% share. A TCP percentage uses the count for its two candidates, which can cover a different number of ballots from the FP count. When a booth result differs from expectations but the district outlook barely moves, the writer can use the estimated district FP count progress to put the result in proportion. A change in a lead or a reassignment of ballots can still warrant an individual vote count. Validation checks the figures supplied for the district. It does not establish that prose assigns every figure to the right booth, candidate or cause.

When a booth's TCP count changes, commentary compares it with an earlier estimate for the same two candidates. For a contest using the usual booth projections, this comes from the booth's TCP estimate or, for a matching Labor–Coalition pairing, its TPP estimate. The observed and projected percentages can be compared even when the count sizes differ. For example, 83 Labor votes out of 133 is 62.4%. Against a booth estimate of 59.1%, that is 3.3 percentage points above the earlier estimate, equivalent to about four votes at the current count size.

For a pairing using live preference learning, the ranked model exports a central estimate for first preferences still awaiting TCP in each voting category of the district. A booth comparison is possible when all those outstanding ballots were at one booth and its TCP subsequently covers them. When several booths contribute or a reporting unit counts only some of the ballots, commentary can compare estimates for the whole original group at both dates. The updated estimate combines the new counts with an estimate for the remainder. For example, if 1,528 of 1,572 provisional ballots have a TCP, both projected percentages refer to all 1,572. Quoting the measured percentage for the 1,528 would require an earlier estimate for that subset. The same distinction applies when two early voting centres contributed to the earlier estimate and only one has reported. Changes in first preferences or incomplete evidence can prevent this comparison.

The commentary prompt favours these comparisons over context-free count lists. A small booth difference with little forecast movement need not receive a paragraph; large corrections and meaningful vote changes remain relevant even when win odds barely move. No new numerical threshold changes the forecast or declares a count statistically surprising.

The publication deadline includes queue time and starts when the numerical forecast completes. Normally all applicable commentary completes first. At the deadline, the package includes numbers, mechanical comparisons, any completed commentary, and explicit unavailable notices. A released package is immutable. Late responses remain local and never amend the published report. API failure and budget exhaustion use the same deadline rule. With commentary explicitly disabled, numerical packages are released immediately.

An opt-in benchmark compares selected model/speed variants on identical saved SA requests. First inspect the example price catalogue and perform a free dry run:

```powershell
Set-Location uploads
..\analysis\.venv-win\Scripts\python.exe -B -m live_pipeline.benchmark --run ..\live_runs\RUN_ID --models sa-commentary-benchmark.json --output ..\live_runs\RUN_ID\benchmark --forecast-id f00000002 --reference-forecast-id f00000001 --limit 1 --budget-usd 10 --dry-run
```

Check the actual forecast IDs in a manually operated run; the prepared helper obtains them from the acceptance ledger. `sa-commentary-benchmark.json` contains **12 variants**: Terra Standard/Fast, Sol Standard/Fast/Ultrafast, and Astra Standard, each at low and medium reasoning. High reasoning is omitted from the current trial selection because the initial tests took longer without a clear improvement in commentary; it remains supported by the benchmark runner. It uses [published OpenAI prices](https://developers.openai.com/api/docs/pricing) checked on 17 November 2026. Astra's API name is `gpt-6-astra`. Each request has the same 8,192-token output ceiling, including reasoning tokens. This is room for reasoning, not a request for longer commentary; prose remains approximately 150–250 words or less. These are comparison settings, not a selected deployment model.

[The cost comparison](live-commentary-costs.md) applies those rates to SA and Victorian capture schedules, distinguishing assumed token volumes from measured request counts and prompt bytes.

To try revised evidence selection on a completed replay, add `--refresh-evidence` to the benchmark command and choose a fresh output folder. This rebuilds the selected inputs in memory using the saved booth counts and forecast summaries. It preserves the original comparison times and within-window reversals. With `--dry-run` the check uses no API credit.

For manual use, replace `--dry-run` with **`--confirm-paid` only after reviewing costs**. Between one and 24 distinct model/speed/reasoning configurations are supported. The default aggregate ceiling remains **US$10**; an explicit budget can be up to **US$15**, as in the full two-model replay above. Dry-run reservations bound tokens by UTF-8 bytes and therefore deliberately exceed likely charges; actual input/output tokens and cache writes are recorded by paid trials.

The trial selects the first counted forecast against the actual empty-count capture, then each subsequent counted forecast against its immediate predecessor. Other windows with the same pair share it. Additional since-start comparisons and the separate pre-count origin are not benchmark cases. Unchanged or insignificant counts receive local notices and do not add API calls. Each variant gets **at most one API attempt per pair**, with no warm-up calls or retries, even after a crash. A durable started record prevents resending an attempt whose outcome is unknown. Reusing the same benchmark folder preserves receipts and spending. Changing the prompt or evidence requires a new output folder rather than mixing incompatible trials.

The benchmark caches the same election and current-forecast prefixes as the ordinary worker, leaving the comparison suffix uncached. The first trial wrote the entire comparison on every call without recording any cache hits; reuse across different model, reasoning or speed choices is therefore not assumed. Up to three model/reasoning groups run concurrently. Each exact model/speed/reasoning configuration processes its three updates consecutively. Within each group, speed variants run sequentially; their order rotates between reasoning levels so Fast is not always the warmed request. The recorded cache usage still matters when comparing speeds. Each request gets up to 180 seconds for measurement, separately recording whether it met the state 90-second and federal 30-second targets. Waiting behind other benchmark variants does not consume this measurement allowance. Ordinary publication deadlines continue to include queue time from forecast completion.

Review validated facts, explanation quality, deadline compliance and cost before explicitly selecting the deployment model. Ordinary automated tests use mocked responses and incur no API charges. The four-capture check has no one-hour anchor yet; it does not replace later tests of multiple parallel windows, TCP realignments and manual preference evidence.

## Recovery and inspection

The progress window shows received, accepted, forecasted and published observations, queue counts, reporting status and actionable errors. Details are in `state.json`, the per-arrival records, and `receipts/`. The collector runs while calculating but cannot modify count history. The worker takes a fixed set of arrivals, accepts them in source-time order and then selects a forecast. Older late arrivals can inform later forecasts but cannot rewrite an existing output.

A failed numerical forecast does not discard accepted counts or the last successful result. The next distinct feed gets an attempt. Missing configuration, an unreadable authoritative history, changed inputs or a storage failure pauses forecasting. Resume explicitly after correcting the cause. Unexpected changes on automatic restart require attention. Baseline and betting-odds reuse still follows the existing input-consistency checks; incoming feed captures do not invalidate those pre-count components.

On Windows, a reader can briefly prevent replacement of a state file. The forecasting worker retries these access/locking failures for up to two seconds; a persistent failure includes the Windows error number and description. The last published state remains intact. A leftover `.tmp` file is an uncommitted replacement, not proof that the forecast completed: retain it for inspection rather than manually replacing `state.json`. Resume the same run after fixing the cause so its completed packages and commentary receipts remain reusable. The commentary trial reads the capture ledger once per polling cycle, irrespective of how many forecasts have already completed.

The upload manager prints a receipt line with the run ID, forecast ID, actual receipt time in UTC and the feed's source time when it accepts a new numerical handoff. With commentary enabled it also reports how many distinct comparisons completed and the elapsed time from dispatching the first API request to receiving the last response. Overlapping requests count as one elapsed interval, including any retries between those endpoints. Shared comparisons count once across timeframes and destinations. Quiet updates can finish entirely locally. The console identifies replies that arrive after the publication deadline and prints a separate acknowledgement for each successful upload.

The upload worker maintains separate acknowledgement/retry files per destination. A failed server does not prevent another destination accepting the same package. Requests use stable run/forecast IDs and a payload digest; the server must reject a different body under an existing identity. An older report completing late may enter history but must not replace the latest observation. Keep the entire run directory together when resuming; selectively deleting its ledger or publication packages breaks that continuity.

Portable verification commands are:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\tests\run_tests.py test_live_automation
cmd /d /c run-portable-tests.cmd
```

These use fictional data, existing recovery functions and a local mock server. They do not run a full election forecast or contact the real website. Full numerical equivalence checkpoints, deployment-model selection, real server publication and the new VEC examples remain deployment verification steps.

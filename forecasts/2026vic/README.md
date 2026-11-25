# Victorian live forecast for 2026

These instructions connect the Victorian Lower House feed to the forecast for the private trial on Wednesday 25 November and the election on Saturday 28 November. On trial day, zero files start at 5:30 pm Victorian time. From 6 pm to 8 pm the VEC publishes fictitious results against the real candidates at five-minute intervals. Use the private replay to check that the local website receives each forecast. Tied first preferences and missing TCP figures are valid trial inputs. [VEC results data feed](https://www.vec.vic.gov.au/about-us/media/2026-results-data-feed)

## Before each event

From PowerShell in the repository root, refresh the inputs while the GUI watcher is stopped. For the trial, use the command below. Before the official count, run it again with `-PrepareFor election` to refresh the shared inputs and the election-day project.

```powershell
.\live_scripts\vec_2026.ps1 prepare -PrepareFor rehearsal
```

The command verifies the candidate roster against a Lower House zero count, extracts the candidate and location files to `downloads`, rebuilds the turnout prior, and matches the current ordinary venues to the 2025 federal House results for One Nation. The received setup ZIPs and any available test samples are retained in separate directories under `forecasts/2026vic/live-inputs/vec`. If the VEC replaces a setup ZIP under the same name, its earlier copy is kept under `setup/previous`. Each run starts with an empty inbox. The collector delivers fresh, timestamped zeros during the trial opening. [How federal geography informs the outstanding vote](../../docs/live-one-nation-booths.md)

The collector reads the signed **container URL** from the current user's encrypted file `forecasts/2026vic/live-inputs/vec-sas.dpapi`. On a new Windows account, paste the full URL from the VEC into the masked prompt below. The command also accepts the URL in the environment variable `VEC_SAS_URL`. Keep the URL out of project files and command history.

```powershell
New-Item -ItemType Directory -Force forecasts/2026vic/live-inputs | Out-Null
Read-Host 'VEC signed container URL' -AsSecureString | ConvertFrom-SecureString | Set-Content -LiteralPath forecasts/2026vic/live-inputs/vec-sas.dpapi
```

## Start a run

| Occasion | Project under `live_runs` | PowerShell collector command |
| --- | --- | --- |
| Private trial, 25 November | `2026vic-trial-1/2026vic-trial-1.pol2` | `.\live_scripts\vec_2026.ps1 rehearsal -Watch` |
| Official count, 28 November onwards | `2026vic-live/2026vic-live.pol2` | `.\live_scripts\vec_2026.ps1 election -Watch` |

1. Use an Analyser build that reads the three VEC early vote types separately and applies the Victorian One Nation declaration weights. Open the appropriate project, press **F1** and run the saved macro `run-model:0;run-projection:0`. Wait for the model and projection to finish.
2. Select **Results → Watch / Resume**. This calculates the start-of-counting forecast and watches the selected run's `inbox`. Start the matching collector if it is not already running. Leave the computer awake.
3. The collector checks Azure once a minute. Each validated Lower House ZIP appears in the inbox in one rename. The trial accepts zeros from the 5:30 pm publication and counts from the 6 pm publication through the final 8 pm cycle. The collector performs a final scan at or after 8:30 pm. Restarting it collects outstanding in-window files while they are still on Azure, including the following morning before the VEC's noon removal. Omit `-Watch` for one scan.

To start or restart the trial uploader, run the following from the repository root with the local backend running. Its console confirms each accepted upload. The local frontend displays the first counted forecast at `/2026vic/replays/2026vic-trial-1` and retains the subsequent history.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B uploads\upload_manager.py --watch 2026vic-trial-1 --local
```

The trial configuration publishes the numerical forecast and mechanical comparisons with commentary off. Its uploader accepts only the local destination. The trial and official count have separate inboxes, accepted histories and publication queues.

## Saturday's official count

The official run is `2026vic-live`. It publishes numerical comparisons and rule-written reports to the local, test and production servers. The commentary is generated once per comparison on this computer and costs no API credits.

1. After the VEC clears the trial and after saving the final pre-election project, refresh the official copy with the GUI watcher stopped. Run the read-only check again. It compares the installed roster with Azure, reports the feed credential expiry if available, and checks the live API on each destination. A failed destination check identifies which website needs attention. It does not prevent the collector from receiving files.

   ```powershell
   .\live_scripts\vec_2026.ps1 prepare -PrepareFor election
   .\live_scripts\vec_2026.ps1 check-election
   ```

2. Open `live_runs/2026vic-live/2026vic-live.pol2`, press **F1** to run its saved model/projection macro, then choose **Results → Watch / Resume**. Start early enough for the GUI to calculate the zero-count reference and calibrate betting odds. In separate PowerShell windows from the repository root, start the collector and official publisher.

   ```powershell
   .\live_scripts\vec_2026.ps1 election -Watch
   .\live_scripts\vec_2026.ps1 publish-election
   ```

   The collector accepts Lower House V2 ZIPs dated from the **6 pm AEDT publication on Saturday 28 November** and continues after midnight. It can start waiting before 6 pm. The first ZIP may contain XML created moments earlier. Reissued content under an existing filename is saved under `vec-conflicts` for inspection and the original stays in the official inbox. A setup zero can be several days old. The GUI uses the Saturday deliveries to start the official counting history.

3. Once the publisher has registered `2026vic-live`, select that run in each destination's Django admin under **Live runs → Select one public live run as official**. The first timestamped forecast then appears at `/forecast/2026vic/live`. Check each destination's `Uploaded` console line or `live_runs/2026vic-live/receipts` to confirm acceptance. The initial `origin` is a comparison reference.

The publisher command runs `upload_manager.py --watch 2026vic-live --all --idle-hours 0`, allowing it to wait through quiet post-election periods until stopped. If a server is unavailable, the others continue to receive forecasts and the publisher retries the missing destination. After a restart, open the same official project and restart the same commands to reuse the count history and upload receipts. [Live automation and recovery](../../docs/live-automation.md)

## What the forecast counts

Each ordinary polling place has its own estimate. For each district and candidate, the forecast reads three distinct early totals for first preferences and, when available, the two-candidate count. `EVCEarly` covers home-district early centres, `MobileVotingEarly` covers mobile early voting, and `Early` covers the remaining early votes. The named venue rows describe ballots already included in the district totals. When a capture omits a breakdown, the reader can recover earlier accepted counts for the same type. [VEC results data feed technical guide](https://www.vec.vic.gov.au/-/media/caa76d02dbe94dc481d8584db66af4c3.pdf)

The starting estimate for all early votes combined, and separately for postals, uses each group's 2022 share of formal votes, scaled to the current roll and expected formal vote. Narracan uses the pooled November 2022 rates and shares because its poll was postponed. The provisional relative standard deviations for these two counts are 15% statewide and 15% independently per district. [2022 Victorian election results](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results)

Within the early total, the 2022 recheck supplies the fraction cast at named home centres. For example, if 75% voted at home centres and the VEC expects one mobile voter per 25 centre voters, the early turnout estimate divides into 75% home, 3% mobile and 22% residual. Using that attendance ratio assumes similar formality and home-district participation at mobile and fixed centres. The input audit records each district's three weights. The prepared historical comparison preserves every candidate's published early totals. It marks the mobile and residual split, the three-way two-candidate split and Narracan's three-way split as estimates. [VEC 2022 recheck workbook](https://www.vec.vic.gov.au/-/media/c4665365795d431e811d0ce818e373d4.xlsx) · [VEC 2026 venue estimates](https://www.vec.vic.gov.au/-/media/19326aa361284a49a69b08194421da96.xlsx)

For One Nation first preferences, the 2025 Victorian federal results set the starting difference between home in-person voters and two declaration groups. Election-day absents receive the full within-division difference, postals receive half, and residual early voters start at the home in-person share. These estimates are balanced against expected turnout to reproduce each district's pre-election median. [How the declaration estimates work](../../docs/live-one-nation-booths.md#victorian-declaration-votes)

The historical comparison combines the 87 November 2022 district rechecks with the VEC's January 2023 Narracan supplementary election. For Narracan, the primary workbook supplies a complete booth and declaration breakdown for 37,503 formal votes. The recheck reports 37,205 formal votes but omits the detailed declaration split. The preparation uses the primary count consistently for that district. Each district's two-candidate comparison uses the VEC's published two-candidate table. The generator verifies the sum for every candidate separately. [Narracan supplementary election results](https://www.vec.vic.gov.au/results/state-election-results/state-by-elections-timeline/narracan-district-supplementary-election-results)

To rebuild the historical file independently, run the following from `analysis` using the Windows non-Stan environment. Add `--cached` to reuse the public source tables in `downloads/turnout/vec-2022-public`.

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.elections.prepare_vec_2022_booths
```

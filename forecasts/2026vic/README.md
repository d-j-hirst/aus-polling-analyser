# Victorian live forecast for 2026

These instructions prepare the Lower House forecast and collect the VEC results for the private rehearsal on Wednesday 25 November and the election on Saturday 28 November. Both start at 6 pm Victorian time. Use the project and collector for the same occasion. The rehearsal runs until 8 pm and uses fictitious first preferences on the real ballot. The VEC does not supply two-candidate results for the trial. Keep the rehearsal local. [VEC results data feed](https://www.vec.vic.gov.au/about-us/media/2026-results-data-feed)

## Before each event

From PowerShell in the repository root, refresh the inputs while the watcher is stopped. Run this after the VEC releases the revised polling locations for the trial, and again before setting up on election day.

```powershell
.\live_scripts\vec_2026.ps1 prepare
```

The command verifies the candidate roster against the Lower House zero count, extracts the candidate and location files to `downloads`, rebuilds the turnout prior, and matches the current ordinary venues to the 2025 federal House results for One Nation. The received setup ZIPs and any available test samples are retained in separate directories under `forecasts/2026vic/live-inputs/vec`. If the VEC replaces a setup ZIP under the same name, its earlier copy is kept under `setup/previous`. The zero file is for preparation. Both run inboxes start empty. [How federal geography informs the outstanding vote](../../docs/live-one-nation-booths.md)

The collector reads the signed **container URL** from the current user's encrypted file `forecasts/2026vic/live-inputs/vec-sas.dpapi`. On a new Windows account, paste the full URL from the VEC into the masked prompt below. The command also accepts the URL in the environment variable `VEC_SAS_URL`. Keep the URL out of project files and command history.

```powershell
New-Item -ItemType Directory -Force forecasts/2026vic/live-inputs | Out-Null
Read-Host 'VEC signed container URL' -AsSecureString | ConvertFrom-SecureString | Set-Content -LiteralPath forecasts/2026vic/live-inputs/vec-sas.dpapi
```

## Start a run

| Occasion | Project under `live_runs` | PowerShell collector command |
| --- | --- | --- |
| Private rehearsal, 25 November | `2026vic-rehearsal/2026vic-rehearsal.pol2` | `.\live_scripts\vec_2026.ps1 rehearsal -Watch` |
| Official count, 28 November onwards | `2026vic-live/2026vic-live.pol2` | `.\live_scripts\vec_2026.ps1 election -Watch` |

1. Use an Analyser build that reads the three VEC early vote types separately and applies the Victorian One Nation declaration weights. Open the appropriate project, press **F1** and run the saved macro `run-model:0;run-projection:0`. Wait for the model and projection to finish.
2. Select **Results → Watch / Resume**. Start the matching collector in its own PowerShell window. It can start before the first capture. Leave the computer awake and both programs running.
3. The collector polls once a minute for the VEC's five-minute updates. It checks the event, house and source date before delivering each complete ZIP. Stop it with **Ctrl+C** and restart with the same command to collect any missed files still on the feed. Omit `-Watch` for a single check. The VEC plans to remove the rehearsal results at noon on Thursday 26 November.

The two runs have separate inboxes and forecast histories. The official run is configured for public visibility when publication is enabled. The collector only delivers files locally. Use the existing [live automation instructions](../../docs/live-automation.md) for election-night publication. For the trial, leave the uploader off or use its local preparation mode.

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

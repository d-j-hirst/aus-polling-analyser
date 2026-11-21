# Victorian live forecast for 2026

These instructions prepare the Lower House forecast and collect the VEC results for the private rehearsal on Wednesday 25 November and the election on Saturday 28 November. Both start at 6 pm Victorian time. Use the project and collector for the same occasion. The rehearsal runs until 8 pm and uses fictitious first preferences on the real ballot. The VEC does not supply two-candidate results for the trial. Keep the rehearsal local. [VEC results data feed](https://www.vec.vic.gov.au/about-us/media/2026-results-data-feed)

## Before each event

From PowerShell in the repository root, refresh the inputs while the watcher is stopped. Run this after the VEC releases the revised polling locations for the trial, and again before setting up on election day.

```powershell
.\live_scripts\vec_2026.ps1 prepare
```

The command verifies the candidate roster against the Lower House zero count, extracts the candidate and location files to `downloads`, and rebuilds the turnout prior. The received setup ZIPs and any available test samples are retained in separate directories under `forecasts/2026vic/live-inputs/vec`. If the VEC replaces a setup ZIP under the same name, its earlier copy is kept under `setup/previous`. The zero file is for preparation. Both run inboxes start empty.

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

1. Use an Analyser build containing the VEC V2 reader. Open the appropriate project, press **F1** and run the saved macro `run-model:0;run-projection:0`. Wait for the model and projection to finish.
2. Select **Results → Watch / Resume**. Start the matching collector in its own PowerShell window. It can start before the first capture. Leave the computer awake and both programs running.
3. The collector polls once a minute for the VEC's five-minute updates. It checks the event, house and source date before delivering each complete ZIP. Stop it with **Ctrl+C** and restart with the same command to collect any missed files still on the feed. Omit `-Watch` for a single check. The VEC plans to remove the rehearsal results at noon on Thursday 26 November.

The two runs have separate inboxes and forecast histories. The official run is configured for public visibility when publication is enabled. The collector only delivers files locally. Use the existing [live automation instructions](../../docs/live-automation.md) for election-night publication. For the trial, leave the uploader off or use its local preparation mode.

## What the forecast counts

Each ordinary polling place has its own estimate. For each district and candidate, the forecast adds the VEC's residual `Early`, `EVCEarly` and `MobileVotingEarly` vote counts into one Early account. This applies to both first preferences and the two-candidate count when available. The individual early and mobile rows in the ZIP describe votes already included in that account. The original ZIP retains them for later analysis. When the VEC supplies only a district total, earlier accepted booth and declaration counts can still be used. A district without earlier detailed counts has no observed breakdown.

The starting estimate for Early and Postal uses the share of formal votes each accounted for in the district in 2022, scaled to the current roll and the turnout model's expected formal vote. Narracan uses the pooled November 2022 rates and category shares because its poll was postponed. The provisional relative standard deviations for these two counts are 15% statewide and 15% independently per district. They are assumptions about how much the 2026 counts could differ from the historical estimates. The input audit records the historical matches and starting weights for ordinary booths. Election-night observations then update these estimates. [2022 Victorian election results](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results)

The historical comparison combines the 87 November 2022 district rechecks with the VEC's January 2023 Narracan supplementary election. For Narracan, the primary workbook supplies a complete booth and declaration breakdown for 37,503 formal votes. The recheck reports 37,205 formal votes but omits the detailed declaration split. The preparation uses the primary count consistently for that district. Each district's two-candidate comparison uses the VEC's published two-candidate table. The generator verifies the sum for every candidate separately. [Narracan supplementary election results](https://www.vec.vic.gov.au/results/state-election-results/state-by-elections-timeline/narracan-district-supplementary-election-results)

To rebuild the historical file independently, run the following from `analysis` using the Windows non-Stan environment. Add `--cached` to reuse the public source tables in `downloads/turnout/vec-2022-public`.

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.elections.prepare_vec_2022_booths
```

# Automatic Live Results

This guide explains how to supply reported election results to live forecasts
and reproduce forecasts from retained result feeds.

The loaders match previous-election booths and candidates to the current
election, then pass counted votes to the live projection. Automatic live
simulations combine three kinds of input:

1. A previous-election result, used to match booths and candidates.
2. A current-election preload (candidate, seat and booth structure with zero
   votes).
3. The latest current-election results.

`LivePreparation` validates and loads these inputs. Jurisdiction-specific XML
and JSON interpretation is handled by `ElectionData`; the resulting previous
and current elections are passed to `LiveV2`.

Run the GUI from the repository root. Relative paths in this document assume
that working directory. Each simulation stores a **Current results directory**
in its `.pol2` settings. New simulations and projects saved before version 65
default to `<HOME>/Downloads`, which expands at runtime to:

- Windows: `%USERPROFILE%\Downloads`
- macOS and Linux: `$HOME/Downloads`

Edit this path through **Edit Simulation** when live and replay simulations
need separate input directories. Absolute paths beneath the current user's
home directory are stored with the `<HOME>` prefix, preventing usernames and
personal home paths from entering the repository. The value is a machine-local
`.pol2` setting and is not exported to `forecast.json`. Durable support files
and the selected working copy continue to live under the repository's
`downloads` folder.

Version-65 projects that already contain an absolute path beneath the current
home directory are normalized to `<HOME>/...` when they are loaded and next
saved.

Live/media-feed snapshots and close derivatives are private operator inputs.
Do not commit retained XML/ZIP files, per-booth exports or replay comparisons.
Public final-result downloads and published statistics can still support the
historical workflow. Installed live parameters are maintained in source; see
[the turnout documentation](../docs/live-turnout.md) for their locations
and the distinction between applying the model and reproducing its calibration.

## Common workflow

1. Confirm the forecast or `.pol2` project has an automatic live simulation
   and the correct previous election code.
2. Install the jurisdiction's durable support files listed below.
3. For state elections, download or copy the current result feed into the
   simulation's configured current-results directory under the required
   official filename. Remove old matching feeds from another election if their
   names do not identify the election.
4. Run the model and projection, then run the automatic live simulation.
5. Check the log for the source selected and the repository working copy
   written as `downloads/<term>_latest.xml`.

The setup is validated immediately before the live simulation runs. Missing or
empty support files cause a fatal setup error rather than a partial run.

Historical `analysis/Booth Results/*.json` files are generated through
`python analysis/fetch_booth_results.py --election <code>`. Supported outputs
are recorded in generated provenance and included in the generated-data
archive. The durable `downloads/*.xml` support files remain manual,
machine-specific setup and are deliberately not archived.

`current_real_url` may also be set to `local:<filename>` to read an already
extracted current-result XML from `downloads/<filename>`. This bypasses the
configured-directory scan. Federal `current_test_url` supports the same form.

The Federal and SA replay scripts advance one snapshot when run without
arguments. At the final snapshot, they report that the end has been reached
and return successfully, leaving the installed feed and replay state unchanged.

## Federal (AEC)

Covered automatic-live specifications: `2022fed`, `2025fed` and `2028fed`.
The URLs stored in a historical or future specification must be checked before
use; AEC event IDs and archive paths are election-specific.

Configure all four live source fields in the election settings:

- `previous_results_url`: the previous election's final Detailed/Verbose ZIP.
- `preload_url`: the current election's Detailed/Preload ZIP. It must contain
  both the preload and polling-district XML files.
- `current_real_url`: the current election's Detailed/Light directory URL,
  ending in `/`. The newest filename in the listing is downloaded.
- `current_test_url`: a specific Detailed/Light ZIP used when
  `current_real_url` is blank.

Downloaded files are cached under URL-derived names in `downloads`. The
current Detailed/Light XML is extracted to `downloads/custom_results.xml`.
No file in the configured current-results directory is used for the normal AEC
path.

For historical replay, either configure a direct archived ZIP as
`current_test_url`, or place extracted XML in `downloads` and use
`local:<filename>`.

### Replaying Federal 2025 snapshots

`Replay-Federal2025LiveSnapshot.ps1` selects nine retained AEC result updates
and extracts the selected XML to `downloads/fed2025-replay.xml`. This lets
the GUI compare its existing vote-size rules with the turnout calculation
using the same counted votes and saved forecast settings. It does not run
the application or download feeds.

The checkpoint timestamps below are the AEC XML's `Created` times. They span
different amounts of counting and a prolonged postal counting period; the
last entry is the final retained feed, not an instruction to force completion.

| Timestamp argument | AEC source time | Count stage |
| --- | --- | --- |
| `20250503171746` | 3 May 17:17:46 | No results |
| `20250503195947` | 3 May 19:59:47 | Early ordinary and PPVC results |
| `20250504010119` | 4 May 01:01:19 | Later election-night results |
| `20250511215001` | 11 May 21:50:01 | Postal pause before the receipt deadline |
| `20250516214931` | 16 May 21:49:31 | Receipt deadline day |
| `20250518214938` | 18 May 21:49:38 | Processing after the receipt deadline |
| `20250519214849` | 19 May 21:48:49 | Continuing local late batches |
| `20250520214756` | 20 May 21:47:56 | Late declaration counting |
| `20250531210545` | 31 May 21:05:45 | Final retained count |

Run from the repository root:

```powershell
.\live_scripts\Replay-Federal2025LiveSnapshot.ps1 -List
.\live_scripts\Replay-Federal2025LiveSnapshot.ps1 20250503171746
.\live_scripts\Replay-Federal2025LiveSnapshot.ps1 -Interactive
.\live_scripts\Replay-Federal2025LiveSnapshot.ps1
```

`-List` returns the checkpoints and source availability without changing
the selection. An explicit timestamp or `-Interactive` chooses a checkpoint;
no argument advances to the next of these nine after the previous selection.
`-WhatIf` checks the selected ZIP and its header without installing anything.
The script searches ignored `downloads/turnout/feed-archive/` storage (or the
optional `POLLING_ANALYSER_FEED_DOWNLOADS`/`POLLING_ANALYSER_FEED_ARCHIVE` location) and existing repository
turnout caches. Use `-ArchiveDirectory` for another directory of retained
ZIPs. When a turnout artifact is present, its recorded ZIP hash must match.
The selection and source/XML hashes are stored under the ignored
`downloads/turnout/federal-gui-replay/` directory.

Use a separate copy of the Federal 2025 live project. In **Edit Simulation**,
set **Current Real URL** to `local:fed2025-replay.xml` and clear
**Current Test URL**. The state-election **Current results directory**
setting does not select AEC feeds. Keep the previous-results and preload URLs
unchanged if their corresponding cached XML files are available, including
the preload's polling-place file. Reports use the XML's source timestamp,
even though this working filename stays the same between selections.

Opening a saved project imports `forecasts/2025fed/forecast.json`, whose
settings take precedence over saved model, projection and live-source
settings. This package selects the replay XML, the 2025 preload, final 2022
previous results and the known 3 May 2025 election date. The live simulation
uses 20,000 iterations. Change the package if these settings need to persist
across reopening a project; editing only the `.pol2` does not change the
imported settings. The diagnostic output folder remains a saved-project
setting. Empty AEC location fields leave booth coordinates unknown, including
mobile services operating at multiple sites; nonempty coordinates are checked
before they are used for geographic matching.

The live application reads `forecasts/<election>/live-inputs/turnout-prior.json`
for the election being simulated. Prepare that required private input using
[the live turnout instructions](../docs/live-turnout.md). No turnout environment
variables or application restart are needed when switching elections.
Received count history accumulates under that election's `live-snapshots/`
directory; an earlier replay ignores later source times already stored there.

Set **Live output folder** to a diagnostic series name such as
`2025fed-turnout4`. This is a folder name under `live_runs/`, rather than a path.
Keep model, projection, iteration count and other simulation settings consistent
when comparing snapshot series. The SA Shift-click batch advance does not
support the Federal replay.

The replay shows how the live model behaves for a particular saved forecast. Its broader forecast accuracy also depends on the saved
model, projection and candidate/preference settings being suitable for
Federal 2025. A result-feed replay by itself does not establish that those
other inputs are suitable for a historical forecast.

## Victoria (VEC)

Covered automatic-live specifications: `2022vic` and `2026vic`.

Required durable files:

- `downloads/<term>_candidates.xml`
- `downloads/<term>_booths.xml`
- `analysis/Booth Results/<previous-term>.json`

For `2026vic`, the previous-result file is therefore
`analysis/Booth Results/2022vic.json`. It still needs to be generated before
that live forecast is operational. The file has no supported generator yet and
is deliberately not required by generated-data archive preflight.

Put VEC media-feed ZIPs whose names contain
`mediafilelitepplh_YYYYMMDD_HHMMSS` in the configured current-results
directory. If several are present, the embedded filename timestamp selects the
newest. Its result XML is extracted to `downloads/<term>_latest.xml`.

## New South Wales (NSWEC)

The automatic parser and acquisition path cover NSWEC feeds. Exported
`2023nsw` and `2027nsw` specifications currently use manual-live mode, so they
do not invoke this path without changing the simulation mode.

Required durable files:

- `downloads/<term>_zeros.xml`
- `analysis/Booth Results/<previous-term>.json`

Put NSWEC result ZIPs in the configured current-results directory. The
supported official filename form has a numeric sequence/time prefix followed
by `-SG`, then an eight-digit date. If several are present, the date and numeric
prefix select the newest. The result XML is extracted to
`downloads/<term>_latest.xml`.

## Queensland (ECQ)

The path has been used with the 2024 Queensland artifacts currently in the
repository.

Required durable files:

- `downloads/<term>_zeros.xml`
- `analysis/Booth Results/<previous-term>.json`

Put ECQ result ZIPs named `YYYYMMDDHHMMSS_publicResults...zip` in the configured
current-results directory. If several are present, the 14-digit filename
timestamp selects the newest. The result XML is extracted to
`downloads/<term>_latest.xml`.

VEC, NSWEC and ECQ ZIP extraction currently invokes Windows PowerShell. Their
directory handling is portable, but their extractor still requires Windows.

## Western Australia (WAEC)

Covered automatic-live specification: `2025wa`.

Required durable files:

- `downloads/<term>_candidates_prev.xml`
- `downloads/<term>_booths_prev.xml`
- `downloads/<term>_results_prev.xml`
- `downloads/<term>_candidates_current.xml`
- `downloads/<term>_booths_current.xml`

Put the extracted WAEC XML whose filename contains `LA VERBOSE RESULTS` in the
configured current-results directory. It is copied directly to
`downloads/<term>_latest.xml`; it is not treated as a ZIP.

During the live-election window (14 days before through 42 days after the
configured election date), the source is also copied beside itself with the
marker replaced by the local capture timestamp. Replays outside that window do
not create false at-the-time archives.

## South Australia (ECSA)

Covered automatic-live specification: `2026sa`.

Required durable files:

- `downloads/<term>_zeros.xml`
- `analysis/Booth Results/<previous-term>.json`

The current ECSA House of Assembly detail XML must have the exact filename
`el<year>_ha_detail.xml` in the configured current-results directory, for example
`el2026_ha_detail.xml`. The XML may be UTF-8 or UTF-16. It is copied directly to
`downloads/<term>_latest.xml`.

During the live-election window, a unique at-the-time copy is retained in
the configured directory using the XML's embedded `last_updated` value.
Replaying old feeds later does not create newly dated archive files.

### Replaying SA 2026 snapshots

`Replay-Sa2026LiveSnapshot.ps1` installs one of the timestamped
`el2026<timestamp>.xml` archives as `el2026_ha_detail.xml`.

Run from PowerShell in `live_scripts`:

```powershell
.\Replay-Sa2026LiveSnapshot.ps1 260315004007
.\Replay-Sa2026LiveSnapshot.ps1
.\Replay-Sa2026LiveSnapshot.ps1 -Interactive
.\Replay-Sa2026LiveSnapshot.ps1 -Interactive -ResultsDirectory C:\LiveTests\SA2026
```

The first command selects an explicit snapshot. The second advances one
snapshot from the previous selection. The third lists all snapshots and asks
for a selection. `-ResultsDirectory` must match the directory configured in
the simulation; it defaults to the user's Downloads directory. State is stored in the ignored
`.sa-2026-live-replay-state.json` file.

Use `-WhatIf` to validate a selection without replacing the Downloads file:

```powershell
.\Replay-Sa2026LiveSnapshot.ps1 260315004007 -WhatIf
```

The replay tool does not download data or alter timestamped source archives.

After selecting and running an initial snapshot manually, hold **Shift** while
clicking **Run Live Simulations** on the Results screen to run a sequence. The
dialog asks how many subsequent snapshots to process. Before each run, the
application installs the next timestamped archive, updates the same replay
state used by the PowerShell script, and runs every configured automatic-live
simulation. All such simulations must use the same SA election and current-
results directory. A failed simulation stops the sequence with that snapshot
left selected for inspection or a manual retry.

Routine feedback such as the live seat-change summary is written to `PALog.log`
during a batch rather than opening a dialog after every run. Conditions that
explicitly require action, such as an output file that cannot be replaced,
still open a dialog.

Explicit initial selection and out-of-sequence movement remain PowerShell
operations. If the installed XML and replay state disagree, select the desired
snapshot with the script again before starting a batch.

## Troubleshooting

- **No current-results file found:** check the exact filename convention and
  that the file is in the simulation's configured current-results directory,
  not the repository `downloads` folder.
- **Wrong snapshot selected:** remove feeds for other elections that share the
  same jurisdiction marker, or use `current_real_url=local:<filename>`.
- **Missing setup file:** create or restore the exact path listed by the setup
  validation error. Do not substitute a result file from another term.
- **Downloaded federal data is stale:** verify every AEC URL contains the event
  ID for the intended election; future forecast specifications often retain
  placeholders until the AEC publishes the live feed.
- **A replay created a new archive:** this should occur only inside the live
  window. SA archive names use ECSA's `last_updated`; WA uses local capture
  time because its selected source contract has no parsed feed timestamp.

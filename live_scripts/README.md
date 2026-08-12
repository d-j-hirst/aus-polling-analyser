# Automatic Live Results

Automatic live simulations combine three kinds of input:

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

`current_real_url` may also be set to `local:<filename>` to read an already
extracted current-result XML from `downloads/<filename>`. This bypasses the
configured-directory scan. Federal `current_test_url` supports the same form.

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

## Victoria (VEC)

Covered automatic-live specifications: `2022vic` and `2026vic`.

Required durable files:

- `downloads/<term>_candidates.xml`
- `downloads/<term>_booths.xml`
- `analysis/Booth Results/<previous-term>.json`

For `2026vic`, the previous-result file is therefore
`analysis/Booth Results/2022vic.json`. It still needs to be generated before
that live forecast is operational.

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

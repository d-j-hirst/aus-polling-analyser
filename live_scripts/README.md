# Live Scripts

`Replay-Sa2026LiveSnapshot.ps1` replays archived ECSA results for the 2026
South Australian election. It installs a timestamped `el2026*.xml` snapshot in
Windows Downloads as `el2026_ha_detail.xml`, which automatic live preparation
then copies to `downloads/2026sa_latest.xml`.

Run from PowerShell:

```powershell
.\Replay-Sa2026LiveSnapshot.ps1 260315004007
.\Replay-Sa2026LiveSnapshot.ps1
.\Replay-Sa2026LiveSnapshot.ps1 -Interactive
```

The first command selects an explicit snapshot. The second advances one
snapshot from the previous selection. The third lists all snapshots and asks
for a selection. The locally remembered selection is stored in the ignored
`live_scripts/.sa-2026-live-replay-state.json` file.

Use `-WhatIf` to validate a selection without replacing the Downloads file:

```powershell
.\Replay-Sa2026LiveSnapshot.ps1 260315004007 -WhatIf
```

The tool is deliberately specific to the existing SA 2026 archive naming
scheme. It does not download data or change the archived timestamped files.
Automatic live preparation continues to copy the selected snapshot to
`downloads/2026sa_latest.xml`, but only creates a timestamped archive while the
configured election is in its live-count window. Archive names use ECSA's
embedded `last_updated` time rather than the time a simulation happens to run.

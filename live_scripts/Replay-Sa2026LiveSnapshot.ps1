<#
.SYNOPSIS
Installs an archived 2026 SA ECSA snapshot as el2026_ha_detail.xml.

.DESCRIPTION
The polling application looks for el2026_ha_detail.xml in the current-results
directory configured for the simulation. This script copies one timestamped
archive (el2026<12-digit stamp>.xml) to that name, then remembers the
selection so a later run without arguments moves to the next snapshot.

Source archives are left unchanged. The script does not write into the
repository downloads folder; LivePreparation copies the selected feed to
downloads/2026sa_latest.xml when the simulation runs.

.PARAMETER Timestamp
12-digit filename stamp, for example 260315004007 for el2026260315004007.xml.
This is not the 14-digit ECSA last_updated value used as snapshot_code in
live-run JSON exports.

.PARAMETER Interactive
List matching archives and prompt for a selection.

.PARAMETER ResultsDirectory
Directory that contains the archives and receives el2026_ha_detail.xml.
Must match the simulation's Current results directory. Defaults to Downloads.

.PARAMETER Help
Print brief usage and exit.

.EXAMPLE
.\Replay-Sa2026LiveSnapshot.ps1 260315004007

.EXAMPLE
.\Replay-Sa2026LiveSnapshot.ps1 -Interactive

.EXAMPLE
.\Replay-Sa2026LiveSnapshot.ps1
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Position = 0)]
    [string]$Timestamp,

    [switch]$Interactive,

    [string]$ResultsDirectory = (Join-Path $HOME 'Downloads'),

    [Alias('h')]
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host @'
Replay-Sa2026LiveSnapshot.ps1
  Copies one archived ECSA feed (el2026<12-digit stamp>.xml) to
  el2026_ha_detail.xml in the simulation's Current results directory
  (default: Downloads). Source archives are not modified.

  The 12-digit argument is the filename stamp, not the 14-digit
  last_updated value that appears as snapshot_code in live-run JSON.

Usage:
  .\Replay-Sa2026LiveSnapshot.ps1 260315004007
      Install that archive.

  .\Replay-Sa2026LiveSnapshot.ps1 -Interactive
      List archives and choose one.

  .\Replay-Sa2026LiveSnapshot.ps1
      Advance one snapshot from the last selection (requires a prior run).

  .\Replay-Sa2026LiveSnapshot.ps1 -Interactive -ResultsDirectory C:\LiveTests\SA2026
      Use a directory other than Downloads.

  .\Replay-Sa2026LiveSnapshot.ps1 260315004007 -WhatIf
      Show the choice without replacing el2026_ha_detail.xml.

Then run the automatic live simulation as usual. LivePreparation copies
the installed feed to downloads/2026sa_latest.xml at run time.
'@
    return
}

$ResultsDirectory = [System.IO.Path]::GetFullPath($ResultsDirectory)
$TargetFilename = 'el2026_ha_detail.xml'
$TargetPath = Join-Path $ResultsDirectory $TargetFilename
$StatePath = Join-Path $PSScriptRoot '.sa-2026-live-replay-state.json'
$SnapshotPattern = '^el2026(?<timestamp>\d{12})\.xml$'

function Get-EcsaLastUpdated {
    param([Parameter(Mandatory = $true)][string]$Path)

    # The timestamp is near the beginning of ECSA's UTF-16 XML. Read only a
    # small prefix so the interactive listing does not load every 5 MB file.
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.StreamReader]::new($stream, $true)
        try {
            $buffer = New-Object char[] 8192
            $read = $reader.Read($buffer, 0, $buffer.Length)
            $prefix = [string]::new($buffer, 0, $read)
            $match = [regex]::Match(
                $prefix,
                '<last_updated>(?<value>[^<]+)</last_updated>',
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($match.Success) { return $match.Groups['value'].Value.Trim() }
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
    return '(not found)'
}

function Get-Snapshots {
    if (-not (Test-Path -LiteralPath $ResultsDirectory -PathType Container)) {
        throw "Current-results directory was not found: $ResultsDirectory"
    }

    $snapshots = foreach ($file in Get-ChildItem -LiteralPath $ResultsDirectory -File) {
        $match = [regex]::Match($file.Name, $SnapshotPattern,
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($match.Success) {
            [PSCustomObject]@{
                Timestamp = $match.Groups['timestamp'].Value
                Filename = $file.Name
                Path = $file.FullName
                SizeBytes = $file.Length
                LastUpdated = Get-EcsaLastUpdated -Path $file.FullName
            }
        }
    }

    return @($snapshots | Sort-Object Timestamp)
}

function Read-ReplayState {
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) { return $null }
    try {
        return Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Could not read replay state $StatePath. Delete or repair it before continuing."
    }
}

function Write-ReplayState {
    param([Parameter(Mandatory = $true)]$Snapshot)

    $state = [ordered]@{
        timestamp = $Snapshot.Timestamp
        source_filename = $Snapshot.Filename
        selected_utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json
    $temporaryPath = "$StatePath.tmp"
    Set-Content -LiteralPath $temporaryPath -Value $state -Encoding UTF8
    if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
        $backupPath = "$StatePath.backup"
        [System.IO.File]::Replace($temporaryPath, $StatePath, $backupPath)
        Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
    }
    else {
        [System.IO.File]::Move($temporaryPath, $StatePath)
    }
}

function Test-EcsaSnapshot {
    param([Parameter(Mandatory = $true)]$Snapshot)

    if ($Snapshot.SizeBytes -le 0) {
        throw "Snapshot $($Snapshot.Filename) is empty."
    }
    try {
        [xml]$document = Get-Content -LiteralPath $Snapshot.Path -Raw
    }
    catch {
        throw "Snapshot $($Snapshot.Filename) is not valid XML: $($_.Exception.Message)"
    }
    if ($document.DocumentElement.LocalName -ne 'HouseOfAssemblyDetail') {
        throw "Snapshot $($Snapshot.Filename) is not an ECSA HouseOfAssemblyDetail XML file."
    }
}

function Select-InteractiveSnapshot {
    param([Parameter(Mandatory = $true)][object[]]$Snapshots)

    Write-Host ''
    Write-Host 'Available SA 2026 snapshots:'
    $numbered = for ($index = 0; $index -lt $Snapshots.Count; ++$index) {
        [PSCustomObject]@{
            Number = $index + 1
            Timestamp = $Snapshots[$index].Timestamp
            ECSAUpdated = $Snapshots[$index].LastUpdated
            Filename = $Snapshots[$index].Filename
        }
    }
    $numbered | Format-Table -AutoSize | Out-Host

    $selection = Read-Host 'Select a snapshot number'
    $number = 0
    if (-not [int]::TryParse($selection, [ref]$number) -or
        $number -lt 1 -or $number -gt $Snapshots.Count) {
        throw 'Selection must be a number from the displayed list.'
    }
    return $Snapshots[$number - 1]
}

if ($Timestamp -and $Interactive) {
    throw 'Specify either a timestamp or -Interactive, not both.'
}
if ($Timestamp -and $Timestamp -notmatch '^\d{12}$') {
    throw 'Timestamp must contain exactly 12 digits, for example 260315004007.'
}

# PowerShell unwraps a single pipeline result; retain collection semantics for one snapshot.
$snapshots = @(Get-Snapshots)
if ($snapshots.Count -eq 0) {
    throw "No archived snapshots matching el2026<timestamp>.xml were found in $ResultsDirectory."
}

if ($Interactive) {
    $selectedSnapshot = Select-InteractiveSnapshot -Snapshots $snapshots
}
elseif ($Timestamp) {
    $selectedSnapshot = $snapshots | Where-Object Timestamp -eq $Timestamp | Select-Object -First 1
    if (-not $selectedSnapshot) {
        throw "No snapshot with timestamp $Timestamp was found in $ResultsDirectory."
    }
}
else {
    $state = Read-ReplayState
    if (-not $state) {
        throw 'No replay state exists. Provide a timestamp or use -Interactive to choose the first snapshot.'
    }
    $currentIndex = -1
    for ($index = 0; $index -lt $snapshots.Count; ++$index) {
        if ($snapshots[$index].Timestamp -eq $state.timestamp) {
            $currentIndex = $index
            break
        }
    }
    if ($currentIndex -lt 0) {
        throw "The remembered snapshot $($state.timestamp) is no longer available. Use a timestamp or -Interactive to choose a new starting point."
    }
    if ($currentIndex -eq $snapshots.Count - 1) {
        throw 'The remembered snapshot is already the latest available snapshot.'
    }
    $selectedSnapshot = $snapshots[$currentIndex + 1]
}

Test-EcsaSnapshot -Snapshot $selectedSnapshot

if ($PSCmdlet.ShouldProcess($TargetPath, "Install $($selectedSnapshot.Filename)")) {
    $temporaryTarget = "$TargetPath.tmp"
    try {
        [System.IO.File]::Copy($selectedSnapshot.Path, $temporaryTarget, $true)
        if (Test-Path -LiteralPath $TargetPath -PathType Leaf) {
            $backupPath = "$TargetPath.backup"
            [System.IO.File]::Replace($temporaryTarget, $TargetPath, $backupPath)
            Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
        }
        else {
            [System.IO.File]::Move($temporaryTarget, $TargetPath)
        }
        Write-ReplayState -Snapshot $selectedSnapshot
    }
    finally {
        if (Test-Path -LiteralPath $temporaryTarget) {
            Remove-Item -LiteralPath $temporaryTarget -Force
        }
    }
}

Write-Host "Selected snapshot: $($selectedSnapshot.Filename)"
Write-Host "ECSA last_updated: $($selectedSnapshot.LastUpdated)"
if ($selectedSnapshot -ne $snapshots[-1]) {
    $nextIndex = [array]::IndexOf($snapshots, $selectedSnapshot) + 1
    Write-Host "Next snapshot: $($snapshots[$nextIndex].Timestamp)"
}
else {
    Write-Host 'Selected snapshot is the latest available.'
}

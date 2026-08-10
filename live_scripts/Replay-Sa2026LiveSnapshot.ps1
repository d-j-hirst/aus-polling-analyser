<#
Replays archived 2026 South Australian ECSA live-result snapshots.

The polling application looks for el2026_ha_detail.xml in the user's Windows
Downloads folder. This script installs one timestamped archived snapshot under
that name, then remembers the selection so a later run without arguments moves
to the next available snapshot.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Position = 0)]
    [string]$Timestamp,

    [switch]$Interactive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DownloadsPath = Join-Path $HOME 'Downloads'
$TargetFilename = 'el2026_ha_detail.xml'
$TargetPath = Join-Path $DownloadsPath $TargetFilename
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
    if (-not (Test-Path -LiteralPath $DownloadsPath -PathType Container)) {
        throw "Windows Downloads folder was not found: $DownloadsPath"
    }

    $snapshots = foreach ($file in Get-ChildItem -LiteralPath $DownloadsPath -File) {
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

$snapshots = Get-Snapshots
if ($snapshots.Count -eq 0) {
    throw "No archived snapshots matching el2026<timestamp>.xml were found in $DownloadsPath."
}

if ($Interactive) {
    $selectedSnapshot = Select-InteractiveSnapshot -Snapshots $snapshots
}
elseif ($Timestamp) {
    $selectedSnapshot = $snapshots | Where-Object Timestamp -eq $Timestamp | Select-Object -First 1
    if (-not $selectedSnapshot) {
        throw "No snapshot with timestamp $Timestamp was found in $DownloadsPath."
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

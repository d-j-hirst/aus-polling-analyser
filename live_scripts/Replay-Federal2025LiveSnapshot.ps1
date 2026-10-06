<#
.SYNOPSIS
Selects a retained Federal 2025 result feed for a manual GUI replay.

.DESCRIPTION
Extracts one AEC Detailed/Light result XML to downloads/fed2025-replay.xml.
Configure Current Real URL as local:fed2025-replay.xml in the live simulation.
The checkpoints share the source revisions used by the turnout comparison.
No argument advances to the next checkpoint after an initial selection,
or reports that the final checkpoint is already selected without changing it.
Source ZIPs are left unchanged; no downloads or simulations are performed.

.PARAMETER Timestamp
The 14-digit checkpoint timestamp displayed by -List.

.PARAMETER ArchiveDirectory
Directory containing retained AEC ZIPs. Defaults to ignored local feed storage;
POLLING_ANALYSER_FEED_DOWNLOADS or POLLING_ANALYSER_FEED_ARCHIVE can select an external directory.
The repository's existing turnout source cache is also searched.

.PARAMETER TargetPath
Working XML path. The default matches local:fed2025-replay.xml.

.EXAMPLE
.\Replay-Federal2025LiveSnapshot.ps1 -List

.EXAMPLE
.\Replay-Federal2025LiveSnapshot.ps1 20250503171746

.EXAMPLE
.\Replay-Federal2025LiveSnapshot.ps1 -Interactive
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Position = 0)]
    [string]$Timestamp,
    [switch]$Interactive,
    [switch]$List,
    [string]$ArchiveDirectory = $(if ($env:POLLING_ANALYSER_FEED_DOWNLOADS) { $env:POLLING_ANALYSER_FEED_DOWNLOADS } elseif ($env:POLLING_ANALYSER_FEED_ARCHIVE) { $env:POLLING_ANALYSER_FEED_ARCHIVE } else { Join-Path $PSScriptRoot '..\downloads\turnout\feed-archive' }),
    [string]$TargetPath = (Join-Path $PSScriptRoot '..\downloads\fed2025-replay.xml')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$TargetPath = [System.IO.Path]::GetFullPath($TargetPath)
$statePath = Join-Path $repositoryRoot 'downloads\turnout\federal-gui-replay\selection.json'
# Archive source identities belong to replay input storage, independently of
# turnout priors and their regeneration. This optional private manifest retains
# hashes for sources held in the existing content-addressed cache directories.
$manifestPath = Join-Path $repositoryRoot 'forecasts\2025fed\live-snapshots\feed-sources.json'
$sourceManifest = if (Test-Path -LiteralPath $manifestPath) {
    Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
}

# These source times also occur in the retained received-feed history. The selection
# spans sparse booths, settled election-night counts, a postal pause and late
# counting, without making the user step through every 90-second AEC release.
$checkpoints = @(
    @('20250503171746', 'No results'),
    @('20250503195947', 'Early ordinary and PPVC results'),
    @('20250504010119', 'Later election-night results'),
    @('20250511215001', 'Postal pause before the receipt deadline'),
    @('20250516214931', 'Postal receipt deadline day'),
    @('20250518214938', 'Processing after the receipt deadline'),
    @('20250519214849', 'Continuing local late batches'),
    @('20250520214756', 'Late-count remaining-vote comparison'),
    @('20250531210545', 'Final retained count')
)

function Get-Checkpoint {
    param([string]$Stamp, [string]$Purpose)
    $sourceTime = [DateTime]::ParseExact($Stamp, 'yyyyMMddHHmmss', $null).ToString('yyyy-MM-ddTHH:mm:ss')
    $filename = "aec-mediafeed-Detailed-Light-31496-$Stamp.zip"
    $expectedHash = $null
    if ($sourceManifest) {
        $source = $sourceManifest.PSObject.Properties[$sourceTime]
        if ($source) { $expectedHash = $source.Value.sha256 }
    }
    $paths = @((Join-Path $ArchiveDirectory $filename))
    if ($expectedHash) {
        foreach ($cache in @('declaration-progress', 'federal-ppvc-reporting')) {
            $paths += Join-Path $repositoryRoot "downloads\turnout\$cache\2025fed\$expectedHash\$filename"
        }
    }
    $path = $paths | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    [PSCustomObject]@{
        Timestamp = $Stamp; SourceTime = $sourceTime; Purpose = $Purpose
        Available = [bool]$path; Path = $path; ExpectedSha256 = $expectedHash
    }
}

if (($Timestamp -and $Interactive) -or ($List -and ($Timestamp -or $Interactive))) {
    throw 'Choose a timestamp, -Interactive, or -List separately.'
}
$snapshots = @($checkpoints | ForEach-Object { Get-Checkpoint -Stamp $_[0] -Purpose $_[1] })
if ($List) { $snapshots; return }

if ($Interactive) {
    for ($i = 0; $i -lt $snapshots.Count; ++$i) {
        Write-Host "$($i + 1). $($snapshots[$i].SourceTime)  $($snapshots[$i].Purpose)  Available: $($snapshots[$i].Available)"
    }
    $number = 0
    if (-not [int]::TryParse((Read-Host 'Select a checkpoint number'), [ref]$number) -or
        $number -lt 1 -or $number -gt $snapshots.Count) { throw 'Select a number from the displayed list.' }
    $selected = $snapshots[$number - 1]
}
elseif ($Timestamp) {
    $selected = $snapshots | Where-Object Timestamp -eq $Timestamp | Select-Object -First 1
    if (-not $selected) { throw 'Use one of the checkpoint timestamps displayed by -List.' }
}
else {
    if (-not (Test-Path -LiteralPath $statePath)) { throw 'Choose the first checkpoint using a timestamp or -Interactive.' }
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $index = [array]::IndexOf([string[]]$snapshots.Timestamp, [string]$state.timestamp)
    if ($index -lt 0) { throw 'The remembered checkpoint is unavailable; select a timestamp explicitly.' }
    # Refuse to advance from a stale selection record: another tool might have
    # replaced the working feed while leaving this replay's state untouched.
    if (-not (Test-Path -LiteralPath $TargetPath) -or
        (Get-FileHash -LiteralPath $TargetPath -Algorithm SHA256).Hash -ne $state.xml_sha256) {
        throw 'The working XML differs from the remembered selection. Select a timestamp explicitly.'
    }
    # Reaching the end is a normal replay outcome. Keep the installed feed and
    # remembered selection intact so another no-argument call is harmless.
    if ($index -eq $snapshots.Count - 1) {
        Write-Host "Already at the final replay checkpoint: $($snapshots[$index].SourceTime). No snapshot changed."
        return
    }
    $selected = $snapshots[$index + 1]
}
if (-not $selected.Available) { throw "Source ZIP unavailable for $($selected.Timestamp). Check -ArchiveDirectory." }
$sourceHash = (Get-FileHash -LiteralPath $selected.Path -Algorithm SHA256).Hash.ToLowerInvariant()
if ($selected.ExpectedSha256 -and $sourceHash -ne $selected.ExpectedSha256) {
    throw 'The source ZIP differs from the revision used by the turnout artifact. Reconcile the source and artifact before replaying.'
}

# Inspect the archive itself even for -WhatIf. Only the expected member is read;
# arbitrary archive paths are never extracted into the workspace.
$archive = [System.IO.Compression.ZipFile]::OpenRead($selected.Path)
try {
    $entry = $archive.GetEntry('xml/aec-mediafeed-results-detailed-light-31496.xml')
    if (-not $entry -or $entry.Length -eq 0) { throw 'The ZIP has no Federal 2025 Detailed/Light XML.' }
    $stream = $entry.Open()
    $reader = [System.IO.StreamReader]::new($stream)
    try {
        $buffer = New-Object char[] 8192
        $length = $reader.Read($buffer, 0, $buffer.Length)
        $header = [string]::new($buffer, 0, $length)
    }
    finally { $reader.Dispose() }
    if ($header -notmatch '<MediaFeed\s[^>]*Created="([^"]+)"') { throw 'The XML has no AEC source timestamp.' }
    if ($Matches[1] -ne $selected.SourceTime -or $header -notmatch '<eml:EventIdentifier Id="31496"') {
        throw 'The XML source timestamp or election identifier differs from the selected checkpoint.'
    }
    if ($PSCmdlet.ShouldProcess($TargetPath, "Install $($selected.SourceTime) ($($selected.Purpose))")) {
        [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($TargetPath)) | Out-Null
        $temporaryPath = "$TargetPath.tmp"
        try {
            $inputStream = $entry.Open()
            $outputStream = [System.IO.File]::Create($temporaryPath)
            try { $inputStream.CopyTo($outputStream) }
            finally { $inputStream.Dispose(); $outputStream.Dispose() }
            if (Test-Path -LiteralPath $TargetPath) {
                $backupPath = "$TargetPath.backup"
                [System.IO.File]::Replace($temporaryPath, $TargetPath, $backupPath)
                Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
            }
            else { [System.IO.File]::Move($temporaryPath, $TargetPath) }
            [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($statePath)) | Out-Null
            [ordered]@{
                timestamp = $selected.Timestamp; source_time = $selected.SourceTime
                source_zip = $selected.Path; zip_sha256 = $sourceHash
                xml_sha256 = (Get-FileHash -LiteralPath $TargetPath -Algorithm SHA256).Hash.ToLowerInvariant()
            } | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
        }
        finally {
            if (Test-Path -LiteralPath $temporaryPath) { Remove-Item -LiteralPath $temporaryPath -Force }
        }
    }
}
finally { $archive.Dispose() }
Write-Host "Selected AEC source: $($selected.SourceTime) ($($selected.Purpose))"
Write-Host 'Run the automatic live simulation for this checkpoint.'

<#
Deliver the next of four genuine SA captures for the initial commentary trial.
The first contains no results; the next three contain 2,725, 3,136 and 3,932 FP votes.
Run once, wait for the forecast, then repeat. No forecast or API call is
started by this script. Original feed files remain unchanged.
#>
[CmdletBinding()]
param([string]$Inbox, [string]$RunId, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$arguments = @('-B', (Join-Path $PSScriptRoot 'replay_feeder.py'),
    '--election', '2026sa', '--source', (Join-Path $workspace 'forecasts\2026sa\live-snapshots\feeds'),
    '--codes-file', (Join-Path $PSScriptRoot 'fixtures\2026sa-commentary-start.txt'), '--step')
if ($RunId) {
    if ($Inbox) { throw 'Choose either -RunId or -Inbox.' }
    $arguments += @('--run-id', $RunId)
} else {
    if (-not $Inbox) { $Inbox = Join-Path $workspace 'live_runs\sa-commentary-inbox' }
    $arguments += @('--inbox', $Inbox)
}
if ($DryRun) { $arguments += '--dry-run' }
& (Join-Path $workspace 'analysis\.venv-win\Scripts\python.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw "SA trial delivery failed (exit $LASTEXITCODE)." }

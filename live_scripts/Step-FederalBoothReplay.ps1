# Installed as Next.ps1 inside each private SA geography replay.
param([switch]$DryRun)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runId = Split-Path -Leaf $PSScriptRoot
$feedArgs = @('--run-id', $runId, '--codes-file', (Join-Path $PSScriptRoot 'captures.txt'), '--step')
if ($DryRun) { $feedArgs += '--dry-run' }
& (Join-Path $repository 'analysis\.venv-win\Scripts\python.exe') -B (Join-Path $repository 'live_scripts\replay_feeder.py') @feedArgs
if ($LASTEXITCODE -ne 0) { throw "Replay delivery failed (exit $LASTEXITCODE)." }

param(
    [Parameter(Mandatory, Position=0)]
    [ValidateSet('prepare', 'check-election', 'publish-election', 'rehearsal', 'election')]
    [string]$Mode,
    [switch]$Watch,
    [ValidateSet('rehearsal','election')]
    [string]$PrepareFor
)

# The Windows credential is encrypted for the account that saved it. Pass
# the signed URL only to the downloader process, never as a command argument.
$ErrorActionPreference = 'Stop'
if ($PrepareFor -and $Mode -ne 'prepare') { throw '-PrepareFor applies to prepare only.' }
if ($Watch -and $Mode -notin 'rehearsal','election') { throw '-Watch applies to a feed collector only.' }
$root = Split-Path $PSScriptRoot -Parent
$python = Join-Path $root 'analysis/.venv-win/Scripts/python.exe'
if ($Mode -eq 'publish-election') {
    # The official publisher can wait through long gaps in post-election
    # counting. It reads its own login and has no need for the feed credential.
    & $python -B (Join-Path $root 'uploads/upload_manager.py') --watch 2026vic-live --all --idle-hours 0
    exit $LASTEXITCODE
}
$previousValue = $env:VEC_SAS_URL
try {
    if (-not $previousValue) {
        $keyPath = Join-Path $root 'forecasts/2026vic/live-inputs/vec-sas.dpapi'
        $secret = ConvertTo-SecureString (Get-Content -Raw -LiteralPath $keyPath).Trim()
        $env:VEC_SAS_URL = [System.Net.NetworkCredential]::new('', $secret).Password
    }
    $arguments = @((Join-Path $PSScriptRoot 'vec_2026_feed.py'), $Mode)
    if ($Watch) { $arguments += '--watch' }
    & $python -B @arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    $env:VEC_SAS_URL = $previousValue
}
if ($Mode -eq 'prepare') {
    Push-Location (Join-Path $root 'analysis')
    try {
        if (-not (Test-Path -LiteralPath 'Booth Results/2022vic.json')) {
            & $python -B -m scripts.elections.prepare_vec_2022_booths
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
        & $python -B -m scripts.turnout.turnout_prepare_vic 2026vic
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        # This also populates the federal booth cache used for Narracan's EVCs.
        & $python -B (Join-Path $PSScriptRoot 'prepare_one_nation_booths.py') 2026vic
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & $python -B 'reports/2026vic-early-centres/prepare_live.py' --fetch
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $projectArguments = @((Join-Path $PSScriptRoot 'prepare_vec_2026_runs.py'))
        if ($PrepareFor) { $projectArguments += @('--only', $PrepareFor) }
        & $python -B @projectArguments
        exit $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

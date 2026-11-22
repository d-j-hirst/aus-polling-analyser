param(
    [Parameter(Mandatory, Position=0)]
    [ValidateSet('prepare', 'rehearsal', 'election')]
    [string]$Mode,
    [switch]$Watch
)

# The Windows credential is encrypted for the account that saved it. Pass
# the signed URL only to the downloader process, never as a command argument.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$python = Join-Path $root 'analysis/.venv-win/Scripts/python.exe'
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
        & $python -B (Join-Path $PSScriptRoot 'prepare_one_nation_booths.py') 2026vic
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & $python -B (Join-Path $PSScriptRoot 'prepare_vec_2026_runs.py')
        exit $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

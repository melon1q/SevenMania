param(
    [Parameter(Mandatory)][string]$Profiler,
    [Parameter(Mandatory)][string]$Chart,
    [Parameter(Mandatory)][string]$Settings,
    [string]$OutputDirectory = 'build-reflex-validation',
    [ValidateRange(2, 100)][int]$Repeats = 3,
    [double]$StartSeconds = 30,
    [double]$DurationSeconds = 8
)
$ErrorActionPreference = 'Stop'
if ($StartSeconds -lt 0 -or $DurationSeconds -le 2) { throw 'Need a nonnegative start and duration > 2 seconds' }
$profileExe = (Resolve-Path -LiteralPath $Profiler).Path
$chartPath = (Resolve-Path -LiteralPath $Chart).Path
$settingsPath = (Resolve-Path -LiteralPath $Settings).Path
# Never overwrite measurements or feed a profiler-mutated settings file to another run.
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$previousConfig = $env:BMS_PLAYER_CONFIG_PATH
$cases = @(@{Name='unlimited'; Fps=0}, @{Name='240'; Fps=240}, @{Name='vsync'; Fps=-1})
$runs = @()
$manifest = @{
    schemaVersion=1; profiler=$profileExe; profilerSha256=(Get-FileHash -LiteralPath $profileExe).Hash
    chart=$chartPath; chartSha256=(Get-FileHash -LiteralPath $chartPath).Hash
    settingsSha256=(Get-FileHash -LiteralPath $settingsPath).Hash
    start=$StartSeconds; duration=$DurationSeconds; repeats=$Repeats
    capturedAt=[DateTime]::UtcNow.ToString('o'); commit=(& git rev-parse HEAD)
    # Establish the acceptance policy before collecting results.
    allowedRegressionFraction=0.05; physicalInputLatencyMeasured=$false
    runs=$runs
}
try {
    for ($repeat = 1; $repeat -le $Repeats; $repeat++) {
        foreach ($case in $cases) {
            # Alternate pair order to reduce thermal/order bias.
            $modes = if ($repeat % 2) { @('off','reflex') } else { @('reflex','off') }
            foreach ($mode in $modes) {
                $name = "r$repeat-gpu-$($case.Name)-$mode"
                $copy = Join-Path $outputPath "$name.ini"
                Copy-Item -LiteralPath $settingsPath -Destination $copy
                $env:BMS_PLAYER_CONFIG_PATH = $copy
                & $profileExe $chartPath (Join-Path $outputPath "$name.csv") gpu $StartSeconds $DurationSeconds $case.Fps $mode 2>&1 |
                    Tee-Object -FilePath (Join-Path $outputPath "$name.log")
                $exitCode = $LASTEXITCODE
                $runs += @{name=$name; repeat=$repeat; fps=$case.Fps; mode=$mode; exitCode=$exitCode}
                $manifest.runs = $runs
                $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding utf8
                if ($exitCode -ne 0) { throw "Profile failed: $name" }
            }
        }
    }
} finally {
    $env:BMS_PLAYER_CONFIG_PATH = $previousConfig
}

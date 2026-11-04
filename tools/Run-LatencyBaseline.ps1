param(
    [Parameter(Mandatory)][string]$Profiler,
    [Parameter(Mandatory)][string]$Chart,
    [Parameter(Mandatory)][string]$Settings,
    [string]$OutputDirectory = 'build-latency-baseline',
    [int]$Repeats = 2,
    [double]$StartSeconds = 30,
    [double]$DurationSeconds = 8
)
$ErrorActionPreference = 'Stop'
if ($Repeats -lt 1 -or $DurationSeconds -le 0) { throw 'Invalid repeat count or duration' }
$profileExe = (Resolve-Path -LiteralPath $Profiler).Path
$chartPath = (Resolve-Path -LiteralPath $Chart).Path
$settingsPath = (Resolve-Path -LiteralPath $Settings).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$copy = Join-Path $outputPath 'baseline.ini'
Copy-Item -LiteralPath $settingsPath -Destination $copy -Force
$previousConfig = $env:BMS_PLAYER_CONFIG_PATH
$cases = @(@{Name='unlimited'; Fps=0}, @{Name='240'; Fps=240}, @{Name='vsync'; Fps=-1})
@{ profiler=$profileExe; profilerSha256=(Get-FileHash -LiteralPath $profileExe -Algorithm SHA256).Hash;
   chart=$chartPath; chartSha256=(Get-FileHash -LiteralPath $chartPath -Algorithm SHA256).Hash;
   start=$StartSeconds; duration=$DurationSeconds; repeats=$Repeats;
   capturedAt=[DateTime]::UtcNow.ToString('o'); commit=(& git rev-parse HEAD);
   settingsSha256=(Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding utf8
try {
    $env:BMS_PLAYER_CONFIG_PATH = $copy
    for ($repeat = 1; $repeat -le $Repeats; $repeat++) {
        foreach ($mode in @('cpu', 'gpu')) {
            foreach ($case in $cases) {
                $name = "r$repeat-$mode-$($case.Name)"
                & $profileExe $chartPath (Join-Path $outputPath "$name.csv") $mode $StartSeconds $DurationSeconds $case.Fps 2>&1 |
                    Tee-Object -FilePath (Join-Path $outputPath "$name.log")
                if ($LASTEXITCODE -ne 0) { throw "Profile failed: $name" }
                Write-Host "Completed: $name"
            }
        }
    }
} finally {
    $env:BMS_PLAYER_CONFIG_PATH = $previousConfig
}

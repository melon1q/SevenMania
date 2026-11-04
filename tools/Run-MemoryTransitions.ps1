param(
    [Parameter(Mandatory=$true)][string[]]$Charts,
    [ValidateRange(1,10)][int]$Rounds=3,
    [string]$OutputDirectory,
    [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$repository=Split-Path $PSScriptRoot -Parent
Push-Location -LiteralPath $repository
try {
    if($Charts.Count -ne 4){throw 'Pass two charts from each of two song folders.'}
    $resolvedCharts=@($Charts | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
    if(!$OutputDirectory){$OutputDirectory=Join-Path $repository ('build-rmlui-song-list/Validation/MemoryTransitions-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $resolvedOutput=(Resolve-Path -LiteralPath $OutputDirectory).Path
    if(!$SkipBuild){
        & cmake --build build-rmlui-song-list --config Release --target BmsMemoryTransitionTests --parallel 4
        if($LASTEXITCODE -ne 0){throw 'Memory test build failed.'}
    }
    $testExe=Join-Path $repository 'build-rmlui-song-list/MemoryTransition/Release/BmsMemoryTransitionTests.exe'
    $skin=Join-Path (Split-Path $testExe -Parent) 'Assets/Skins/DefaultReference.semaskin'
    $shippingSkin=Join-Path $repository 'build-rmlui-song-list/Release/Assets/Skins/DefaultReference.semaskin'
    if(Test-Path -LiteralPath $shippingSkin){Copy-Item -LiteralPath $shippingSkin -Destination $skin -Force}
    [ordered]@{
        head=(& git rev-parse HEAD);rounds=$Rounds;charts=$resolvedCharts
        hashes=@(Get-FileHash -LiteralPath ($resolvedCharts+@($testExe,$skin,(Join-Path $repository 'src/Main.cpp'))))
        song_hold_ms=600;variant_hold_ms=200;play_hold_ms=700;exit_hold_ms=700
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resolvedOutput 'manifest.json') -Encoding utf8
    & $testExe $resolvedOutput $Rounds @resolvedCharts 2>&1 | Tee-Object -FilePath (Join-Path $resolvedOutput 'run.log')
    if($LASTEXITCODE -ne 0){throw "Memory test failed; see $resolvedOutput"}
    Write-Host "CSV and manifest: $resolvedOutput"
} finally {Pop-Location}

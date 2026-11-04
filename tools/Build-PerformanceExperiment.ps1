param(
    [ValidateSet('Baseline', 'Optimized', 'Tracy', 'Hybrid', 'All')][string]$Variant = 'All'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $projectRoot 'build/Release'
$variants = if ($Variant -eq 'All') { @('Baseline', 'Optimized', 'Tracy', 'Hybrid') } else { @($Variant) }
foreach ($name in $variants) {
    $buildPath = Join-Path $projectRoot ('build-performance-' + $name.ToLowerInvariant())
    $optimized = if ($name -eq 'Baseline') { 'OFF' } else { 'ON' }
    $tracy = if ($name -eq 'Tracy') { 'ON' } else { 'OFF' }
    $hybrid = if ($name -eq 'Hybrid') { 'ON' } else { 'OFF' }
    $arguments = @('-S', $projectRoot, '-B', $buildPath, "-DBMS_USE_TRACY=$tracy",
        "-DBMS_USE_MIMALLOC=$optimized", "-DBMS_USE_ONETBB=$optimized", "-DBMS_HYBRID_RENDERER=$hybrid")
    $sourceFolders = @{}
    foreach ($dependency in @('tracy', 'mimalloc', 'onetbb')) {
        $cache = Join-Path $projectRoot "build-performance-deps/$dependency"
        if (Test-Path -LiteralPath $cache) {
            $source = Get-ChildItem -LiteralPath $cache -Directory | Select-Object -First 1
            if ($source) {
                $arguments += "-DFETCHCONTENT_SOURCE_DIR_$($dependency.ToUpperInvariant())=$($source.FullName)"
                $sourceFolders[$dependency] = $source.FullName
            }
        }
    }
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) { throw "Configure failed: $name" }
    & cmake --build $buildPath --config Release --target BmsPlayer BmsPerformanceExperiment
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $name" }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildPath 'Release/BmsPlayer.exe') -Destination (Join-Path $destination "BmsPlayer-$name.exe")
    if ($optimized -eq 'ON') {
        Copy-Item -LiteralPath (Join-Path $buildPath 'Release/tbb12.dll') -Destination $destination
    }
    Copy-Item -LiteralPath (Join-Path $buildPath 'Release/Assets') -Destination $destination -Recurse -Force
    # Include dependency notices alongside local test builds.
    $notices = Join-Path $destination 'Performance-Licenses'
    New-Item -ItemType Directory -Force -Path $notices | Out-Null
    foreach ($dependency in @('tracy', 'mimalloc', 'onetbb')) {
        if ($sourceFolders.ContainsKey($dependency)) { $folder = $sourceFolders[$dependency] }
        else { $folder = Join-Path $buildPath "_deps/$dependency-src" }
        if (Test-Path -LiteralPath $folder) {
            foreach ($file in @('LICENSE', 'LICENSE.txt', 'LICENSE.rst', 'NOTICE.txt')) {
                if (Test-Path -LiteralPath (Join-Path $folder $file)) {
                    Copy-Item -LiteralPath (Join-Path $folder $file) -Destination (Join-Path $notices "$dependency-$file")
                }
            }
        }
    }
    Write-Host "Ready: $(Join-Path $destination "BmsPlayer-$name.exe")"
}

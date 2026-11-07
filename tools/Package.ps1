param(
    [string]$DestinationFolder = 'dist-trial',
    [string]$BuildFolder = 'build-rmlui-song-list/Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildFolder))
$destination = [IO.Path]::GetFullPath((Join-Path $projectRoot $DestinationFolder))

# Explicit runtime list: no personal songs, settings, tests or old builds.
$runtimeFiles = @(
    'BmsPlayer.exe',
    'Assets/Skins/DefaultReference.semaskin',
    'Assets/Fonts/Michroma-Regular.ttf', 'Assets/Fonts/Oxanium-Regular.ttf', 'Assets/Fonts/Oxanium-Bold.ttf',
    'Assets/Fonts/Licenses/Michroma-OFL.txt', 'Assets/Fonts/Licenses/Oxanium-OFL.txt',
    'Assets/turntable.png', 'Assets/blackbutton.png', 'Assets/whitebutton.png',
    'Assets/hud-icon-slot.png', 'Assets/hud-hi-speed.png', 'Assets/hud-gauge-mode.png', 'Assets/hit-splash.png', 'Assets/startup-splash.png',
    'Assets/levelchange.mp3', 'Assets/click.mp3',
    'Assets/UI/song-list.rml', 'Assets/UI/song-list.rcss',
    'Assets/UI/settings.rml', 'Assets/UI/settings.rcss', 'Assets/UI/settings-probe.rml',
    'Assets/Licenses/Signalsmith-Stretch.txt', 'Assets/Licenses/Signalsmith-Linear.txt',
    'Assets/Licenses/NVIDIA-NVAPI.txt', 'Assets/Licenses/RmlUi.txt',
    'Assets/Licenses/RmlUi-Containers.txt', 'Assets/Licenses/FreeType.txt',
    'Assets/Licenses/FreeType-Licenses.txt', 'Assets/Licenses/Zlib.txt', 'Assets/Licenses/PL_MPEG.txt'
)
$readme = Join-Path $projectRoot 'docs/TrialREADME.md'
foreach ($relative in $runtimeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $relative) -PathType Leaf)) {
        throw "Runtime file missing: $relative. Build the default BmsPlayer target first."
    }
}
if (-not (Test-Path -LiteralPath $readme -PathType Leaf)) { throw 'Trial README is missing.' }
if (Test-Path -LiteralPath $destination) {
    throw "Destination already exists: $destination. Choose a new folder to preserve its contents."
}
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($relative in $runtimeFiles) {
    $target = Join-Path $destination $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildRoot $relative) -Destination $target
}
Copy-Item -LiteralPath $readme -Destination (Join-Path $destination 'README.md')
New-Item -ItemType Directory -Path (Join-Path $destination 'Songs') | Out-Null
Write-Output "Trial distribution: $destination"
Write-Output "Runtime files: $($runtimeFiles.Count), plus README.md and an empty Songs folder."

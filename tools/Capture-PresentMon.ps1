<#
.SYNOPSIS
  Capture SevenMania (BmsPlayer.exe) presentation with PresentMon and classify "tearing" reports.
.DESCRIPTION
  Start a song first, then run this script during gameplay. Requires PresentMon
  (https://github.com/GameTechDev/PresentMon/releases) - pass its path with -PresentMon.
  Run from an elevated PowerShell (ETW tracing needs admin or Performance Log Users).
  Output: Validation/PresentMon/<timestamp>.csv + printed summary.
.EXAMPLE
  .\tools\Capture-PresentMon.ps1 -PresentMon C:\Tools\PresentMon-2.3.0-x64.exe -Seconds 30
.EXAMPLE
  .\tools\Capture-PresentMon.ps1 -Analyze Validation\PresentMon\20261107-120000.csv
#>
param(
    [string]$PresentMon,
    [int]$Seconds = 30,
    [string]$Process = 'BmsPlayer.exe',
    [string]$Analyze
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Get-Column($row, [string[]]$names) {
    foreach ($n in $names) { if ($row.PSObject.Properties.Name -contains $n) { return $n } }
    return $null
}
function Get-Percentile([double[]]$values, [double]$p) {
    if (-not $values -or $values.Count -eq 0) { return [double]::NaN }
    $sorted = $values | Sort-Object
    $sorted[[Math]::Min($sorted.Count - 1, [int][Math]::Round($p * ($sorted.Count - 1)))]
}

if (-not $Analyze) {
    if (-not $PresentMon -or -not (Test-Path $PresentMon)) { throw 'PresentMon executable not found. Pass -PresentMon <path>.' }
    if (-not (Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($Process)) -ErrorAction SilentlyContinue)) {
        throw "$Process is not running. Start a song, then run this script during gameplay."
    }
    $outDir = Join-Path $root 'Validation\PresentMon'
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $Analyze = Join-Path $outDir ((Get-Date -Format 'yyyyMMdd-HHmmss') + '.csv')
    Write-Host "Capturing $Process for $Seconds s -> $Analyze"
    & $PresentMon --process_name $Process --output_file $Analyze --timed $Seconds --terminate_after_timed --stop_existing_session --no_console_stats
    if (-not (Test-Path $Analyze)) { throw 'PresentMon produced no output (admin rights? correct version 2.x?).' }
}

$rows = @(Import-Csv $Analyze)
if ($rows.Count -lt 2) { throw "Too few presents in $Analyze" }
$first = $rows[0]
$modeCol    = Get-Column $first @('PresentMode')
$tearCol    = Get-Column $first @('AllowsTearing')
$frameCol   = Get-Column $first @('MsBetweenPresents', 'FrameTime')
$displayCol = Get-Column $first @('MsBetweenDisplayChange', 'DisplayedTime')
$dropCol    = Get-Column $first @('Dropped')
$syncCol    = Get-Column $first @('SyncInterval')

$num = { param($v) $d = 0.0; if ([double]::TryParse($v, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$d)) { $d } else { $null } }
$frames  = [double[]]@($rows | ForEach-Object { & $num $_.$frameCol } | Where-Object { $_ -ne $null -and $_ -gt 0 })
$display = if ($displayCol) { [double[]]@($rows | ForEach-Object { & $num $_.$displayCol } | Where-Object { $_ -ne $null -and $_ -gt 0 }) } else { @() }
$dropped = if ($dropCol) { @($rows | Where-Object { $_.$dropCol -eq '1' }).Count } else { 0 }
$tearing = if ($tearCol) { @($rows | Where-Object { $_.$tearCol -eq '1' }).Count } else { 0 }
$modes   = $rows | Group-Object $modeCol | Sort-Object Count -Descending

Write-Host ''
Write-Host "Presents: $($rows.Count)  (file: $Analyze)"
Write-Host 'PresentMode:'
$modes | ForEach-Object { Write-Host ("  {0,-40} {1,7}  {2,5:P1}" -f $_.Name, $_.Count, ($_.Count / $rows.Count)) }
if ($syncCol) { Write-Host ('SyncInterval: ' + (($rows | Group-Object $syncCol | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join ', ')) }
Write-Host ("AllowsTearing presents: {0}   Dropped: {1} ({2:P2})" -f $tearing, $dropped, ($dropped / $rows.Count))
Write-Host ("Present interval ms  P50 {0:N3}  P99 {1:N3}  P99.9 {2:N3}  max {3:N3}" -f (Get-Percentile $frames .5), (Get-Percentile $frames .99), (Get-Percentile $frames .999), ($frames | Measure-Object -Maximum).Maximum)
if ($display.Count) {
    $p50 = Get-Percentile $display .5
    $late = @($display | Where-Object { $_ -gt $p50 * 1.5 }).Count
    Write-Host ("Display change ms    P50 {0:N3}  P99 {1:N3}  P99.9 {2:N3}  >1.5xP50: {3} ({4:P2})" -f $p50, (Get-Percentile $display .99), (Get-Percentile $display .999), $late, ($late / $display.Count))
}

$top = $modes[0].Name
$verdict =
    if ($tearing -gt 0 -and $top -match 'Independent|Hardware') { 'REAL TEARING POSSIBLE: tearing presents on an independent-flip path.' }
    elseif ($top -match 'Legacy|Copy.*GDI|Blit') { 'GDI/Blit path dominant: check gGpuUnavailable / [GDI] HUD tag and presentation-diagnostics.csv fallback stage.' }
    elseif ($dropped / $rows.Count -gt 0.01) { 'Flip model, no tearing: >1% dropped frames -> perceived "tearing" is most likely judder/frame drops.' }
    else { 'Flip model, no tearing, few drops: look at display-change P99 spikes and per-play CSV.' }
Write-Host ''
Write-Host "Verdict: $verdict"

<#
.SYNOPSIS
  Launch SevenMania with an A/B presentation label for presentation-diagnostics.csv.
.DESCRIPTION
  Every switch is read once at startup (close any running BmsPlayer first) and is appended
  to the CSV "variant" label automatically. FPS mode (VSYNC / cap 240 / cap 600) is the
  in-game setting and is recorded separately.

  Upload
    -SkipUnchanged      skip unchanged HUD/overlay uploads (default off; measured worse)
  Pacing
    -CapPacing On|Off   FPS caps at a refresh multiple start each frame before a vblank (default On)
    -VsyncPacing        same alignment in VSync mode (default off)
    -NoReadyWake        VSync+aligned: do NOT fall back to ready-signal painting when the
                        output queue is still busy (default: fallback on)
    -VblankPacing       legacy: turn both cap and VSync alignment on
    -VblankLeadUs N     how early before the vblank a frame starts (default 2000)
    -FrameLatency N     output queue depth (default 1; 2 adds one refresh of latency)
  BGA
    -BgaGpuScale        upload plain full-screen BGA stills at source resolution and scale
                        them on the GPU (default off)
    -NoVideoNv12        upload BGA video as RGB again (NV12 GPU conversion is the default;
                        -VideoNv12 is accepted and keeps the default)
  Diagnostics
    -RawStallClassifier with aligned pacing, keep the old loop-stall threshold (1.0x interval)
  Benchmark
    -NoInputBenchmark   every note MISS, keysounds at note time, BGA/notes rendered, no miss
                        layer, gauge never fails, no record saved, keeps running without focus
                        ("BENCH · NO INPUT · n/N" badge at the top-left)
    -BenchRepeat N      with -NoInputBenchmark: the chart you start is played N times in a row
                        (default 2, 0 = off), then the game closes itself
    -NoBenchExit        stay open after the last benchmark play
.EXAMPLE
  .\tools\Start-PresentVariant.ps1 -Variant base -CapPacing Off -NoInputBenchmark
  .\tools\Start-PresentVariant.ps1 -Variant pace -NoInputBenchmark
  .\tools\Start-PresentVariant.ps1 -Variant vsync -VsyncPacing -NoInputBenchmark
  .\tools\Start-PresentVariant.ps1 -Variant vsync -VsyncPacing -NoReadyWake -NoInputBenchmark
#>
param(
    [Parameter(Mandatory)][string]$Variant,
    [switch]$SkipUnchanged,
    [ValidateSet('On', 'Off')][string]$CapPacing = 'On',
    [switch]$VsyncPacing,
    [switch]$NoReadyWake,
    [switch]$VblankPacing,
    [switch]$RawStallClassifier,
    [switch]$BgaGpuScale,
    [switch]$VideoNv12,
    [switch]$NoVideoNv12,
    [switch]$NoInputBenchmark,
    [ValidateRange(0, 100)][int]$BenchRepeat = 2,
    [switch]$NoBenchExit,
    [ValidateRange(200, 8000)][int]$VblankLeadUs = 2000,
    [ValidateRange(1, 3)][int]$FrameLatency = 1,
    [string]$Exe
)
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'build-rmlui-song-list\Release\BmsPlayer.exe' }
if (-not (Test-Path $Exe)) { throw "BmsPlayer.exe not found: $Exe" }
$on = { param($value) if ($value) { '1' } else { '0' } }
Remove-Item Env:SEVENMANIA_VBLANK_PACING -ErrorAction SilentlyContinue
$env:SEVENMANIA_DIAG_VARIANT = $Variant
$env:SEVENMANIA_SKIP_UNCHANGED_UPLOADS = & $on $SkipUnchanged
$env:SEVENMANIA_CAP_VBLANK_PACING = & $on ($VblankPacing -or $CapPacing -eq 'On')
$env:SEVENMANIA_VSYNC_VBLANK_PACING = & $on ($VblankPacing -or $VsyncPacing)
$env:SEVENMANIA_VSYNC_READY_WAKE = & $on (-not $NoReadyWake)
$env:SEVENMANIA_ALIGNED_STALL_CLASSIFIER = & $on (-not $RawStallClassifier)
$env:SEVENMANIA_VBLANK_LEAD_US = "$VblankLeadUs"
$env:SEVENMANIA_FRAME_LATENCY = "$FrameLatency"
$env:SEVENMANIA_BGA_GPU_SCALE = & $on $BgaGpuScale
$env:SEVENMANIA_VIDEO_NV12 = & $on (-not $NoVideoNv12)
$env:SEVENMANIA_NO_INPUT_BENCHMARK = & $on $NoInputBenchmark
$env:SEVENMANIA_BENCH_REPEAT = "$BenchRepeat"
$env:SEVENMANIA_BENCH_EXIT = & $on (-not $NoBenchExit)
Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe)
Write-Host ("Started $Exe`n  variant=$Variant  cap-vblank=$($env:SEVENMANIA_CAP_VBLANK_PACING)  vsync-vblank=$($env:SEVENMANIA_VSYNC_VBLANK_PACING)" +
    "  ready-wake=$($env:SEVENMANIA_VSYNC_READY_WAKE)  lead=${VblankLeadUs}us  latency=$FrameLatency" +
    "`n  skip-unchanged=$($env:SEVENMANIA_SKIP_UNCHANGED_UPLOADS)  aligned-stall-classifier=$($env:SEVENMANIA_ALIGNED_STALL_CLASSIFIER)  bga-gpu-scale=$($env:SEVENMANIA_BGA_GPU_SCALE)  video-nv12=$($env:SEVENMANIA_VIDEO_NV12)  noinput=$($env:SEVENMANIA_NO_INPUT_BENCHMARK) repeat=$BenchRepeat exit=$($env:SEVENMANIA_BENCH_EXIT)")

# Reflex stages 3–4

## Behavior

- Graphics: Off / NVIDIA Reflex. Default Off. Anti-Lag 2 is labeled unavailable.
- Uses the actual D3D11 device, NVIDIA vendor ID and SDK capability queries. The
  selection is disabled without a supported driver/GPU or when GPU rendering is off.
- Frame ID increments once at frame start, before input processing. Marker order:
  SimulationStart → InputSample → SimulationEnd → RenderStart → RenderEnd →
  PresentStart → PresentEnd. Deferred frames do not fabricate Present markers.
- OFF keeps the existing frame pacer and emits supported measurement markers.
  ON calls NvAPI_D3D_Sleep once before pumping input, with the selected numeric FPS
  interval or the 1500 Hz unlimited/VSync loop ceiling. The ordinary software pacer
  does not add another wait on this path. Present remains nonblocking. Boost is off.
- Keyboard judgement and WASAPI continue to use the existing audio clock. No SDK
  call is made on the audio thread. Pause turns Reflex off; resume re-applies it.
- Resize/device destruction releases per-device state and re-probes on creation.
  SDK operation failures disable Reflex for that process session and restore the
  ordinary pacer; no repeated initialization/retry per frame. Debug output records
  the operation and NVAPI error. Missing runtimes and non-NVIDIA devices run normally.
- The NVAPI module remains initialized/loaded until process exit. Unload during
  resize reproduced a driver callback crash during testing and is deliberately avoided.
- NVAPI GetSleepStatus.bLowLatencyMode is observed after visible frame presentation;
  it may still report false immediately after setting the mode, or in hidden windows.

## Validation (2026-10-22)

GTX 1660 Ti, driver 617.14. The hardware test displayed a small test window and
verified ON, 60 frames of Sleep/markers/Present, driver-reported low latency enabled,
then OFF. Resize and ordinary GPU rendering tests passed. Controller tests cover
marker order/IDs, duplicate Sleep rejection, deferred frames, cached configuration,
configuration/Sleep/marker errors and session failure persistence across re-attach.
An SDK-disabled build also passed GPU and controller tests.

Actual gameplay smoke test: Creamy Sugary OVERDRIVE!!! ANOTHER, same 30–38 second
segment, 240 FPS setting, mimalloc/oneTBB + Hybrid renderer, one OFF and one ON run.
Both had GPU fallback=0, audio render failures=0 and empty-padding events=0.
ON reported reflex_active=1 and driver_low_latency=1. These are SDK activation and
stability checks, not evidence of lower physical input-to-photon latency. The small
single-run sample does not establish comparative performance. Further tuning and
combination/recovery testing in stages 5–6 remain separate work.

Run the profiler with an isolated settings copy via BMS_PLAYER_CONFIG_PATH:
`BmsGameplayProfile chart.bms trace.csv gpu 30 8 240 off` (or final argument `reflex`).
Build without the SDK integration with `-DBMS_ENABLE_NVIDIA_REFLEX=OFF`.

## Official references

- https://docs.nvidia.com/nvapi/group__dx.html (Sleep, SetSleepMode, GetSleepStatus, markers)
- https://github.com/NVIDIA/nvapi (MIT SDK headers and public function IDs)

## Browse records

Unplayed charts draw no combo/accuracy/rank labels. Played charts show saved best
accuracy and max combo; rank is the grade alone, using the same thresholds as results.
The grade uses a 64-pixel bold font. A raster test checks no text for missing records
and the enlarged grade height for an existing record.

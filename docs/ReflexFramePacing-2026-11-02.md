# Reflex 프레임 페이싱 조사 — 2026-11-02

## 결론

NVAPI 직접 통합이다. Streamline은 사용하지 않는다. 다음 확인된 문제를 수정했다.

1. OFF에서 SDK Sleep을 생략했다. NVAPI 공식 권장에 맞춰 지원 장치에서는 OFF도 프레임당 한 번 호출한다. OFF의 SDK minimumIntervalUs=0은 유지하며 자체 제한은 FramePacer가 맡는다.
2. GPU 텍스트/스킨 자원 준비가 RenderStart 마커보다 먼저 실행됐다. 마커를 준비 작업 앞에 배치했다.
3. 수치 FPS 제한의 FramePacer는 처리 지연 뒤 다음 격자 슬롯까지 건너뛰었다. 600/1000FPS에서 타이머 초과 지연도 관측됐다. 500FPS 이상 수치 제한은 시작 간 최소 간격으로 제한하고, 해당 구간에서는 마지막 1ms 이내를 입력 메시지를 처리하는 짧은 폴링으로 마무리한다. 타이머는 그 앞에서 깨운다. CPU 사용량 증가와 맞바꾼 변경이다.

**Reflex ON에서 1200→600FPS로 보이는 간헐적 지연의 최종 하위 원인까지 해결한 것은 아니다.** ON 무제한의 평균 FPS는 전후 거의 같고 1% Low는 이번 표본에서 개선되지 않았다. 느린 1% 구간은 SDK Sleep보다 CPU 프레임 처리가 대부분이었다. 관련 없는 BGA·오디오·판정·컨트롤러 로직은 수정하지 않았다.

## 공식 근거와 통합 확인

- 로컬 SDK: third_party/nvapi/nvapi.h 및 nvapi_interface.h. System32의 nvapi64.dll을 프로세스 수명 동안 유지하고 QueryInterface로 함수 주소를 얻는다.
- 초기화: NVIDIA vendor 0x10de, D3D11 장치, NVAPI 초기화/함수 존재/GetSleepStatus 성공을 검사한다. 실패 시 자체 Pacer로 복귀한다.
- 설정: SetSleepMode의 LowLatencyMode ON/OFF, Boost=false, minimumIntervalUs, UseMarkersToOptimize=true를 사용한다. 동일 설정은 다시 보내지 않는다.
- Sleep: 입력 메시지 처리 전 beginFrame에서 한 번. Phase가 Idle이 아니면 중복 호출하지 않는다. OFF 마커도 유지한다.
- 마커 순서: SimulationStart → InputSample → SimulationEnd → RenderStart → RenderEnd → PresentStart → PresentEnd. INPUT_SAMPLE=6 등 로컬 NVAPI 열거값과 일치한다. 제출을 미룬 프레임에 가짜 Present 마커를 만들지는 않는다.
- NVAPI는 OFF/interval=0에서도 Sleep을 **권장**한다. Streamline의 ‘항상 호출해야 한다’는 문구를 직접 NVAPI의 필수 규정과 혼동하면 안 된다.

공식 문서: [NVAPI Sleep / SetSleepMode](https://docs.nvidia.com/nvapi/group__dx.html), [NVAPI 공식 헤더](https://raw.githubusercontent.com/NVIDIA/nvapi/main/nvapi.h), [NVIDIA Reflex SDK](https://github.com/NVIDIA-RTX/REFLEX).

## 가설 판정

| 가설 | 확인 결과 |
|---|---|
| A. FramePacer + Reflex 이중 대기 | ON은 due 검사를 우회하고 SDK interval이 제한한다. 측정 ON의 Pacer OS wait는 0이다. 중복 대기를 원인으로 확정할 근거 없음. |
| B. Sleep 중복 | beginFrame의 phase 가드와 회귀 테스트에서 프레임당 한 번. 제출 재시도에서는 다시 호출하지 않음. |
| C. 마커 위치 | RenderStart 앞의 GPU 자원 준비를 확인해 수정. |
| D. Present/VSync | 수치/무제한 시험은 VSync OFF. Present(0, DO_NOT_WAIT)와 준비 이벤트를 사용한다. 제출 간격은 시뮬레이션 FPS와 다르다. 드라이버 강제 VSync/외부 제한까지 배제한 측정은 아님. |
| E. 전환 상태 | OFF/ON/실패/해제/재생성 회귀 통과. 지속적인 ON/OFF 재설정이나 중복 Sleep은 관측 안 됨. |

FramePacer.due는 시작 시각을 기록하고, advance는 다음 제한 시각을 정한다. 기존 60/144/240/400/VSync의 격자 방식은 유지한다. 고FPS arm은 일찍 깨우고 pollBeforeDeadline은 남은 짧은 시간 동안 메시지를 펌핑한다. Reflex ON에서도 advance는 실행되지만 실제 다음 프레임 진입은 Reflex가 결정하므로 자체 타이머를 추가로 기다리지 않는다.

설정 0은 진짜 무한이 아니라 **1500FPS 상한**이다. 자체 period는 666,667ns이고 Reflex interval은 올림한 667us(약 1499.25FPS 상한)다. 600/1000은 현재 일반 UI 목록에 없으므로 진단 도구가 실제 내부 목표값으로 직접 설정했다. UI 항목을 임의로 추가하지 않았다.

## 측정 환경과 범위

GTX 1660 Ti (렌더 장치의 adapter description 확인), D3D11/DirectComposition, 1920×1080 보더리스, VSync OFF, 같은 DefaultReference 스킨과 채보, 30초부터 실제 엔진 8초 자동 입력 재생이다. 개인 INI 대신 실행별 격리 INI를 썼고 컨트롤러/자원 표시/정확도 재시작은 껐다. 오디오는 재생 시계를 유지하되 볼륨 0이다.

최종 6조건×전후=12캡처는 **동일 바이너리**에서 진단용 legacy 경로만 바꿨다. 초기 탐색과 중간 수정 시험은 최종 비교 표에 혼합하지 않았다. 각 캡처 중앙 2–7초를 집계했다. 단일 짧은 반복이므로 장시간 tail latency를 대표하지 못한다. 리플레이는 같은 예정 입력을 메시지로 전송하지만 실제 도착 판정은 미세하게 다를 수 있다.

- CPU 측 시간은 steady_clock의 경과 시간이다. 스케줄링 대기가 섞일 수 있으며 GPU 실행 시간과 다르다.
- sleep는 SDK 호출 경과 시간, wait는 메시지/GPU 준비/타이머를 함께 기다리는 커널 대기이다. 마지막 폴링의 바쁜 대기 시간은 wait 열에 포함되지 않는다.
- CPU frame은 실제 updatePlaybackFrame 호출 구간이며 Sleep/프레임 사이 대기를 제외한다. paint와 D2D command submission은 별도 scope다.
- 1% Low = 가장 느린 1% 프레임 간격의 평균을 FPS로 환산했다. P99의 역수와 다르다.
- CPU 사용률은 전체 8초 프로세스의 모든 스레드 CPU 시간 / (실제 16 논리 CPU × 8초)이다. 프레임 통계의 중앙 5초와 집계 구간이 다르다.
- Present 성공 간격은 제출 간격이지 실제 scanout이 아니다. GPU timestamp도 전체 draw bracket으로, CPU 호출 시간과 구분한다.
- 프레임 기록은 미리 할당한 65,536행 버퍼에만 쓰고 재생 종료 후 저장한다. 최종 전 조건 dropped=0, GPU fallback=0이다.

## 수정 전후 프레임 결과

표의 FPS는 내부 갱신 FPS, 시간은 ms다. B=수정 전 / A=수정 후.

| 제한 / Reflex | 평균 FPS B→A | P95 B→A | P99 B→A | 최대 B→A | 1% Low B→A | CPU 전체기준 % B→A |
|---|---:|---:|---:|---:|---:|---:|
| 600 / OFF | 588.6→593.9 | 2.118→1.718 | 3.170→2.229 | 3.640→3.020 | 294.0→418.0 | 7.5→9.5 |
| 600 / ON | 599.7→599.6 | 1.682→1.682 | 1.741→1.729 | 2.111→2.239 | 557.2→551.9 | 11.6→11.1 |
| 1000 / OFF | 846.4→960.0 | 2.008→1.215 | 2.207→1.833 | 3.649→2.553 | 402.0→509.6 | 8.2→11.4 |
| 1000 / ON | 987.5→987.6 | 1.018→1.017 | 1.452→1.447 | 2.377→2.555 | 625.0→626.5 | 11.5→11.5 |
| 무제한(1500) / OFF | 1418.9→1418.2 | 0.863→0.863 | 1.470→1.476 | 2.707→3.279 | 615.9→597.8 | 11.0→11.8 |
| 무제한(1500) / ON | 1223.6→1223.5 | 0.837→0.834 | 1.427→1.438 | 1.987→2.654 | 661.4→636.6 | 11.3→11.5 |

600/1000 OFF의 페이싱은 개선됐지만 CPU 사용량이 증가했다. 무제한과 ON의 평균/꼬리 시간은 개선이라고 주장할 정도로 좋아지지 않았다. 최대 프레임은 일부 조건에서 악화되었으므로 완전 해결로 해석하지 않는다.

## 구간별 평균 (ms)

각 셀은 전→후다. Scope는 중첩될 수 있어 임의로 모두 더하면 안 된다.

| 조건 | Pacer 커널 대기 | SDK Sleep | CPU frame | CPU paint | D2D 명령/EndDraw | Present 호출 | Present 제출 간격 P99 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 600 / OFF | 1.037→0.398 | 0.000→0.001 | 0.593→0.549 | 0.473→0.475 | 0.045→0.045 | 0.012→0.012 | 5.455→5.323 |
| 600 / ON | 0.000→0.000 | 1.038→1.043 | 0.625→0.619 | 0.468→0.465 | 0.043→0.042 | 0.011→0.011 | 5.873→5.754 |
| 1000 / OFF | 0.590→0.000 | 0.000→0.001 | 0.553→0.485 | 0.446→0.440 | 0.031→0.028 | 0.008→0.007 | 5.270→4.991 |
| 1000 / ON | 0.000→0.000 | 0.473→0.471 | 0.536→0.538 | 0.433→0.435 | 0.026→0.026 | 0.007→0.007 | 5.668→5.645 |
| 1500 / OFF | 0.000→0.000 | 0.000→0.001 | 0.467→0.465 | 0.423→0.423 | 0.019→0.019 | 0.005→0.005 | 4.965→5.012 |
| 1500 / ON | 0.000→0.000 | 0.304→0.303 | 0.510→0.511 | 0.423→0.423 | 0.021→0.021 | 0.006→0.006 | 5.067→5.517 |

## 지연 감소 기능과 남은 원인

SDK GetLatency를 종료 직후 한 번 호출했다. status=0이고 유효한 마지막 frame report만 사용했다. 입력 샘플 마커부터 GPU render end까지의 평균은 아래와 같다. 이 값은 키보드 스캔/USB/오디오/디스플레이 scanout까지 포함한 실제 입력-표시 지연이 아니다. 표본도 마지막 64 report 중 유효한 일부여서 전체 8초를 대표하지 않는다.

| 조건 | SDK 입력마커→GPU완료 평균 ms 전→후 | 유효 report 수 전→후 |
|---|---:|---:|
| 600 / OFF | 1.650→2.362 | 27→52 |
| 600 / ON | 1.571→1.396 | 26→26 |
| 1000 / OFF | 1.477→2.343 | 20→34 |
| 1000 / ON | 1.403→1.381 | 16→16 |
| 1500 / OFF | 1.775→2.252 | 19→25 |
| 1500 / ON | 1.472→1.351 | 13→13 |

ON의 driver_low_latency=1, OFF=0을 확인했고 SDK 마커 보고도 유효했다. ON 보고값은 이 표본에서 작아졌지만, 작고 순서가 고정된 표본으로 통계적인 지연 개선이나 Reflex 인증을 주장하지 않는다. OFF 보고값은 오히려 커져 FPS 안정성 개선이 입력-표시 지연 개선을 보장하지 않는 것도 드러났다.

무제한 ON의 느린 1% 구간을 이전 CPU 프레임과 정렬해 보면, 수정 전 평균 간격 1.512ms 중 직전 CPU frame 1.504ms / 현재 Sleep 0.004ms, 수정 후 1.571ms 중 직전 CPU frame 1.544ms / Sleep 0.011ms였다. 관측된 약 600FPS 수준의 순간 하락은 이 캡처에서는 긴 SDK Sleep으로 설명되지 않는다. CPU frame의 모든 하위 비용을 분해한 것은 아니므로 정확한 하위 함수/드라이버 잠금/OS 스케줄링 원인은 남아 있다.

탐색 중 ON/1000에 41.788ms 간격이 있었으나 SDK 대기는 0.507ms, 직전 CPU frame은 0.494ms여서 대부분은 그 밖의 구간이었다. 최종 쌍에서는 재현되지 않았다. 이를 이번 수정으로 없앴다고 주장하지 않는다. PIX/GPUView/ETW 스케줄링 추적과 장시간 반복 측정이 다음 조사 대상이다. BGA나 오디오 타이밍을 바꿔 이 값을 숨기지는 않았다.

## 변경 파일 / 검증

- src/LowLatency.h: beginFrame — 지원 장치의 OFF Sleep 유지, 중복 방지 유지.
- src/Main.cpp: windowProc — RenderStart를 GPU 준비 앞으로 이동. waitForPlaybackWork — 진단 scope만 추가.
- src/FramePacer.h: reset/advance/pollBeforeDeadline/arm — 수치 제한 최소 간격과 고FPS 일찍 깨우기.
- src/HudFrameProbe.h, tests/HudFrameProfile.cpp: SDK Sleep/커널 대기/프로세스 CPU/SDK 지연 report 계측. legacy 비교는 진단 빌드에서만 가능하다.
- tools/Run-HudFrameStudy.py, RunReflexStudy.py, AnalyzeReflexStudy.py: 격리 캡처 및 오프라인 집계.
- tests/LowLatencyTests.cpp, FrameTests.cpp: OFF 호출, 한 프레임 한 Sleep, 마커 순서, 전환/실패/재생성, 비싼 프레임/1500 상한 회귀.

Release BmsPlayer.exe 및 진단 실행 파일 빌드 성공. BmsLowLatencyTests와 BmsFrameTests 통과. 컨트롤러·BGA·판정·오디오 엔진 파일은 변경하지 않았다. 사용자 기존 변경 사항은 분리하여 보존한다.

## 재현 절차

```powershell
cmake --build build-rmlui-song-list --config Release --target BmsHudFrameProfile
python tools/RunReflexStudy.py --label my-reflex --duration 20 --repeats 2
python tools/AnalyzeReflexStudy.py my-reflex
```

label은 기존 결과와 겹치지 않게 지정한다. 원시 CSV/로그/설정/실행 파일 및 소스 해시는 build-rmlui-song-list/Validation/HudFrameStudy/에 있다. --reflex-before는 진단 실행 파일에서만 수정 전 세 경로를 복원한다. 일반 실행 파일에는 legacy 경로 분기가 컴파일 시 제거된다. 캡처는 버퍼 제한 때문에 4–40초이며 긴 검증은 반복 횟수로 늘린다.

다른 곡은 RunReflexStudy.py에 --chart "C:/곡/채보.bms"를 추가한다. 현재 자동 재현 도구는 로컬 테스트 곡 경로가 필요하며 모든 설치 환경에 그 곡이 있다고 가정하면 안 된다. 외부 FPS 제한, 강제 VSync/VRR, 전원/온도 조건을 기록해 같은 조건에서 반복하고 수동 플레이/물리 입력-표시 지연도 별도로 확인해야 한다.

# Reflex ON / 1500FPS 상한 CPU 지연 추가 조사 — 2026-11-02

## 결론과 확정 수준

`27e3bc7`과 [이전 보고서](ReflexFramePacing-2026-11-02.md)를 먼저 확인했다. FramePacer, Reflex Sleep 횟수·위치, 마커 순서·위치, 제한값은 그대로 유지했다. 이번 변경은 진단 빌드의 계측과 오프라인 분석뿐이다. 미검증 최적화나 게임 동작 수정은 하지 않았다.

이번 정상 플레이 표본의 주된 느린 CPU 호출 경로는 다음과 같다.

```
updatePlaybackFrame
  UpdateWindow → windowProc(WM_PAINT)
    CompositionPresenter::present → drawFrame → uploadHybrid
      배경 변경 행 memcmp
      ID2D1Bitmap::CopyFromMemory
      uploadedPixels_ = hybridPixelOwner_
```

마지막 소유권 대입 한 줄에도 큰 시간이 기록됐다. 최신 세분화 ON 캡처의 느린 CPU 1% 평균은 1.792ms이며, 그중 uploadHybrid 1.051ms, 배경 업로드 0.967ms였다. 배경 안에서는 비교 0.150ms, CopyFromMemory 0.374ms, 소유권 대입 0.443ms로 나뉘었다. 이 합계는 같은 프레임 집합의 중첩되지 않는 하위 구간이다. 배경 복사 프레임은 전체의 490/20,632개(2.38%)지만 느린 CPU 1%의 193/206개(93.69%)를 차지했다.

**확인된 병목 위치는 배경 갱신·소유권 교체 경로다.** 다만 대입 비용 중 마지막 참조 해제/힙 해제/스케줄링의 각각의 비중, CopyFromMemory 안에서의 드라이버 대기와 CPU 복사의 비중, Reflex가 이 경로의 비용을 늘리는 정확한 원인은 아직 확정하지 않았다. CPU 호출 경과 시간을 GPU 실행 시간으로 해석하지 않는다.

정상 플레이와 별개로, 포커스/일시정지 처리 중 메시지 펌프에서 동기 화면 그리기가 실행되는 수십 ms 경로도 확인했다. 두 종류의 지연을 섞어 Reflex Sleep 문제로 결론 내리면 안 된다.

## 시험 조건과 기록 방법

- 실제 렌더 장치 GTX 1660 Ti, Windows/D3D11/DirectComposition, Hybrid renderer ON, 1920×1080 보더리스, VSync OFF.
- 현재 DefaultReference 원본 복사본, 이미지 판정/콤보 ON, 동일 Creamy Sugary OVERDRIVE!!! 채보의 30초부터 실제 오디오 시계·게임 로직·메시지 입력·GPU 렌더 경로를 20초 실행했다. 오디오 볼륨은 0, 컨트롤러/자원 표시/정확도 재시작은 OFF, 실행별 격리 INI를 사용했다.
- 설정 FPS=0: 이전 구현대로 자체 1500FPS 상한, Reflex ON SDK interval=667µs다. 무한 FPS라는 의미가 아니다. OFF/ON 로그의 실제 driver_low_latency=0/1을 확인했다.
- 각 캡처 중앙 2–19초, 17초만 집계했다. 분위수는 정렬한 값의 floor((N−1)×q)를 사용했다. 임계값 빈도는 해당 17초 동안의 횟수/초다.
- 첫 비교는 OFF→ON→ON→OFF 순서 4회다. 같은 바이너리에서 상세 scope를 끈 boundary OFF/ON도 각 1회 실행했다. 이후 소유권 구간을 추가 분리한 OFF/ON 각 1회를 별도 표로 제시한다. 후자의 실행 시점/시스템 상태가 다르므로 앞 표와 합치거나 개선 전후로 해석하지 않는다.
- 현재 작업 트리에는 기존 사용자 변경이 있다. 순수 `27e3bc7` 체크아웃을 비교한 것이 아니다. 각 실행 manifest에 바이너리·관련 소스·스킨·채보·INI SHA256을 남겼다. 기존 변경은 되돌리지 않았다.
- 프레임/호출 구간은 사전 할당된 65,536행 × 최대 128개 span에 기록한다. 플레이 중 파일 로그·동적 컨테이너 확장 없이 종료 후 CSV로 저장한다. UI thread cycle counter도 기록하되, 이를 µs나 GPU 시간으로 환산하지 않는다.
- CPU 구간은 updatePlaybackFrame의 begin/finish, interval은 다음 CPU 프레임 시작까지다. 따라서 CPU 프레임 i의 지연은 보통 i+1의 interval에 나타난다. Sleep/입력 펌프는 시작 전 pending 구간이므로 같은 CSV 행에 있어도 CPU frame 안의 시간은 아니다.
- 중첩 scope 시간은 포함 시간이다. 합산 금지. 분석기는 느린 프레임의 span을 실제 CPU 시작/끝에 잘라 검증한다. span 내보내기는 CPU≥1.25ms 또는 interval≥1.5ms인 프레임 및 앞뒤 2개로 한정한다. 따라서 작은 CPU tail의 모든 span이 있는 것은 아니며 coverage를 함께 출력한다. 최신 ON 느린 1%는 206/206개가 기록됐다. OFF는 119/237개여서 잘린 span 평균을 전체 OFF 1% 평균으로 사용하지 않았다.
- 최종 비교·boundary·소유권 세분화 캡처 모두 frame dropped=0, span dropped=0, GPU fallback=0, paused/unfocused/auto-resume=0이다. 중간 탐색의 포커스 손실/일부 span overflow 표본은 정상 비교 표에서 제외했다.

## P99·최악 프레임·빈도

모든 시간은 ms다. interval은 CPU 프레임 시작 간격이며 물리적 화면 표시 간격이 아니다.

| 캡처 | 프레임 수 | interval P99 | P99.9 | 최악 interval | CPU P99 | 최악 CPU | ≥1.667ms 횟수(회/초) | ≥2ms 횟수(회/초) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| final r1 OFF | 24,021 | 1.5459 | 1.8942 | 3.0011 | 0.7543 | 2.0474 | 80 (4.706) | 17 (1.000) |
| final r1 ON | 20,859 | 1.3327 | 1.6897 | 2.4386 | 1.3182 | 2.3316 | 26 (1.529) | 6 (0.353) |
| final r2 ON | 20,857 | 1.3415 | 1.6668 | 2.5637 | 1.3324 | 2.5604 | 21 (1.235) | 4 (0.235) |
| final r2 OFF | 24,088 | 1.5521 | 1.9493 | 3.0183 | 0.7564 | 2.1260 | 83 (4.882) | 22 (1.294) |
| boundary OFF | 24,084 | 1.5500 | 1.8700 | 2.8795 | 0.7176 | 1.9493 | 85 (5.000) | 12 (0.706) |
| boundary ON | 20,851 | 1.3535 | 1.6437 | 2.6003 | 1.3436 | 2.5942 | 20 (1.176) | 7 (0.412) |

이 표본에서는 **ON의 CPU tail은 크지만, 프레임 시작 간격의 P99와 ≥2ms 빈도는 OFF보다 작았다.** Reflex ON이 모든 지표에서 나쁘다는 전제는 실측과 맞지 않는다. CPU 시간과 interval을 구별해야 한다. 상세/경계 계측의 tail은 이 표본에서 비슷하지만 계측 영향이 없다는 증명은 아니다. 평균 갱신 FPS는 상세 OFF 약 1413–1417, ON 약 1227로 기존 관측 범위다.

소유권 세분화 후 별도 실행:

| 캡처 | 프레임 수 | interval P99 / 최악 | CPU P99 / 최악 | ≥1.667ms | ≥2ms |
|---|---:|---:|---:|---:|---:|
| attributed OFF | 23,703 | 1.8725 / 4.2696 | 0.9423 / 2.6951 | 457 (26.882/초) | 105 (6.176/초) |
| attributed ON | 20,632 | 1.6305 / 2.5681 | 1.6208 / 2.4821 | 172 (10.118/초) | 24 (1.412/초) |

이 실행은 양쪽 tail이 더 컸다. 시스템 부하·열·클록·계측 추가의 영향을 분리한 실험이 아니므로 원인을 단정하지 않는다. 위 두 정상 비교 세트에서 ≥5ms/≥10ms interval은 없었다. 장시간 플레이의 극단적인 tail이 없다는 뜻은 아니다.

## CPU 세부 구간

attributed OFF/ON에서 CPU 시간이 가장 큰 1% 행 각각의 scope 평균이다. OFF 237개, ON 206개다. 단위 ms. 행 합계에는 앞서 설명한 pending scope도 포함되므로 전체를 더해 CPU 시간을 계산하지 않는다. ON의 아래 렌더 하위 구간은 CPU span과 일치함을 확인했다.

| 측정 경로 | OFF 느린 1% 평균 | ON 느린 1% 평균 |
|---|---:|---:|
| 입력 메시지 펌프(프레임 시작 전) | 0.006544 | 0.009783 |
| pressLane / releaseLaneInput | 0.000314 / 0.000123 | 0.000462 / 0.000157 |
| 오디오 위치 조회 | 0.000873 | 0.000701 |
| AsyncVideoBga::update | 0.000181 | 0.000169 |
| updateBgaState | 0.000066 | 0.000056 |
| 홀드·지뢰·미스 처리 등 GameLogic | 0.008644 | 0.003582 |
| drawGameScreen | 0.525268 | 0.423027 |
| └ drawBgaBackplate | 0.345766 | 0.287630 |
| prepareAppliedSkinGpu | 0.103324 | 0.076469 |
| └ 표시 데이터 snapshot | 0.002082 | 0.002094 |
| CompositionPresenter::prepare | 0.000175 | 0.000171 |
| ResourceStats / 최종 GdiFlush | 0.000068 / 0.000755 | 0.000059 / 0.000676 |
| CompositionPresenter::present 전체 | 0.7267 | 1.2436 |
| └ uploadHybrid | 0.534403 | 1.050741 |
| 　 └ 배경 업로드 전체 | 0.439889 | 0.967448 |
| 　 　 └ memcmp 행 비교 | 0.072226 | 0.149940 |
| 　 　 └ 배경 CopyFromMemory | 0.153534 | 0.374414 |
| 　 　 └ uploadedPixels_ 소유권 대입 | 0.214019 | 0.442909 |
| 　 └ 오버레이 준비 | 0.000038 | 0.000038 |
| 　 └ 기어 / 좌하단 / 우상단 영역 업로드 | 0.076643 / 0.006032 / 0.011590 | 0.066877 / 0.005543 / 0.010619 |
| └ CompositionDraw(업로드 제외) | 0.143289 | 0.141792 |
| 　 └ D2D BeginDraw / EndDraw | 0.001219 / 0.114472 | 0.001217 / 0.115440 |
| └ D3D CopyResource 호출 | 0.004664 | 0.004957 |
| └ swapchain GetBuffer | 0.000692 | 0.000744 |
| └ GPU query 수집(GetData, DONOTFLUSH) | 0.000541 | 0.000169 |
| └ Present 호출 | 0.033233 | 0.033567 |

NVAPI 개별 마커의 ON 느린 CPU 1% 행 평균은 각 0.0002–0.0034ms 수준이다. `request`는 0.000031ms, Pacer OS wait는 ON에서 0이다. 느린 **interval** 1%를 직전 CPU에 정렬하면 ON 평균 interval=1.8044ms, 직전 CPU=1.7909ms, 현재 Sleep=0.003625ms, 현재 input pump=0.007679ms다. 따라서 이번 tail을 이중 Sleep으로 설명할 근거가 없다. 같은 CPU-tail 행의 Sleep 평균(약 0.413ms)을 CPU 안의 비용으로 더하는 오류를 피했다.

기존 CompositionDraw 계측은 uploadHybrid가 끝난 **뒤** 시작했다. 따라서 이전 보고서에서 D2D 명령 제출이 작게 보여도 앞선 배경 업로드 비용이 사라진 것은 아니었다.

UI thread cycles는 attributed ON 전체 평균 약 2.066M, CPU tail 평균 약 7.973M이었다. 대부분이 순수한 스케줄링 휴면이라고 보기 어려운 보조 근거지만, 클록/메모리 정체/커널 비용을 분리하지 못한다. cycle 수를 시간 단위로 변환하지 않았다.

## 큰 프레임과 주변 프레임의 실제 경로

attributed ON의 CPU 최악 serial 3662:

| serial | CPU 시간 | 현재 interval | uploadHybrid | 배경 비교 | 배경 복사 | 소유권 대입 |
|---|---:|---:|---:|---:|---:|---:|
| 3660 | 0.3232 | 0.7992 | 0 | 0 | 0 | 0 |
| 3661 | 0.4582 | 0.7993 | 0 | 0 | 0 | 0 |
| **3662** | **2.4821** | 0.8003 | **1.9104** | 0.1585 | **1.2043** | **0.4667** |
| 3663 | 0.5036 | **2.4855** | 0 | 0 | 0 | 0 |
| 3664 | 0.3001 | 0.7990 | 0 | 0 | 0 | 0 |

3662의 span: UpdateWindow 2.4781 → WM_PAINT 2.4728 → presenter 2.1012 → uploadHybrid 1.9104 → 배경 1.8298ms다. 별도 하위 경로는 drawGameScreen 0.3505, CompositionDraw 0.1505, Present 0.0221ms였다. 입력·일시정지·포커스 손실이 발생한 프레임은 아니다.

위 호출은 정상 플레이의 동기 WM_PAINT 안에서 실행됐으며 다음 프레임 간격에 지연이 드러났다. OFF 최악 CPU serial 4469도 동일한 배경 경로였다(CPU=2.6951ms, uploadHybrid=1.7085ms, 복사=0.6313ms, 소유권 대입=0.6356ms). 즉 이 작업은 ON에서만 존재하는 경로가 아니다. OFF에서는 시작 전 대기/폴링과 다음 due 시각의 관계 때문에 interval tail과 CPU tail의 모양이 달라진다. 이번 분석을 이유로 이전 pacer를 다시 바꾸지 않았다.

소스에서 `uploadedPixels_`는 이전 업로드 프레임을 비교용으로 보유하는 shared_ptr다. 현재 프레임으로 교체하면 이전 owner의 참조 감소와 마지막 참조일 경우 해제가 이 줄에서 수행될 수 있다. `Main::drawBgaBackplate`는 비디오 `Frame::pixels`를 aliasing shared_ptr로 넘기고, `AsyncVideoBga::Frame`은 vector<uint32_t>를 가진다. 1920×1080이면 픽셀 저장 공간만 약 7.91MiB다. **대입 scope의 비용은 측정했지만 destructor/allocator별 스택은 수집하지 않았다.** 따라서 ‘매번 이 크기를 UI 스레드에서 해제했다’는 최종 확정은 하지 않는다.

배경 복사 프레임의 CPU 평균은 OFF=0.7584ms, ON=1.5895ms였다. 갱신 없는 프레임은 OFF=0.3951ms, ON=0.4261ms다. 비교한 행은 평균 약 450행이었다. 이 카운터는 바이트가 아니라 비교 행 수이며 복사 카운터는 API 호출 수다. 초기 탐색 CSV의 `backdrop_compare_bytes/backdrop_copy_bytes` 명칭은 실제로 행/호출을 센 잘못된 이름이므로 바이트 분석에 쓰지 않았다. 최신 계측에서 이름을 바로잡았다.

## 포커스·일시정지의 별도 큰 지연

초기 탐색 ON에 약 42.6ms interval, input pump 약 40.1ms가 있었다. 당시 포커스 플래그가 없었고 일부 span이 넘쳐 정확한 전체 원인 확정에는 부족했다. 이후 상태 플래그를 추가했고 정상 비교는 포커스를 유지해 재수집했다.

별도의 `--focus-probe` 제어 시험은 8초 ON 실행 중 4초에 **합성 WM_KILLFOCUS 1회**를 보냈다. 실제 OS 포커스를 잃은 시험과는 다르다. paused paint=1, 진단 러너 auto-resume=1, span drop=0을 기록했다.

- serial 4971 interval=24.9205ms, CPU update 자체=0.7499ms.
- input pump=24.1195ms → pausePlayback=2.0744ms, WM_PAINT=20.6213ms → drawGameScreen=20.2073ms, resumePlayback=1.2265ms.
- 현재 SDK Sleep=0.3203ms. 배경 업로드=0.1202ms로 정상 플레이의 배경 복사 tail과 다른 경로다.

이는 일시정지 UI 첫 그리기가 메시지 펌프 안에서 긴 프레임 간격을 만들 수 있음을 재현한다. 초기 42ms 전부가 동일 원인이었다거나 일반 플레이에서도 동일 빈도로 발생한다는 증명은 아니다. pause UI 내부의 더 작은 함수는 이번 scope로 분리하지 않았다. 진단 러너의 자동 resume는 원래 있던 동작이며 본 게임의 자동 재개 기능을 추가한 것이 아니다.

## 관련 코드와 다음 수정 우선순위

| 우선순위 | 함수/파일 | 다음 작업과 최소 수정 후보 |
|---|---|---|
| 1 | `CompositionPresenter::uploadHybrid`, `src/CompositionPresenter.h` 배경 소유권 대입(현재 작업 트리 약 411행) | allocator/해제 스택을 ETW/WPA로 확인한다. 마지막 owner 해제가 확인되면 디코더 측의 **개수 제한된** 재사용 버퍼 또는 worker 반환 큐를 검토한다. 무제한 보류·dangling pointer·렌더 중 강제 해제는 금지. 이번에는 동작 수정 없음. |
| 2 | 같은 함수의 CopyFromMemory(약 407행), memcmp(약 399행) | GPUView/ETW로 호출 내부 CPU 복사/드라이버 대기를 분리하고 변경 영역·업로드 시점·버퍼 수를 검토한다. GPU 재사용 완료를 보장하는 작은 실험부터 A/B 측정한다. 복사 삭제나 BGA 갱신 생략을 바로 적용하지 않는다. |
| 3 | `drawGameScreen`, `Main.cpp`; `WM_KILLFOCUS`→`pausePlayback`→`WM_PAINT` | 수십 ms 현상의 별도 대상. pause 메뉴의 최초 텍스트/이미지 준비를 더 세분화한 뒤 진입 전 준비 후보를 검증한다. 정상 플레이 tail과 구별한다. |
| 4 | `drawBgaBackplate`(Main.cpp 약 2755행), `prepareAppliedSkinGpu`(SkinApplicationHost.inl 약 192행) | 배경 CPU 조합과 스킨 준비가 다음 큰 구간이다. 현재 수치만으로 BGA/판정 계산을 변경하지 않는다. |
| 낮음 | `LowLatency::emit/request`, `CompositionPresenter::collectGpuTiming/GetBuffer/Present` | 이번 tail에서 작은 호출 시간이었다. Reflex SDK와 FramePacer를 다시 변경할 근거 없음. |

그 외 위치: `updatePlaybackFrame` 약 2176행, `pressLane/releaseLaneInput` 약 2084/2134행, `WM_PAINT` 약 5648행, 메인 입력 펌프 약 6065행, `AsyncVideoBga::Frame` 15행·worker 프레임 생성 57행, `hybridCpuRegions`(HybridFrame.h 13행). 작업 트리의 기존 변경 때문에 행 번호는 이동할 수 있으므로 함수명을 기준으로 찾는다.

## 변경 파일·검증·재현

- `src/HudFrameProbe.h`: CPU stage/counter, 고정 용량 span, thread cycles, 종료 후 주변 프레임 저장. 일반 빌드에서는 모두 no-op.
- `src/Main.cpp`: 읽기 전용 scope/상태 카운터만 추가. 컨트롤러 처리, BGA/오디오/판정 규칙 변경 없음.
- `src/CompositionPresenter.h`, `src/LowLatency.h`, `src/SkinApplicationHost.inl`: 기존 호출 주위 진단 scope만 추가.
- `tests/HudFrameProfile.cpp`: 계측 전 창 포커스 요청, 상태 카운터, 명시적으로 선택하는 합성 focus 시험. 테스트 러너에만 적용.
- `tools/Run-HudFrameStudy.py`: `--focus-probe`와 manifest 기록.
- `tools/AnalyzeReflexCpu.py`: P99/P99.9/max/임계 빈도, 직전 프레임 정렬, 상태 필터, 느린 호출 경로 및 앞뒤 2프레임 분석.

Release 진단 타깃 `BmsHudFrameProfile` 빌드 성공, 실제 GPU 실행 성공. Release `BmsLowLatencyTests`, `BmsFrameTests` 빌드 및 CTest **2/2 통과**. 전체 게임/에디터 회귀나 수시간 플레이는 수행하지 않았다. 기본 배포 `Release/BmsPlayer.exe`는 이번 계측 작업에서 교체하지 않았다.

raw 결과는 커밋하지 않은 `build-rmlui-song-list/Validation/HudFrameStudy/`의 각 prefix `.manifest.json/.meta.json/.frames.csv/.spans.csv/.gpu.csv/.submits.csv/.log`다. 대표 prefix는 `cpu-final-r1-off`, `cpu-final-r1-on`, `cpu-final-r2-on`, `cpu-final-r2-off`, `cpu-final-boundary-off/on`, `cpu-attributed-off/on`, `cpu-focus-control`이며 모두 `-original-11-r1`이 이어진다. 분석 집계는 `cpu-attribution-summary.json`이다.

재현 예시(프로젝트 루트, 새 label 사용, Python 경로는 로컬 환경에 맞춘다):

```powershell
cmake --build build-rmlui-song-list --config Release --target BmsHudFrameProfile
python tools/Run-HudFrameStudy.py --label cpu-repeat-off --asset original --cases 11 --duration 20 --repeats 2 --fps 0
python tools/Run-HudFrameStudy.py --label cpu-repeat-on --asset original --cases 11 --duration 20 --repeats 2 --fps 0 --reflex
python tools/AnalyzeReflexCpu.py --labels cpu-repeat
```

실행 중 다른 창 조작을 피하고 paused/unfocused/auto-resume/drop 상태를 확인한다. 상세 scope 대조는 새 label에 `--boundary`를 붙인다. 합성 pause 경로는 `--reflex --focus-probe --duration 8 --repeats 1`로 별도 실행한다. 기존 결과 label은 덮어쓰지 않도록 도구가 거부한다.

## 남은 불확실성

이번 계측은 CPU 호출 경과 시간이다. 기존 비동기 GPU timestamp bracket은 전체 렌더 작업 범위이며 세분화한 각 CPU scope의 GPU 시간을 의미하지 않는다. ETW/WPA/GPUView의 드라이버 내부·힙 해제·OS 스케줄링 스택, 실제 scanout/입력-광자 지연은 측정하지 않았다. 비디오 디코딩 worker의 함수별 시간도 계측하지 않았으므로 UI의 AsyncVideoBga::update가 짧다는 사실만으로 worker와의 메모리·CPU 경쟁을 배제할 수 없다. Reflex의 지연 감소 효과 자체를 증명하거나 반박하지 않는다.

고정 span 버퍼 때문에 진단 프로세스의 peak working set이 약 574–590MiB로 늘었다(이전 경계 계측보다 약 200MiB 증가). 경계 모드도 같은 버퍼를 할당하므로 scope 호출 부담 비교이지 메모리 계측 부담을 없앤 시험이 아니다. 새 소유권 scope를 추가한 마지막 쌍의 성능 변동 원인도 별도 통제가 필요하다. 단일 GPU·짧은 채보 구간·자동 메시지 입력 시험이므로 다른 BGA/해상도/실제 수동 입력과 장시간 재현으로 확대해야 한다. **최적화 전후 성능 개선 수치는 없다. 이번 결과는 병목 위치 확인과 다음 수정 순서를 정하기 위한 것이다.**

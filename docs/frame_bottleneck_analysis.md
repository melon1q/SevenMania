# 프레임 안정성 1차 병목 분석 (2026-10-24)

## 범위와 보존

메인 `21ff0ef`는 변경하지 않는다. 사용자 작업 브랜치 `테스트`의 `687a29c`와 현재 24ms HUD 색 전환을 기준으로 `codex/frame-study-instrumentation`에서 계측한다. 기존 staged CI/README/UI 변경은 별도 보존하며 실험 커밋에서 제외한다. 원본 index/worktree 패치는 무시되는 `build-frame-study/`에 저장했다. 이 보고서는 첫 탐색 1회 결과이며 최종 비교는 `frame_optimization_experiments.md`에 기록한다.

## 측정 조건 및 정의

- 실제 Win32 메시지 루프, WASAPI 키음, BGA, 판정/이펙트 및 GPU Present를 실행한다. 1920×1080, DirectComposition OFF, 무제한(1500 상한), 자원 제한 100%, mimalloc/oneTBB/hybrid ON, Tracy OFF, Reflex OFF. 테스트 빌드의 프레임 통계/알림 비프도 유지한다.
- Creamy Sugary OVERDRIVE!!! Another 및 ha＠ppy s＠mmer v＠c＠tion!! Normal, 파서 seed `0x20261024`. 30초 seek 후 10초 실행 중 31~39초의 8초만 집계한다. 짧은 구간이므로 장시간/온도/메모리 변동으로 일반화할 수 없다.
- 각 노트의 KEYDOWN과 KEYUP을 Win32 PostMessage로 전달한다. 원래 판정/키음 경로를 통과한다. 10ms WM_TIMER의 실제 스케줄 지터를 포함하므로 이것은 물리 입력 지연 측정이 아니다. idle은 입력 없는 비교이며 MISS/POOR 효과도 달라져 순수 입력 비용 분리는 불가능하다.
- 평균 FPS = 1000/평균 시작 간격. 1% Low = 가장 긴 ceil(N×0.01) 간격의 평균의 역수. 백분위는 정렬 후 선형 보간, 표준편차는 모집단 기준. 스파이크는 >2ms 및 >중앙값×2를 각각 기록한다. 고정 FPS에서는 >2ms 자체가 성능 문제를 뜻하지 않는다.
- interval_ms는 연속 update 시작 간격, frame_ms는 update~paint 종료 비용이다. 입력/대기는 update 이전에 pending으로 수집한다. 현재 텍스트 처리 비용은 다음 interval에 주로 반영되므로 현재 프레임 상관은 processing 기준을 함께 본다. 부모/자식 scope는 중첩되어 합산하면 안 된다.

## 초기 탐색 실측 (각 1회; 최종 승자 선정용 아님)

| 시나리오 | 평균 FPS | 1% Low | 평균 ms | P95 ms | P99 ms | P99.9 ms | 표준편차 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| creamy-cpu-replay | 927.6 | 300.5 | 1.078 | 2.096 | 2.789 | 3.883 | 0.456 |
| creamy-gpu-idle | 1427.6 | 643.6 | 0.700 | 0.895 | 1.217 | 1.891 | 0.121 |
| creamy-gpu-replay | 1305.8 | 383.5 | 0.766 | 1.286 | 2.268 | 3.010 | 0.316 |
| happy-gpu-replay | 1378.5 | 431.5 | 0.725 | 0.999 | 1.883 | 2.981 | 0.221 |

## 가능성이 높은 가설 및 독립 실험

| 우선 | 근거/의심 위치 | 재현·검증 | 예상 효과(수치 미확정) |
|---|---|---|---|
| 1 | `src/Main.cpp::drawAnimatedCenteredText` → `src/TextRasterCache.h::draw`. 색상·콤보별 캐시 miss. already-outlined HUD도 outlined=false 상태에서 outline 배열 생성과 전체 후처리 순회를 수행한다. Creamy GPU text_ms P99 1.529ms, miss와 processing Pearson r=0.828. | 같은 replay로 outlined=false의 수학적으로 항등인 후처리만 건너뛴다. frozen 원본과 모든 출력 픽셀 비교. | cache miss 프레임의 CPU 꼬리 감소. GDI 자체 비용이 우세하면 효과 작을 수 있음. |
| 2 | 같은 함수의 miss마다 CreateCompatibleDC×3 / CreateDIBSection×2 / Delete. 콤보 증가 및 24ms 색 전환 때문에 반복된다. | E1과 별도 브랜치에서 측정 DC와 동일 크기 DIB만 재사용. 렌더/알파 복원·캐시 키는 유지하고 픽셀 비교. | OS GDI 객체 생성/해제 변동 감소. 서로 다른 글자 폭에서는 DIB 재생성이 필요해 제한적일 수 있음. |
| 3 | `src/KeyDeck.h::drawCached`: scratch 회전만 바뀌어도 모든 키 영역을 재생성. `Main.cpp::drawGame` deck scope. | 실제 deck P99 비용 및 idle/replay 비교 후 필요시 별도 회전 전용 부분 갱신 실험. | 작은 주기적 CPU 비용 감소 가능; 글자 비용보다 우선도 낮음. |
| 4 | `Main.cpp::drawBgaBackplate`, `CompositionPresenter::present/drawFrame`: 30Hz BGA backplate 복사·업로드 및 모니터/GPU 제출 대기. | bga_rebuilt·upload/gpu_queue/present 및 출력 제출 카운터와 스파이크 비교. CPU/GPU 및 240/VSync/Reflex 시나리오. | API/복사 비용은 존재하나 첫 결과에서 판정 텍스트보다 작은 꼬리. GPU 실행 시각별 인과관계는 별도 ETW 필요. |
| 5 | `Main.cpp::pressLane`, `showTimingJudgement`; `AudioEngine::trigger/fillBuffer`. 판정/키음 큐 확장 또는 락 경합. | pending 입력 시간·trigger_lock과 audio thread mix/lock 원자 카운터 비교. 기존 판정/LN/오디오 테스트. | 계측으로 큰 경합이 확인될 때만 변경한다. 판정/음악 시계 변경 금지. |
| 6 | 동적 할당, 시스템 자원 샘플링·FPS 통계·진단 비프 큐, OS 스케줄링, FramePacer/Reflex 대기. | C++ new 횟수·바이트/자원 scope·wait scope와 이벤트/스파이크 간격 분석. 상세/경계-only 계측 대조. | 평균보다 반복 꼬리/우발 지연 원인 구분. 외부 DPC/드라이버는 이 계측만으로 확정 불가. |

노트 밀도와 processing r=0.010(초기 Creamy GPU)로 이 구간의 주원인이라는 근거는 약하다. 입력 발생 프레임이 느린 관측은 있지만 같은 입력이 새 콤보 글자/효과도 유발하므로 판정 계산 자체의 병목으로 단정하지 않는다. 모든 가설을 효과가 있다고 가정해 동시에 적용하지 않는다.

기존 `judgement_ms`는 이름과 달리 데크·판정 글자 등을 포함한 **화면 그리기 부모 구간**이다. 판정 알고리즘 비용은 새 `judge_logic_ms`로 분리했다. 두 값을 혼동해서 “판정 계산에 1.7ms가 든다”고 해석하면 안 된다. 입력/판정은 희소하므로 모든 프레임 기준 P99가 0일 수 있으며, 분석 JSON에는 0이 아닌 실행의 별도 분포도 저장한다.

Win32 입력의 무효화 요청이나 출력 준비 신호에 의해 정규 업데이트 밖에서도 `WM_PAINT`가 실행될 수 있다. 그 구간의 타이머는 pending으로 다음 행에 합쳐진다. `interval_ms`는 그 사이의 전체 벽시계 시간을 포함하지만 `frame_ms`는 해당 업데이트 시작부터 그 업데이트의 paint 종료까지만 잰다. 따라서 pending 그리기 비용은 `frame_ms`에 포함되지 않을 수 있다. 특히 VSync에서 업데이트 FPS를 그리기 FPS로 해석하면 안 된다. 정규 루프 밖의 중복 그리기 억제는 추가 검토 후보지만, 입력 직후 시각 반응을 늦출 수 있어 이번 추천 변경에는 포함하지 않는다.

## 계측 경로와 한계

### 추가 발견: GDI 동기화의 긴 꼬리

초기 outline 실험 1회에서 74.945ms 시작 간격이 기록됐다. 직전 프레임은 총 65.060ms, 그중 `Main.cpp::drawGameScreen`의 노트 그리기 전 `GdiFlush`(`sync_ms`)가 54.183ms였다. 이 실행의 낮은 1%와 표준편차를 집계에서 빼지 않는다. 함수 내부에서 대기했음은 확인되지만 드라이버 대기와 OS 선점을 이 타이머만으로 분리할 수 없다.

이에 `codex/frame-exp-hud-raster`에서 판정선과 레인 구분선 두 종류의 `FillRect`만 기존 CPU `fillGameRect` 경로로 바꾸는 독립 실험을 추가했다. BGA/빔 이전 장벽과 노트 이전 장벽은 유지한다. 출력 순서와 버퍼 소유권을 보존한 채 해당 장벽으로 전달되는 GDI 작업을 줄이는 실험이다. 18개 클리핑·색상 조합의 전체 픽셀 비교 및 기존 GPU Display 테스트가 통과했다. 반복 측정에서 긴 꼬리가 줄어드는지는 최종 보고서에서 판정하며, 드문 한 번의 사건이 사라졌다는 이유만으로 완치했다고 주장하지 않는다.

### 환경 확인

장비에서 AMD Ryzen 7 9800X3D(논리 프로세서 16개), NVIDIA GeForce GTX 1660 Ti(드라이버 `32.0.16.1714`)와 AMD Radeon 내장 GPU(`32.0.21045.16010`)가 조회됐다. 이는 설치 장치 목록이며 실행마다 선택한 DXGI 어댑터와 실제 모니터 scanout 시각을 기록한 것은 아니다. MSVC/Visual Studio 18 2026 x64 Release(`/O2`, `/Ob2`)로 비교했다. 노트 배속은 설정의 quarter-step 20, 즉 5.00x다.

기존 빌드 디렉터리 외에 같은 기준 커밋 `2af1307`의 깨끗한 worktree도 별도로 빌드해 비교한다. 첫 clean 기준 GPU replay 측정은 평균 1259.4FPS, P99 2.359ms, P99.9 3.290ms로 기존 기준 반복 범위와 비슷했다. 파일/빌드 캐시가 원인이라는 가설을 지지할 근거는 현재 부족하다.

- `PerformanceTrace.h::Trace`: 최대 65,536 프레임/32,768 이벤트를 사전 reserve한 메모리 버퍼에 저장 후 종료시에만 CSV 기록. 넘으면 dropped 기록. 10초×1500FPS 범위 내. 상세 타이머 OFF 대조 환경변수 `BMS_STUDY_BOUNDARY_ONLY=1` 제공.
- `Main.cpp::pressLane/WM_KEYUP/showTimingJudgement`: 입력 down/up, 판정, authored 입력 시점 이벤트. `waitForPlaybackWork`와 저지연 beginFrame 대기를 별도 기록. 기존 BGA/노트/데크/빔/피드백/텍스트/Present scope 유지.
- `ExperimentalAllocator.cpp`: mimalloc을 통과하는 일반 C++ new/free의 TLS 카운트. UI thread만 프레임에 귀속한다. aligned/CRT/COM/GDI/드라이버 할당과 실제 allocator stall은 미측정이다. allocation 상관관계는 원인 확정이 아니다.
- `AudioEngine.cpp`: trigger 락 획득 대기 및 worker mix/trigger 락 시간을 atomic 누적 후 UI에서 회수. worker 구간은 frame 경계를 가로지를 수 있다. deviceMutex 전체 경합/오디오 하드웨어 출력 시각은 미측정이다.
- `CompositionPresenter`: 기존 누적 GPU 시간/업로드/제출 수 유지. scope는 CPU API 시간이며 GPU 실행 시간이나 실제 모니터 표시 시점과 동일하지 않다. PresentMon/ETW/고속카메라/loopback 없는 현재 실험은 출력 간격·물리 입력 지연·AV sync의 완전 검증을 제공하지 않는다.
- 시작/종료/프로세스 메모리 조회와 stdout은 측정 구간 밖. 비프/상시 프레임 통계는 현재 사용자 테스트 버전과 동일하게 남겨두며 별도 승인 후 최종 적용에서 제거한다.

## 재현 절차

1. `cmake --build build-performance-hybrid --config Release --target BmsGameplayProfile BmsFrameStudyRasterTests --parallel 4`
2. `./tools/Run-FrameStudy.ps1 -Profiler ./build-performance-hybrid/Release/BmsGameplayProfile.exe -Label base -Repeat 1 -SourceCommit <실제 SHA>` (2,3회 반복).
3. `python tools/Analyze-FrameStudy.py build-frame-study/results --output build-frame-study/summary.json`
4. 빌드/테스트와 성능 측정은 동시 실행하지 않는다. 각 run manifest에 exe/chart/config SHA256, seed, UTC, commit을 보관한다. 테스트 곡은 개인 파일이므로 커밋하지 않는다. 다른 PC는 동일 파일 hash 확인 후 실행한다.
5. 각 실험은 기준 SHA에서 생성한 브랜치/worktree에서 최소 변경 후 동일 검증/반복 실행. 최종 보고서에서 채택/기각/불확실을 구분한다. main 병합 및 실제 배포 실행파일 교체는 하지 않는다.

## 실제 설정 확인 후 추가 검증

초기 표는 기본 통제 설정(5.00x/버퍼256/자원표시OFF/CPU성능모드OFF/ReflexOFF)이다. 사용 중인 테스트 INI는 5.50x/버퍼32/자원표시ON/CPU성능모드ON/ReflexON/화면모드2로 달랐다. 관련 항목만 복제해 user-* 및 paired-* 추가 대조를 수행했다. 원본 INI 해시가 그대로임을 확인했다. 자원 표시를 켠 원본 overlay 관측 최대는 1.937ms였고, 단순히 표시를 끄는 해결책은 채택하지 않았다. 실제 조건에서도 글자 생성의 CPU 꼬리 개선은 재현됐지만 입력·API 제출 지표가 모든 대조에서 일관되게 좋아지지는 않았다. 최종 적용은 보류하며 자세한 값은 frame_optimization_experiments.md에 기록했다.

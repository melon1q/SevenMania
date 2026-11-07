# 3단계: 비동기 곡 로딩 분산 (2026-11-05)

## 새 흐름과 작업 분류

곡 선택 → 기존 gLoadWorker에서 전체 채보 파싱 → 불변 채보 게시 → UI에서 판정/레인 인덱스 및 최소 준비 화면 생성(PreparingPlay) → worker의 영상 초기 준비·키음 PCM·재생속도 준비와 이미지 BGA 큐 병행 → UI에서 플레이용 GPU 타깃/스킨 준비 → 시작 장벽 → ReadyToStart → 기존 자동 시작 → Playing.

| 작업 | PreparingPlay 전/후 | 실행 및 소유권 |
|---|---|---|
| BMS 전체 파싱·지원 모드 검사 | 전 | 기존 gLoadWorker |
| 판정 배열·점수·게이지·레인 인덱스 | 전 | UI, 기존 초기화 재사용 |
| 최소 스킨 화면 | 최초 준비 프레임 | UI, 기존 CPU 스킨 캐시 재사용 |
| 전체 키음 PCM 및 bmson 가공 | 후에도 계속 | 기존 gLoadWorker가 AudioEngine 준비 자원 독점 |
| 음원 재생속도 변환 | 후에도 계속 | 같은 worker, 키음 준비 이후 |
| 영상 초기 준비 | 후에도 계속 | 기존 BgaVideoLayers/AsyncVideoBga 스레드·선행 프레임 정책 유지 |
| 이미지 초기 디코딩 | 후에도 계속 | 기존 AsyncBgaRaster 큐·worker 유지 |
| 첫 플레이용 GPU 준비 | 최소 화면을 한 번 그린 다음 | UI, 기존 prepareSkinForPlayback/prepareGpu 재사용 |

스레드를 새 종류로 추가하지 않았다. 준비 화면은 완성된 조절 UI가 아니라 멈춘 플레이 외형과 준비 안내를 보여준다. 준비 중 배경 영상 scheduler에는 접근하지 않는다. 스킨이 있으면 스킨만 표시하여 기존 CPU 기어가 중복되지 않게 했다. 스킨이 없으면 기존 네이티브 플레이 렌더러를 고정 playhead로 사용한다.

정적 준비 화면은 캐시하고, 배속·레인가드·판정선·기어 위치·해상도 변경 시 다시 그린다. 스킨 애니메이션은 최대 약 30Hz로 재평가하여 로딩 worker와의 불필요한 CPU 경쟁을 줄인다. 실제 Playing의 렌더링/FramePacer/Reflex 알고리즘은 변경하지 않았다.

## 소유권과 안전성

- 한 번에 로딩 worker 하나만 사용한다. 새 요청은 이전 worker가 종료·join된 뒤 시작한다.
- 요청마다 uintptr_t generation을 증가시킨다. 채보 게시/완료 메시지는 generation을 전달하며 UI는 현재 세대, 선택 인덱스, 실제 경로를 확인한다. 오래된 메시지는 현재 worker를 join하거나 결과를 적용하지 않는다.
- 파싱 결과는 shared_ptr<const Chart>로 게시한다. UI는 잠금 아래 참조를 얻어 표시·판정용 채보를 복사하고 이후 worker의 불변 채보를 수정하지 않는다. PCM은 복사하지 않는다. 작업 완료 후 원본 채보 참조를 해제한다. 순간적으로 전체 채보 두 사본이 존재할 수 있다.
- worker는 더 이상 gLoadResult.chart/error를 수정하지 않는다. 오디오/BGA 결과와 타이밍은 worker 전용 필드에 쓰며 release/acquire 완료 플래그 이후 UI가 읽는다.
- 준비 화면은 worker가 쓰는 AudioEngine 샘플·duration·출력 상태와 영상 scheduler를 조회/초기화하지 않는다. 실제 노트 입력·자동 POOR는 gPlaying=false로 차단된다. UI 효과음 요청은 기존 triggerMutex 경로이고 준비 중 효과음 출력은 중지되어 있다.
- 시작 장벽은 worker 완료/회수, 판정·채보 준비, CPU 준비 화면 1회 출력, GPU 준비 시도 및 기존 폴백, 초기 이미지 요청 완료를 모두 확인한다. 성공/실패 처리된 기존 부분 로딩 정책은 유지한다.
- ESC 취소는 토큰만 설정하고 UI는 메시지를 계속 처리한다. 무거운 디코더를 메인에서 즉시 join하지 않는다. worker 완료가 확인되면 join하고 자원을 정리한 뒤 선곡 화면으로 돌아간다. 취소 처리 중 새 곡 실행은 이전 자원 회수까지 대기한다.
- worker 완료 후 준비 상태에서 취소할 때 대기 상태에 남던 경로를 자동 테스트로 발견해 수정했다.
- 종료는 취소 → worker join → 미디어 해제 순서를 유지한다. 종료 시 진행 중인 OS/코덱 호출을 강제 중단하지 않으므로 즉시 종료 시간을 보장하지 않는다.
- 초기 준비 실패는 완료/실패 결과로 끝내며 이미지 실패는 기존 안전 폴백 정책을 유지한다. OS/코덱 자체가 반환하지 않는 상황에 대한 강제 종료 기능은 없다.

## 변경 파일과 핵심 함수

- src/PlaybackLoadJob.h: 세대, 불변 채보 게시, 완료 상태, 준비 장벽 플래그, 고정 크기 계측 버퍼.
- src/PlaybackLoadWorker.inl: beginPlaybackLoadWorker. 파싱 직후 게시하고 기존 영상→오디오→속도 변환 순서로 계속 준비.
- src/PlaybackLoadRuntime.inl: acceptPlaybackChart / completePlaybackWorker / pollPreparingPlayback / drawPreparingPlayback / reportPlaybackLoad.
- src/PlaybackLoading.inl: prepareSelectedChartData(deferred), startPreparedChart의 시작 장벽.
- src/AsyncBgaRaster.h: begin(비동기 시작), initialPrepared(펌프/완료 조회), 기존 start(동기 호환 래퍼).
- src/SkinApplicationHost.inl: 준비용 스킨 경로와 기어 앵커 허용. 선곡 미디어 접근 우회.
- src/Main.cpp: 메시지·화면·상위 루프 연결, 준비 중 BGA/스킨 접근 경계. 6324줄 → 6305줄, 순감소 19줄.
- tests/AsyncPlaybackIntegration.inl: 실제 worker/준비 화면/오디오 시작/취소/세대 검증 및 합성 자료.
- tests/CompatibilityPlaybackTests.cpp, tests/SkinBindingTests.cpp: 통합 테스트 연결.

## 진단 사용법

PowerShell에서 환경변수 `SEVENMANIA_LOAD_TRACE=1`을 설정한 프로세스로 게임을 실행하면 실행 파일 옆 SevenMania-loading.csv에 결과를 누적한다. 1 대신 전체 CSV 경로도 지정할 수 있다. `SEVENMANIA_LOAD_SERIAL=1`은 동일 구현에서 파싱 결과의 UI 반영을 전체 worker 완료까지 미루는 진단 대조 모드다. 평소에는 두 변수를 해제한다.

CSV: Preparing/Ready/Playing 전환 시간, 파싱, 키음 준비(가공 포함), 속도 변환, 영상 준비, 이미지 초기 준비 완료를 확인하기까지의 경과 시간, GPU 준비 호출의 CPU 경과 시간, 준비 화면 간격 P95/P99/최대, Working Set/Private Usage 샘플 최대, PCM 바이트, GPU 폴백, 실제 재렌더 횟수/최대 CPU 시간.

핫패스 파일 로그는 없다. 프레임 간격은 사전 할당한 4096개까지 저장하고 종료 시 집계한다. 메모리는 초기/완료/준비 프레임 16회마다 샘플링하므로 정확한 전 구간 최대나 시간 평균이 아니다. GPU 준비 값은 CPU 호출 시간이며 GPU 실행 시간 계측이 아니다. Preparing 전환 시각은 상태 진입이고 모니터에 실제로 제시된 시각은 아니다. 정적인 준비 화면도 Windows paint에는 응답하지만 실제 장면 재렌더는 캐시로 생략한다.

## 실측: 합성 통합 테스트

Release, 같은 프로세스·같은 합성 파일·1280급 숨겨진 테스트 창에서 실제 WASAPI 출력 시작까지 실행했다. GPU 없는 native 통합 경로와 기본 스킨/GPU 경로는 구분한다. 실제 사용자의 곡 모음이나 과거 2단계 바이너리를 측정한 결과가 아니다.

| 채보 (비동기, 1회) | Preparing ms | Ready ms | Playing ms |
|---|---:|---:|---:|
| 일반 | 11.85 | 27.17 | 55.69 |
| 이미지 BGA | 5.98 | 21.67 | 47.81 |
| 동영상 BGA | 9.33 | 24.80 | 51.21 |
| 약 6만 노트 | 35.85 | 50.36 | 78.33 |

96개 서로 다른 음원 파일(PCM 18,431,616바이트)은 최초 측정을 별도로 두고 직렬/비동기 순서를 번갈아 추가 2회씩 측정했다.

| 96키음, 반복 2회 평균 | 비동기 | 직렬 대조 |
|---|---:|---:|
| Preparing ms | 11.29 | 240.71 |
| Ready ms | 247.53 | 256.47 |
| Playing ms | 275.37 | 285.26 |
| 키음 준비 ms | 234.79 | 230.53 |
| Working Set 샘플 최대의 평균 MiB | 101.17 | 103.12 |
| Private Usage 샘플 최대의 평균 MiB | 79.96 | 82.35 |

비동기 두 실행의 준비 화면 간격 P99는 16.02/16.10ms였고 표본은 14/16개다. 정적 장면은 각각 한 번 렌더링했고 해당 CPU 시간은 0.58/0.69ms였다. 표본이 작고 테스트 루프 대기 간격이 포함되므로 인게임 FPS 개선이나 통계적 우위를 뜻하지 않는다.

첫 96키음 실행은 비동기에서 키음 준비 489.75ms/Playing 521.75ms, 직렬에서 245.47ms/297.88ms였다. 반복 실행에서는 차이가 줄었다. 캐시·초기화·측정 순서의 영향을 배제하지 못했으므로 첫 실행 차이의 원인을 단정하지 않는다. 전체 곡 시작이 항상 빨라진다고 주장하지 않는다. 확인한 핵심은 키음 디코딩이 진행 중일 때 준비 화면을 표시한다는 점이다.

기본 스킨+GPU 별도 검사: Preparing 11.11ms, Ready 100.63ms, Playing 130.20ms, CPU 준비 화면 첫 렌더 56.54ms, GPU 준비 호출 27.93ms. 첫 스킨 렌더와 GPU 업로드는 아직 동기 배치라 긴 초기 프레임이 남는다. 이를 숨기거나 더 빠르다고 보고하지 않는다. 이번에는 초기 데이터/CPU 화면/GPU 준비를 별도 시점으로 나눴으며 텍스처별 분할 업로드까지 구현하지 않았다.

평균 300MB 이하라는 과거 관찰을 이번 구조의 효과로 입증하지 않았다. 위 값은 합성 데이터의 샘플 최대이고, 원래 사용자 채보·스킨·캐시 조건 및 2단계 기준 실행의 시간 평균과 동등하지 않다. 스킨 경로의 샘플 최대는 Working Set 약 117.77MiB / Private Usage 약 132.20MiB였다. 메모리 차이를 최적화 효과로 단정할 수 없다.

## 빌드 및 회귀 검증

BmsPlayer.exe를 기존 build-rmlui-song-list/Release 경로에 Release로 재빌드했다.
최종 테스트 5/5 통과, 11.05초: BmsCompatibilityTests / BmsCompatibilityPlaybackTests / BmsAudioTests / SevenManiaSkinBindingTests / SevenManiaSkinEditorSmoke.

| 요구 검증 | 수행 범위 |
|---|---|
| 일반/다수 키음/이미지/동영상 | 합성 자료로 실제 로딩 worker와 WASAPI 시작, 기존 BGA 픽셀·영상/키음 믹서 회귀 검사 통과 |
| 3곡 이상 연속 | 여러 합성 곡의 연속 로딩·자동 시작·출력 중지 통과. 3곡 완주를 수동으로 검사한 것은 아님 |
| 준비 도중 ESC | 실제 windowProc WM_KEYDOWN/Escape 경로를 직접 호출, 파싱 전/준비 중 취소와 자원 회수 확인 |
| 취소 후 다른 곡 | 회수 완료 직후 다른 채보 로딩·시작 확인 |
| 같은 곡 재시작 | 실제 restartPlayback, 오디오 실행/스캔 인덱스 초기화/로딩 세대 유지 확인 |
| 대용량 | 약 60,001노트 합성 채보 로딩/시작 확인 |
| 종료 중 정리 | 취소→join→미디어 해제 순서를 함수 수준으로 실행. 실제 창 닫기 전체 경로 수동 검증은 미실행 |
| 세대/실패 | 이전 세대 게시·완료 무시, 지원하지 않는 채보 오류 후 무한 대기 없음 확인 |
| 스킨/판정 정지 | 기본 스킨 준비 화면 실제 렌더 캡처 확인. 준비 중 playhead·점수·오디오 미실행 확인 |

실제 사용자 라이브러리, 장시간 메모리 추이, 인게임 Reflex ON/OFF P99, 사람의 컨트롤러 입력, 장치 분리/코덱 무응답은 이번에 검증하지 않았다.

## 4단계 연결점과 잔여 위험

pollPreparingPlayback(false)는 Ready 상태를 유지한다. 다음 UI는 시작 버튼/자동 시작 정책을 이 경계에 연결하고 배속·레인가드·판정선 값 변경 시 준비 화면 캐시를 갱신하면 된다. 노트 배속과 오디오 재생속도를 구분해야 한다. 후자를 바꾸면 키음 속도 변환 준비를 다시 완료하기 전 시작하면 안 된다.

Preparing 중에는 worker가 가진 오디오·영상 자원을 설정 UI가 직접 재시작/해제하지 않아야 한다. 해상도가 바뀌면 이미지의 기존 크기 작업이 끝난 뒤 새 크기를 요청하고 GPU 준비를 다시 수행한다. 영상 크기는 기존 비동기 요청/캐시 정책을 유지한다. 최초 스킨 CPU 렌더 및 GPU 업로드의 긴 배치는 이후 계측을 바탕으로 제한된 업로드 큐 도입 여부를 판단해야 한다. 현재 GPU 준비를 개별 텍스처 단위로 나누지는 않았다.

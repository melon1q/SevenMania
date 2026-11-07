# 2단계: 곡 로딩 상태와 실제 시작 경계 분리 (2026-11-05)

## 구현

`PlaybackLoadState`는 UI 스레드만 소유한다. 라이브러리 스캔의 `gLoading`과 별개로 곡 실행 수명주기를 표현하고, 기존 `gPlaybackLoadReady`를 대체한다.

`Idle → LoadingEssential → PreparingPlay → ReadyToStart → Playing`
오류는 `LoadFailed`, 취소는 `Cancelled`로 종료한다. 새 곡 요청은 `begin()`으로 이전 완료 상태를 지운다. 정상 종료/곡 선택 복귀는 재생 상태를 정리하며, 오류·취소 상태는 다음 요청까지 보존한다.

- `LoadingEssential`: 기존 worker의 파싱, 영상 초기 준비, 전체 키음 디코딩, 재생속도 준비. 기존 순서를 유지한다.
- `resourcesLoaded()`: worker join 후 WM_LOAD_FINISHED 처리에서 호출한다. 기존 타이틀 페이드 대기도 유지한다.
- `prepareSelectedChartData()`: 채보 확정, 판정·점수·게이지·레인 인덱스, 이미지 BGA 초기 준비, GPU/스킨 준비를 수행한다. gPlaying은 false다.
- `ReadyToStart`: 준비 함수를 통과한 뒤에만 진입한다. 아직 오디오를 시작하지 않는다.
- `startPreparedChart()`: ReadyToStart에서만 AudioEngine::startAt을 호출한다. 성공 후 Playing 및 gPlaying을 활성화하고 기존 FPS 통계·FramePacer 시작 처리를 수행한다.
- `finishSelectedChart()`: 위 단계를 순서대로 연결하는 자동 시작 어댑터다. 다음 단계에서는 ready() 이후 이 자동 호출을 보류할 수 있다.

`PlaybackLoading.inl`에 기존 초기화와 시작 코드를 추출했다. 독립 상태 모델은 `PlaybackLoadState.h`다. Main.cpp의 작업 전 6412줄 → 작업 후 6324줄, 순감소 88줄이다.

준비 중에도 플레이 스킨 스냅샷을 만들 수 있게 했으며, 이를 위해 gPlaying을 임시로 켜지 않는다. 판정·키음 진입점은 기존 gPlaying 검사로 차단된다. 오디오 worker가 시작되는 시점은 기존 AudioEngine::startAt 내부이며, 이번 작업은 디코더·믹서·판정 계산·BGA 최적화·Reflex·FramePacer 알고리즘을 변경하지 않는다. 스킨 준비가 실패해 기존 폴백을 사용하는 정책도 유지한다.

## 변경 파일

- src/PlaybackLoadState.h: 상태 및 허용 전이, 준비 완료 상태.
- src/PlaybackLoading.inl: 기존 플레이 데이터 초기화 추출, 실제 시작, 자동 연결 및 오류 정리.
- src/Main.cpp: worker 완료/페이드/취소/종료 연결, 준비용 스킨 스냅샷 허용.
- tests/CompatibilityPlaybackTests.cpp: 상태 전이, 실제 준비 함수, 준비 중 시간·입력·판정 차단, 취소·반복, WASAPI 시작 및 첫 노트 판정/키음 믹싱 검사.
- tests/SkinBindingTests.cpp: PreparingPlay/ReadyToStart의 Play 스냅샷과 공유 렌더러 출력 검사.

## 검증

Release BmsPlayer.exe를 기존 경로 build-rmlui-song-list/Release/BmsPlayer.exe에 업데이트했다. 관련 테스트도 Release로 빌드했다.
최종 5/5 통과, 총 7.40초:
BmsCompatibilityTests / BmsCompatibilityPlaybackTests / BmsAudioTests / SevenManiaSkinBindingTests / SevenManiaSkinEditorSmoke.

추가 확인 내용:
- 상태 전이 20회 반복, 중복 시작/잘못된 전이/취소 후 완료 차단.
- 실제 준비 함수를 3회 호출하고 각 PreparingPlay/ReadyToStart에서 25ms 대기 후 updatePlaybackFrame/pressLane을 호출: gPlaying=false, 오디오 미실행, playhead·miss 인덱스·점수 불변, 키음 요청 없음.
- 실제 WASAPI 시작 성공 및 오디오 위치 진행 확인 (테스트 로그: Load boundary: real WASAPI start passed).
- 첫 노트는 실제 출력 시작을 확인한 뒤 출력 worker를 중지하고 테스트 시계를 노트 시각에 고정하여 기존 pressLane을 호출했다. 히트·점수·키음 요청 및 믹싱된 PCM의 비영 값을 확인했다. 실제 입력 장치로 사람이 친 첫 노트 검증은 아니다.
- 기존 호환성 테스트의 BMP 레이어, 영상 프레임/재시작/레이어, 입력·판정·게이지·믹서 검사를 함께 통과했다.
- Play 준비 스냅샷을 WARP 공유 렌더러로 렌더링했다.

미실행: 실제 사용자 곡의 전체 UI 조작, 키보드 ESC 메시지를 통한 취소·재선곡, 장시간 플레이, 고FPS/Reflex 프레임 타임 계측. 취소는 테스트에서 토큰과 상태/정리 함수를 직접 호출했다. 성능 개선 수치는 측정하지 않았다.

## 3단계 주의점

아직 준비 시간을 줄이거나 작업을 새 스레드로 분산하지 않았다. PreparingPlay는 현재 동기 함수 구간이며 ReadyToStart는 자동 시작 직전의 독립 상태다. 사용자가 머무는 준비 화면은 다음 단계에서 연결해야 한다.

중간 결과를 UI에 게시하려면 요청 세대 및 결과 소유권을 확정하고, 기존 전역 AudioEngine 컨테이너를 UI/worker가 동시에 수정하지 않도록 해야 한다. GPU 타깃 자원은 메인 스레드에서 준비하고, 해상도 변경으로 무효화된 자원만 재준비해야 한다. 기존 이미지 BGA start()의 초기 대기는 이번 단계에서 그대로다. worker 완료 메시지는 계속 join 이후에만 소비한다.

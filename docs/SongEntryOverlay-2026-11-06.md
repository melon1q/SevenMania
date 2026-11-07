# 곡 선택 직후 진입 오버레이 (2026-11-06)

## 변경한 흐름

곡 선택 → 기존 디코딩 worker 시작 → 전면 `songintro` 표시 → 설정한 표시 시간 종료 → 기존 PreparingPlay 프리뷰 → 필수 리소스 완료 및 기존 시작 정책 충족 → Playing.

디코딩이 먼저 끝나도 오버레이가 끝나기 전에는 `startPreparedChart()`가 시작을 거부한다. 표시가 끝나도 키음·BGA·GPU 준비 장벽은 그대로 유지한다. PreparingPlay가 먼저 표시되어야 GPU 준비가 진행되는 기존 정책도 유지한다. 즉시 자동 시작 모드에서는 준비 화면이 아주 짧을 수 있다. 프리뷰 자동/수동 시작 정책은 기존 설정을 따른다. 오버레이 Off는 기존 바로 준비 동작이다.

## 스킨 및 에디터

- 사용자 제공 `C:/Users/yakko/Documents/DefaultReference.semaskin`을 기본 스킨의 기준으로 사용했다. 원본 파일은 수정하지 않았다.
- 원본 ZIP의 기존 화면과 리소스 바이트를 보존하고 `manifest.json`의 supportedScreens에 `songintro`, `screens/songintro.json`만 추가했다.
- 에디터 화면 목록의 `songintro`에서 요소 추가/삭제, 이미지 가져오기·교체, 좌표·크기·레이어, 폰트·색상 및 바인딩을 기존 도구로 편집한다.
- `boxMode: title`은 선택한 곡의 타이틀 이미지다. 일반 image 요소로 패키지 내 고정 이미지를 사용할 수도 있다. 제목 이미지가 없으면 동적 이미지는 투명하게 비운다.
- 지원 데이터: `song.title`, `song.subtitle`, `song.genre`, `song.artist`, `song.bpm`, `chart.level`, `chart.levelText`.
- 구형 스킨에 songintro가 없으면 CPU 폴백 카드로 표시한다. 에디터로 구형 스킨을 열면 독립적인 편집용 songintro 페이지를 추가한다.
- 기존 On/Off, 표시 시간(800~1500ms, 기본1100ms) 설정을 재사용한다.

## 구현 위치

- `SongIntroHost.inl`: UI 소유 스냅샷 생성, 시작 시각, 독립 화면 및 타이틀 이미지 공급.
- `PlaybackLoadRuntime.inl`: 로딩 화면 최상단에서 오버레이를 먼저 표시하고 실제 시작을 차단.
- `PlaybackLoading.inl`: 실제 오디오 시작 경계에서 추가 방어.
- `Main.cpp`: worker 시작 직후 모듈 호출; Playing 이후 표시 경로 제거.
- `skin/SkinPackage.h`, `SkinLoader.cpp`, `SkinData.*`, `SkinApplication.*`: 다섯 번째 화면, subtitle 바인딩, 화면별 동적 박스 이미지 공급.
- `editor/EditorDocument.h`, `SkinEditor.cpp`: 화면 선택, 구형 스킨 편집 마이그레이션, 미리보기·저장/재열기.

## 검증

실제 Release 게임에서 Dreadnought [NORMAL] 선택 직후 전면 타이틀 이미지·장르·제목·작곡가·레벨이 표시되고 이후 플레이로 전환되는 것을 직접 확인했다. 외부 데스크톱 앱의 오버레이는 게임의 렌더링 계층 밖에 있다.

회귀 테스트는 실제 비동기 worker를 실행하여 즉시 표시 상태, 오디오 미시작, 오버레이 중 시작 거부, 취소·재선곡 및 기존 준비 장벽을 확인한다. 스킨 테스트는 songintro의 별도 타이틀 박스에 색상 텍스처를 공급한 후 GPU 읽기 결과를 확인한다. 에디터 smoke는 songintro의 미리보기와 저장 후 다시 열기의 JSON 일치를 확인한다.

성능 비교 수치는 측정하지 않았다. 장시간 플레이와 모든 사용자 스킨·해상도 조합의 수동 검증은 수행하지 않았다. 기존 FramePacer, Reflex, 판정 규칙 및 오디오/BGA 디코딩 알고리즘은 변경하지 않았다.

최종 검증: 기존 Release 구성으로 BmsPlayer, SevenManiaSkinEditor 및 두 통합 테스트 실행 파일 빌드 성공. BmsCompatibilityTests, BmsCompatibilityPlaybackTests, SevenManiaSkinBindingTests, SevenManiaSkinEditorSmoke 모두 통과(4/4, 총27.90초). 기본 BmsPlayer.exe 및 Release/Assets/Skins/DefaultReference.semaskin 갱신. Main.cpp 순증가 2줄.

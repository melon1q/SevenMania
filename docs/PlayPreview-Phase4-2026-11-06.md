# 4단계 플레이 프리뷰: 구현 범위와 미완료 항목

PlayPreview.h는 실제 Chart·AudioEngine과 분리된 시각 타이머, 시작 정책, 입력 지연 시각과 정적 배경 캐시를 관리한다. PlayPreviewRender.inl은 8개 레인에 고정 32개 위상 패턴을 반복 표시한다. 실제 노트 데이터를 생성·교체하거나 판정 함수에 전달하지 않는다. 기존 chartLayout, 270×배속×speedAt 계산과 fillGameRectSubpixel을 재사용한다. 일반 노트는 현 소스의 단색 폴백이다.

게임플레이 설정의 플레이 준비 시작 방식: 즉시 자동(기본), 프리뷰 자동(준비 진입/마지막 조작에서 2초), 수동(Ready 이후 Enter). INI Gameplay/PreviewStart=0..2에 저장한다. 기존 비동기 자원 장벽은 그대로다.

준비 중 키보드: F1/F2 배속, PgUp/PgDn 레인가드, 위/아래 판정선, 좌/우 기어, Enter 시작. 기존 저장 경로를 사용한다. 컨트롤러 논리 레인 0..7은 각각 기어 왼쪽/배속 감소/배속 증가/가드 감소/가드 증가/판정선 위/판정선 아래/기어 오른쪽, 메뉴 버튼은 시작이다. 상승 에지로 조절하고 누른 동안 자동 시작 타이머를 연장한다. 실제 컨트롤러는 미검증이다.

정적 스킨 배경은 전체 재렌더하지 않고 저장된 버퍼를 복원해 예시 노트 위치만 변경한다. 추가 버퍼는 가로×세로×4바이트(1920×1080에서 약 7.91MiB)이며 이것은 계산값이지 측정값이 아니다. 시작·취소 시 버퍼를 해제한다. 패턴은 고정 루프이고 별도 파일 I/O·키음 디코딩을 하지 않는다. 설정 조작 시 저장 I/O는 발생한다.

변경 파일: Main.cpp 연결, PlaybackLoadRuntime.inl 준비 렌더/시작 조건, PlayPreview.h/PlayPreviewRender.inl, SettingsController.h/SettingsActions.inl/SettingsRmlBindings.inl/SettingsRmlPage.inl, Assets/UI/settings.rml, 테스트 2개.

Release BmsPlayer.exe와 관련 테스트 빌드 성공. BmsCompatibilityTests, BmsCompatibilityPlaybackTests, SevenManiaSkinBindingTests, SevenManiaSkinEditorSmoke 4/4 통과(7.62초). 시작 정책의 2초 경계·조작 지연·수동 요청 단위 검사와 실제 비동기 로딩에서 프리뷰 자동/수동 시작 검사를 추가했다. 기존 BGA·오디오·판정 회귀 검사를 함께 실행했다.

미완료/미검증:
- 스킨별 노트 이미지 지정/렌더링 경로는 아직 구현하지 않았다. 기존 실제 노트 렌더링도 단색이며 이를 임의로 이미지 지원이라고 보고하지 않는다.
- 롱노트 예시, 네이티브 Win32 설정 폴백에 시작 모드 컨트롤 추가는 미구현이다. 설정은 RmlUi 게임플레이 페이지에서 제공한다.
- 실제 컨트롤러, 여러 스킨/해상도에서 움직임·가림의 시각 검증, 장시간 메모리 및 프레임 시간 비교는 미실행이다.
- 따라서 요청 전체의 완료 조건을 충족했다고 주장하지 않는다. 기본 프리뷰·입력·시작 정책을 구현한 상태이며 이미지 노트 통합과 성능/시각 검증이 남는다.

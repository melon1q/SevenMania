# 곡 진입 오버레이 (2026-11-06)

SongIntroOverlay.h는 타이틀 이미지와 장르/제목/서브타이틀/아티스트/레벨을 한 번 래스터화한 카드와 GPU 비트맵을 소유한다. 기존 drawPlaybackLoadingScreen의 titleImage 경로와 Chart 메타데이터를 재사용했다. 기존 로딩 이미지의 전면 표시 대신 플레이 레인 바깥쪽의 더 넓은 여백을 선택한다. 여백이 180px보다 작으면 표시를 생략한다. 따라서 첫 입력이 빠른 곡에서도 판정선이나 노트를 가리지 않으며 pre-roll과 오디오 지연은 추가하지 않았다.

Ready→Playing 시작 함수는 오디오 시작 전에 카드를 준비하고, AudioEngine::startAt 성공 후 표시 타이머를 시작한다. 기본 1100ms, 끝 200ms 동안 페이드아웃한다. 타이머는 steady_clock이며 playhead/판정과 독립적이다. 재시작에도 다시 준비·표시한다. ESC는 기존 일시정지이며 일시정지 화면에서는 오버레이를 숨긴다. 크기 변경 시 안전하게 생략한다.

RmlUi 게임플레이 설정에 곡 진입 연출 Off/On(기본 On), 연출 시간 800~1500ms(100ms 단위)를 추가했다. Gameplay/SongIntro, SongIntroMs로 저장한다. 별도 간단/전체 모드는 추가하지 않았다. Win32 폴백 설정 화면에는 신규 컨트롤을 추가하지 않았다.

CompositionPresenter에 독립적인 최종 UI 콜백을 추가했다. 기존 스킨 foreground 이후에 카드를 그린다. 오버레이는 매 프레임 이미지 파일·폰트를 읽지 않으며 준비 단계에서 캐시를 만든다. CPU 폴백은 AlphaBlend, GPU는 준비된 D2D bitmap을 사용한다. 스킨의 신규 정의 없이 작동한다. 초기에 이미지를 디코딩·텍스트 래스터화하는 동기 비용은 남는다. 인게임 성능 수치는 측정하지 않았다.

수정: Main.cpp(연결/설정/재시작), PlaybackLoading.inl(준비와 시작), CompositionPresenter.h(최종 콜백), SettingsActions.inl/SettingsController.h/SettingsRmlBindings.inl/SettingsRmlPage.inl, Assets/UI/settings.rml. 추가: SongIntroOverlay.h, SongIntroHost.inl. 테스트: CompatibilityPlaybackTests.cpp, SkinBindingTests.cpp.

Release BmsPlayer.exe 업데이트 및 관련 빌드 성공. BmsCompatibilityTests, BmsCompatibilityPlaybackTests, SevenManiaSkinBindingTests, SevenManiaSkinEditorSmoke 4/4 통과(7.60초). 카드 테스트는 유효 BMP/없는 이미지/이미지 미지정, 서브타이틀, 빈 메타데이터, 타이머 경계·reset·playhead 불변을 검사했다. GPU 카드 렌더링은 공유 D2D 테스트 타깃에서 검사한다. 기존 비동기 로딩 자동/수동 시작, ESC 취소, 재시작, BGA·키음·판정 회귀 검사를 재사용했다.

미검증: 사용자 실제 스킨·곡별 수동 시각 검사, 장시간 실행, 다양한 DPI/화면 비율, 인게임 프레임 타임 비교. 저해상도나 큰 기어로 외부 여백이 부족하면 연출을 생략하는 것이 의도된 안전 폴백이다. 표시 중 화면 크기를 변경하면 연출을 종료한다.

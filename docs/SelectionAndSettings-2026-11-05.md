# 곡 선택 버튼·설정 재배치 — 2026-11-05

## 반영 내용

1. 기본 스킨에 select-settings(설정), select-info(곡 정보) 이미지 요소를 추가했다. 각각 독립 PNG 리소스로 에디터에서 교체 가능하다. 배속·게이지는 위쪽 줄, 두 새 버튼은 그 아래 줄이다. 곡 선택의 판정 슬롯은 제거했다. 사용자 스킨에 같은 ID의 이미지 요소를 넣으면 동일한 동작을 연결할 수 있다.
2. 곡 정보는 별도 창 없이 중앙 불투명 사각형 패널에 표시한다. 곡명, 작곡가, 장르, BPM, 키 모드, 난이도, 일반/롱노트 수, 길이를 제공한다. 메타데이터만 있는 곡은 버튼을 누를 때 원본을 파싱해 개수를 계산한다. 매 프레임 파싱하지 않는다. 패널이 열리면 목록 입력을 막고 Esc 또는 클릭으로 닫는다.
3. 일반/요약 설정의 사용자 판정 프로필 선택을 제거했다. 기본 프로필은 Normal로 고정하며 기존 SM-JUDGE-1의 BMS RANK 적용과 확장 판정 정책을 유지한다. 내부 구형 INI 키는 호환성을 위해 보존한다.
4. 키 배치를 별도 [키 설정]으로 분리했다. 컨트롤러 설정도 해당 페이지로 이동했다. 5키+SCR(6입력), 7키+SCR(8입력), 9키(9입력), 10키+2SCR(12입력), 14키+2SCR(16입력)를 KeyBindings5/7/9/10/14 섹션에 독립 저장한다. 취소/중복/예약 키 처리와 기본값 복원을 유지한다. 7키는 기존 KeyBindings와 함께 저장해 마이그레이션한다. 다른 모드의 플레이 제한은 유지한다.
5. 게임 내 스킨 탭과 판정/콤보 렌더링 선택을 제거했다. 런처 [스킨]에서 .semaskin 선택·검증·기본 스킨 복원, 판정 Y 위치, 배경 종류·블러·불투명도, 사용자 이미지 선택을 제공한다. 런처의 기존 적용/게임 시작 시 원자 저장·충돌 검사 정책을 사용한다. 이미지 모드를 기본으로 적용하며 이미지가 없는 구형 스킨은 기존 폰트 대체를 유지한다.
6. 스킨 에디터의 Box에 hybrid 모드를 추가했다. 게임 곡 선택에서 이 박스는 런처의 배경 설정을 따라 단색/선택곡 BGA·동영상/타이틀/사용자 이미지를 표시한다. 임의 사용자 동영상 파일 가져오기 기능을 추가한 것은 아니다. 에디터는 기존 Box 미리보기 구조를 사용하며 실제 선택곡 영상은 게임에서 공급한다.
7. 중복 콤보 텍스트 위치 옵션을 새 설정 화면에서 제거했다. 판정과 함께 사용하는 위치는 런처의 판정 Y 설정으로 관리한다.
8. 기존 FPS 옵션을 보존하며 600/800/1000과 사용자 설정을 추가했다. 게임 그래픽의 사용자 범위 입력 또는 런처 사용자 FPS(1~1500)를 사용한다. 무제한의 기존 1500 상한·VSync 정책은 유지했다.
9. 플레이 중 Enter+PgUp/PgDn은 레인 가드를 1%씩, Enter+위/아래는 판정선을 1px씩 조정하고 저장한다. 판정선 최종 위치는 기존 화면 경계 규칙을 따른다. 단독 PgUp/PgDn의 BGA 밝기 조절은 유지한다.
10. 곡 선택 휠 한 노치당 한 항목씩 이동한다. Windows의 휠 줄 수 설정을 곱하지 않고 고해상도 휠의 잔여 델타를 누적한다.

## 주요 변경 위치

- Main.cpp: 버튼 액션·패널·키 캡처 연결·휠·실시간 위치 조정. 기존 사용자 스크롤 애니메이션 코드는 보존했다.
- KeyProfiles.h: 모드별 키 배열·검증·INI 저장/복원.
- Launcher.cpp, LauncherSkinSettings.inl: 런처 스킨/사용자 배경 설정과 사용자 FPS.
- SettingsRmlPage.inl, SettingsRmlBindings.inl, SettingsActions.inl, SettingsController.h, Assets/UI/settings.rml: 설정 이동/삭제 및 새 값 연결.
- FrameRateSettings.h, SharedStartupSettings.h: 프레임 목표와 사용자 값 검증.
- SkinApplicationHost.inl, skin/SkinApplication.cpp, skin/SkinLoader.cpp, editor/SkinEditor.cpp: 이미지 버튼 클릭 판별·하이브리드 박스 연결.
- Assets/Skins/DefaultReference.semaskin: 선택 화면의 새 이미지 요소 및 슬롯 위치. 다른 화면 디자인은 이번 작업의 변경 대상이 아니다.
- CMakeLists.txt: 런처/런처 UI 테스트가 기존 스킨 로더 라이브러리를 재사용하도록 연결.

## 검증

BmsPlayer.exe, SevenManiaLauncher.exe, SevenMania Skin Editor.exe Release 빌드 성공. 기본 build-rmlui-song-list/Release 경로를 갱신했다.

최종 회귀 테스트 6/6 통과(22.87초): BmsSongSelectionTests, SevenManiaSkinBindingTests, BmsSettingsPhase6Tests, SevenManiaSkinEditorSmoke, SevenManiaLauncherTests, SevenManiaLauncherUiTests. 마지막 런처 배경 목록 UI 변경 후 런처 테스트를 추가 실행해 통과했다.

추가 자동 검증: 두 이미지 버튼의 실제 클릭 영역, 정보 패널 열기/닫기 및 일반 2·롱노트 1 개수 표시, 5개 키 모드의 실제 캡처 완료와 재로딩, 사용자 937FPS/800FPS 적용, 수동 판정 프로필 변경 차단. 키 설정 페이지의 실제 렌더 캡처(keys.png)를 확인했다. 에디터 기본 저장/불러오기 스모크 검사도 통과했다.

반복 설정창 수명 테스트에서 private memory 증가 32MiB 허용치를 한 번 초과했다. 기준을 낮추지 않고 재실행한 최종 테스트는 통과했다. 재현되는 누수로 확정하지 않았으며 장시간 안정성 보장으로 해석하지 않는다. 테스트와 빌드 로그는 build-rmlui-song-list/Validation/controls-*에 있다.

## 확인하지 않은 사항

실제 컨트롤러를 사용한 조작, 모든 해상도에서의 패널 외형, Enter 조합의 하드웨어 키 입력, 장시간 플레이의 프레임 시간은 수동 측정하지 않았다. 키 설정은 비지원 모드를 다시 활성화하지 않는다. 사용자 스킨의 새 버튼은 이미지 ID를 추가해야 하며 기존 스킨을 임의 재작성하지 않는다. 기본 스킨의 작업 전 복사본은 build-rmlui-song-list/Validation/controls-before/Assets/Skins/DefaultReference.semaskin에 보존했다.

# C# 부분 도입 1단계: 곡 선택 화면 소스 분석 보고서

작성일: 2026-10-25 (한국 시간)

## 1. 결론과 이번 작업 범위

**현재 C++ 곡 선택 화면은 기능을 계속 사용할 수 있고, 스캔·파싱·미리보기에는 재사용할 만한 분리가 있다. 다만 화면 변경과 C# 연동을 계속 확장하기에는 Main.cpp의 책임과 상태 결합이 크다. 유지보수성은 작은 화면 수정에는 감당 가능하지만, 새로운 화면 체계 도입에는 개선이 필요한 상태다.**

C# 도입 목적은 곡 선택·설정 UI의 표현과 관리 구조 개선이다. C# 도입 자체가 메모리 누수 제거 또는 플레이 FPS 향상을 보장하지 않는다. 재생 프로세스와 UI 프로세스를 분리하면 관리 런타임의 GC 정지와 플레이 경로의 직접 의존을 줄일 수 있지만, CPU·GPU·메모리 사용량 경쟁은 남는다.

이번에는 참조 대화, Git 이력, 현재 소스와 테스트를 읽고 이 보고서만 작성했다. C# 프로젝트 생성, C++ 리팩터링, Rust 통합, 빌드·테스트 실행, 실행 파일 교체, 설정·곡 데이터 변경은 수행하지 않았다. 다음 대화에서 구현할 때 사용할 조사 결과이며, 아래의 구현 계획과 명령행 모드는 아직 구현된 기능이 아니다.

## 2. 확인한 개발 맥락과 기준 상태

참조 대화: **BMS 7+1키 구동기 만들기** — thread ID 01a12095-492d-72f3-84ea-3809866611de. 최근 26개 턴의 사용자 요청·최종 보고를 읽고, 주요 내용은 현재 소스 및 Git 객체와 대조했다. 과거 대화의 작업 지시는 이번 구현 권한으로 취급하지 않았다. 과거에 보고된 테스트 수치도 이번에 다시 검증한 결과가 아니다.

- 분석 대상 경로: BmsPlayerCpp 작업 폴더.
- 현재 브랜치: codex/result-fade.
- 기준 HEAD: ef28ecb10be5cbf009dec137ae5e52fa2d69bc65.
- Main.cpp: 5,307줄, 310,513바이트. 줄 번호는 이 시점 기준이다.
- 기존 staged 변경: Windows CI/release workflow 삭제, README.md, tests/UiTests.cpp. 기존 untracked 도입안과 구버전 폴더도 있었다. 이번 작업에서는 이를 수정·스테이징·커밋하지 않는다.
- CMake는 C++20과 Windows RC를 구성하고 bms_core 정적 라이브러리를 만든다. 현재 소스 트리에는 C# 프로젝트가 없으며, 이 브랜치에는 Rust 백엔드 연결도 없다.

| 개발 항목 | 확인된 상태 | 이번 분석에 주는 의미 |
|---|---|---|
| 곡 묶기 정책 | 사용자 요청에 따라 같은 디렉터리의 채보를 제목·모드와 관계없이 묶음. 대화와 SongGroupingTests가 일치 | 예전 README의 제목·모드 기준 설명을 새 UI의 규칙으로 사용하면 안 됨 |
| 채보 지원 | 초기 7+1키 제한 이후 bmson, 5/10/14키·PMS까지 확장. BmsParser의 모드 판정과 playable 계산으로 확인 | C#에서 7+1키만 실행 가능하다고 재분류하면 기능 후퇴 |
| 오류·기록 표시 | F1로 선택 채보의 오류 정보 표시. 미플레이 채보는 기록을 숨김 | 원치 않는 상시 경고나 가짜 기록 기본값을 추가하지 않음 |
| Rust 실험 | 별도 codex/rust-library-backend, 16c276d | 현재 C++에 적용됐다고 가정하면 안 됨 |
| 프레임 안정성 조사 | 텍스트 재생성 최적화 후보가 있지만 최종 통합·메인 병합은 보류된 맥락 | C# 화면 전환에 미승인 최적화나 실험 브랜치를 함께 섞지 않음 |
| 최근 화면 변경 | fbf9fc9의 플레이→리절트 페이드, ef28ecb의 같은 이름 BGA 영상 대체 처리 포함 | 리절트와 미디어 복귀 흐름도 도입 경계에 포함 |

Rust 실험 보고서는 git show codex/rust-library-backend:docs/Rust-Backend-Experiment.md로 읽었다. 실제 범위는 UTF-16 줄 분리, JSON 문법/중간 트리, 파일 열거, 곡명 정규화의 일부 이전이다. 명령·조건문·노트·타이밍 전체 의미 분석을 Rust로 바꾼 버전이 아니다. 보고된 스캔 성능 개선은 없고, JSON 왕복 비용도 남아 있다. C# 도입과 이 실험 통합은 분리해야 한다.

## 3. 곡 선택 화면의 실제 구조

| 역할 | 현재 구현 위치 | 결합 형태 |
|---|---|---|
| 곡 데이터·선택 상태 | [Main.cpp](../src/Main.cpp) 326줄 gCharts, 327줄 SongGroup, 986줄 selectedGroupIndex(), 997줄 selectedIndex() | HWND ListView의 선택 행 → lParam의 그룹 인덱스 → 채보 인덱스 |
| 곡 그룹·난이도·정렬 | Main.cpp 1202줄 cycleSelectedVariant(), 1236줄 compareTitleText(), 1281줄 buildSongGroups(); [SongGrouping.h](../src/SongGrouping.h), [ChartOrdering.h](../src/ChartOrdering.h) | 순수 규칙 일부는 헤더에 있지만 UI 갱신·효과음·선택 복구는 Main.cpp |
| 가져오기·분석 | Main.cpp 1319줄 scanFolder(); [SongLibrary.cpp](../src/SongLibrary.cpp) 202줄 scanSongFolder() | 외부 폴더를 실행 파일 옆 Songs에 복사하고 전체 라이브러리 재분석 |
| 미리보기 | Main.cpp 1010줄 stopBrowseMedia(), 1034줄 tickBrowseMedia(); [SongPreview.h](../src/SongPreview.h) | 그룹 대표 채보, 오디오 시계, 선택 지연, 백그라운드 캐시와 연결 |
| 배경·화면 | Main.cpp 2563줄 BrowsePanelLayout, 2630줄 drawBrowseScreen(), 3422줄 layoutControls(); [BrowseBackdrop.h](../src/BrowseBackdrop.h) | GDI/GDI+ 직접 그리기와 네이티브 ListView·버튼 혼합 |
| 설정·기록 | Main.cpp 578줄 loadDisplaySettings(), 644줄 saveDisplaySettings(), 919줄 loadPlayRecords(), 962줄 savePlayRecord(), 3722줄 settingsProc() | 전역 설정값·실제 오디오/GPU 설정·INI 저장이 같은 경로에 위치 |
| 재생 진입·복귀 | Main.cpp 1464줄 showBrowseView(), 1519줄 beginSelectedChart(), 1583줄 finishSelectedChart() | 선택 행과 전체 gCharts, 미디어, 게임 상태·컨트롤 표시 여부를 함께 변경 |
| 입력·실행 루프 | Main.cpp 4467줄 songListSubclass(), 4560줄 windowProc(), 5062줄 wWinMain() | 곡 선택·설정·로딩·플레이·일시정지·리절트가 같은 메시지 루프 공유 |

현재의 흐름은 다음과 같다.

~~~text
프로그램 시작 → INI/기록 로드 → scanFolder()
  → C++ 가져오기/병렬 파싱 → WM_LOAD_FINISHED
  → 그룹 생성/대표 프리뷰 준비 → ListView 생성·선택
  → 500ms 후 프리뷰 → Enter/START/연습모드
  → 선택 채보 재파싱·음원/BGA 준비 → 실제 플레이
  → 리절트/페이드 또는 QUIT → showBrowseView()
~~~

## 4. 유지할 만한 장점

1. **스캔·가져오기 로직이 UI에서 어느 정도 분리돼 있다.** SongLibrary의 함수는 경로, 취소 플래그, 진행률 콜백으로 호출된다. 디렉터리 단위 데이터 재사용, 최대 4개 작업자, 파일 복사·취소·실패 처리를 이미 갖춘다. C#에서 같은 기능을 새로 작성하기보다 이 경계를 활용할 수 있다.
2. **UI를 막지 않는 로딩 경로가 있다.** scanFolder()/beginSelectedChart()가 작업 스레드를 만들고 PostMessageW로 완료를 알린다. 완료 처리에서는 join() 후 결과를 읽는다. 공유 전역 결과 구조 때문에 분리 비용은 있지만, 이 사실만으로 데이터 경쟁이 있다고 단정할 근거는 없다.
3. **미리보기는 이미 별도 객체다.** SongPreview는 디코더 작업자 하나와 선택 요청 우선 처리, 취소, 최근 음원 캐시를 갖는다. 500ms 선택 지연과 그룹 대표 프리뷰 덕분에 빠른 탐색·난이도 전환마다 재생이 재시작되지 않도록 구성돼 있다.
4. **순수 규칙과 회귀 자산이 있다.** SongGrouping, ChartOrdering, PreviewTiming에는 HWND 없이 평가할 수 있는 규칙이 있다. 그룹·라이브러리·오디오·화면 테스트는 재작성할 때 기준으로 사용할 수 있다.
5. **해제 경로가 존재한다.** WM_DESTROY에서 작업자 join, 프리뷰 종료, 이미지 목록·폰트·DC·비트맵 정리가 수행된다. CompositionPresenter는 ComPtr를 사용한다. 수동 자원이 많다는 이유로 실제 누수가 확정됐다고 볼 수는 없다.

## 5. 주요 문제와 유지보수 위험

아래는 재현 확인한 실행 버그와 동일한 의미가 아니다. 코드로 확정할 수 있는 구조, 논리적으로 도출한 동작, 실행 검증이 필요한 위험을 구분했다.

### 5.1 높은 우선순위: 선택 모델이 네이티브 컨트롤에 묶여 있음

**확인:** selectedIndex()는 selectedGroupIndex()를 통해 ListView의 현재 선택 행을 읽어야 한다. beginSelectedChart()도 이 결과에 의존한다. gCharts, gSongGroups, gBrowseSelections, gActiveChartIndex가 별도의 전역 상태다.

**영향:** C#에서 채보 경로만 보내도 지금 함수를 바로 재사용할 수 없다. 숨겨진 C++ 목록을 먼저 만들고 가짜 선택 이벤트를 보내는 연결은 UI 구조에 계속 종속된다. 정렬·재검색·필터 이후 인덱스 수명도 관리해야 한다.

**권장:** 목록 선택 상태를 HWND에서 독립시키고, 경로/채보 식별자·옵션을 입력으로 받는 재생 요청 경계를 만든다. 목록용 요약 모델과 재생용 전체 Chart는 분리한다. C#은 노트·음원 맵 전체를 보관하지 않는다.

### 5.2 높은 우선순위: 화면·재생·설정 상태 전이가 한 곳에 몰림

**확인:** gPlaying, gPaused, gShowingResult, gLoading, gPlaybackLoadReady와 여러 창 핸들이 함께 상태를 표현한다. showBrowseView()는 화면 표시뿐 아니라 오디오·GPU·BGA·연습 데이터 복구까지 처리한다. 약 640줄의 settingsProc()와 약 480줄의 windowProc()가 전역 상태를 직접 바꾼다.

**영향:** 화면 기능 추가가 재생 준비·복귀·자원 해제에 영향을 줄 수 있다. C#이 같은 상태를 별도로 관리하면 둘 중 누가 재생/취소/복귀를 확정하는지 모호해진다.

**권장:** LoadingLibrary, Browsing, PreparingPlayback, Playing, Paused, Result 같은 명시적인 상태와 요청/완료 결과를 정의한다. 실제 재생 성공·실패·종료의 최종 판단은 C++이 맡고 C#은 통지된 상태를 표시한다. 초기 도입에서 모든 상태 처리를 동시에 다시 작성하지 않는다.

### 5.3 높은 우선순위: 프로세스 분리 시 RANDOM 분기가 달라질 수 있음

**확인:** BmsParser.cpp 284~285줄은 프로세스 안의 static sessionSeed 또는 명시적 seed와 경로 해시를 결합한다. 같은 프로세스의 목록/프리뷰/재로딩은 이 기준을 공유하지만, 스캔용 실행과 플레이용 실행은 서로 다른 기본 세션 시드를 만들 수 있다. BmsParser 생성자는 seed를 받을 수 있으나 scanSongFolder()의 공개 API에는 seed 인수가 없다.

**영향:** 기존 도입안처럼 카탈로그 실행과 재생 실행을 분리하면 같은 파일의 다른 분기를 선택해 목록·프리뷰·실제 노트가 불일치할 수 있다. 아직 새 프로세스 모드를 구현하지 않았으므로 현재 도입 버그로 재현한 결과는 아니다.

**권장:** 하나의 선택 세션에서 재사용할 parseSeed 또는 확정 분기 정보를 카탈로그·프리뷰·재생 계약에 포함한다. 경로 정규화와 정적 랜덤 옵션에도 동일한 기준을 사용한다. 같은 seed/파일이 같은 노트·자산·타이밍을 만드는지 후속 검증한다.

### 5.4 높은 우선순위: 기존 UI 테스트가 완료 판정에 사용할 수 없는 조건을 포함

**확인:** tests/UiTests.cpp 178줄은 GetDlgItem(app.main, 1003)의 활성화를 기다린다. 현재 Main.cpp에는 ID_PLAY 상수와 명령 처리만 남아 있고 ID_PLAY 버튼 생성은 없다. START는 직접 그린 영역으로 처리된다. 따라서 이 대기 조건은 실제 라이브러리 로딩 완료를 판별하지 못한다. Rust 실험 보고서의 동일 실패 설명과도 일치한다.

**영향:** C# 도입 전후를 비교할 때 기존 UI 테스트 전체 통과를 기준으로 삼을 수 없다. 현재 staged UiTests.cpp의 두 줄은 프리뷰 토글 검사 변경이며 이 대기 조건을 고치지 않는다.

**권장:** 후속 구현에서 실제 로딩 상태·완료 이벤트를 기준으로 검사를 분리한다. WPF UI와 Win32 UI의 자동화는 각 화면 체계에 맞춰 분리하되, 그룹·선택·요청 데이터 계약 검사는 공통으로 만든다. 이번에는 테스트를 실행하거나 수정하지 않았다.

### 5.5 중간 우선순위: 고정 좌표·직접 그리기·이벤트 경로 중복

**확인:** BrowsePanelLayout과 layoutControls()는 창 크기에 따라 일부 배치를 조정하지만, 폰트 크기·버튼 높이·행 높이는 직접 지정한다. ListView는 owner-data 모드가 아닌 실제 행 삽입 방식이다. 상태/상세 문구를 숨긴 STATIC 컨트롤에 저장한 뒤 drawBrowseScreen()에서 GetWindowTextW로 다시 읽어 그린다. 키 처리는 songListSubclass, LVN_KEYDOWN, WM_KEYDOWN, wWinMain의 선처리에 분산돼 있다.

**영향:** 테마·접근성·높은 DPI·긴 제목·검색 필터를 추가할 때 수정 지점이 늘어난다. 동일 명령을 추가하면서 포커스에 따른 동작 차이가 생길 수 있다. 높은 DPI 표시 실패나 대규모 목록 성능 저하는 이번에 실측하지 않았다.

**권장:** WPF의 XAML 스타일·레이아웃·바인딩과 명령 경계로 표현을 옮긴다. 기존 100ms 키 반복 제한, 260ms 스크롤 애니메이션, 난이도 순서를 무심코 기본 컨트롤 동작으로 바꾸지 않는다. 가상화도 실제 템플릿·스크롤 구성에서 작동하는지 확인해야 한다.

### 5.6 중간 우선순위: 화면 그리기 중 이미지 디코딩·데이터 복제

**확인:** drawBrowseScreen()의 배너 표시는 매 호출마다 파일 경로로 Gdiplus::Bitmap을 생성한다. WM_LOAD_FINISHED는 그룹별 Chart를 previews에 복사한 후 일부 벡터를 비운다. SongPreview는 전체 Chart 기반 준비 요청과 AudioEngine 캐시를 보관한다.

**영향:** 화면 갱신 중 파일 접근·디코딩이나 큰 자료 복사가 발생할 수 있다. 프리뷰 캐시의 64개/256MiB 기준은 캐시 내부 PCM에 대한 축출 기준이며, 현재 재생 중·준비 중 음원, 대기 Chart, GPU 자원까지 포함한 앱 전체 메모리 상한이 아니다. 현재 누수가 발생했다고 단정할 근거는 없다.

**권장:** 이미지 변경 시 한 번 준비하는 배너 캐시와 경량 ChartSummary/PreviewRequest를 검토한다. 캐시는 용량 상한·축출·종료 시점을 정한다. C#에도 전체 Chart와 큰 PCM을 복제하지 않는다.

### 5.7 중간 우선순위: 그룹과 숨김 목록의 분류 기준이 서로 다름

**확인:** buildSongGroups()는 같은 폴더에 실행 가능·불가 채보를 함께 묶고 실행 가능 채보를 앞에 둔다. rebuildSongRows()는 그룹 첫 채보의 playable로 그룹 전체의 일반/숨김 목록을 결정하지만 숨김 개수는 실행 불가 채보 수로 센다. cycleSelectedVariant()는 그룹의 모든 채보를 순회한다.

**도출되는 예:** 한 폴더에 정상 채보와 실행 불가 채보가 함께 있으면 숨김 개수는 증가하지만, 해당 그룹은 일반 목록에 남는다. 좌우 순회로 실행 불가 채보를 선택할 수도 있다. beginSelectedChart()가 실행을 차단하므로 실행 가능 검사가 빠진 것은 아니다.

**권장:** 사용자 의도인 “같은 폴더의 모든 채보 묶기”는 유지한다. 숨김 화면을 채보 단위로 보여줄지, 그룹 안에서 상태를 표시할지 별도 계약으로 정한다. 이 논리적 불일치의 실제 UI 표시는 후속 혼합 폴더 사례로 확인한다.

### 5.8 중간 우선순위: 설정·기록 저장에 이중 작성자가 생길 위험

**확인:** 설정은 현재 C++ INI 함수가 쓰며, 플레이 중 F1/F2 배속·PageUp/PageDown 밝기 변경도 saveDisplaySettings()를 호출한다. 기록은 정상 종료 시 C++이 최대값을 누적하고 경로 기반 INI 섹션에 쓴다. BMS_PLAYER_CONFIG_PATH, 테스트·프로파일 전용 경로도 있다.

**영향:** C#이 같은 INI 전체를 별도로 저장하거나 JSON으로 즉시 옮기면 플레이 중 변경과 기록을 덮어쓸 수 있다. 새 실행 파일 옆 Songs/Assets를 무조건 사용하면 기존 라이브러리를 못 찾거나 불필요한 복사가 발생할 수 있다.

**권장:** 첫 부분 도입에서는 C++의 기존 INI와 기록을 기준으로 두고, C#에는 조회 또는 명시적인 변경 요청만 제공한다. 전용 저장 주체 이전과 형식 변환은 별도 단계로 진행한다. 런처 위치, 플레이어 위치, libraryRoot, assetsRoot, configPath, recordsPath는 서로 독립적으로 확정한다.

### 5.9 높은 이식 위험: 미리보기·연습·리절트는 단순한 목록 기능이 아님

**확인:** 프리뷰는 전용 음원이 없으면 노트·BGM을 합성하고 대표 채보 시간을 공유한다. 배경은 프리뷰 오디오 시계를 따른다. 연습모드는 전체 노트·BPM 등을 필요로 한다. captureBrowseBackdrop()는 현재 C++ 곡 선택 화면을 그려 리절트 배경을 만든다. 리절트 페이드와 GPU 출력 제거도 같은 창의 복귀 흐름에 들어 있다.

**영향:** C# 목록만 만들고 C++ 목록을 삭제하면 프리뷰·결과 배경·연습 진입·정렬/선택 복귀가 함께 없어질 수 있다. WPF의 일반 미디어 컨트롤이 기존 프리뷰 합성을 그대로 대체한다고 볼 수 없다.

**권장:** 첫 구현은 실험용 별도 진입점으로 만들고, 원래 기능을 사용 가능한 경로로 유지한다. C++ 프리뷰 서비스, WPF 이미지 표시, 결과 배경 전달 중 어떤 방식을 쓸지 명시한다. 매 프레임 CPU 비트맵을 JSON으로 보내는 연결은 피한다. 프리뷰 정지·디코더 유휴 확인 후 플레이를 시작하고, 리절트 배경은 시작 전에 한 번 전달하거나 기존 C++ 렌더링을 유지하는 방법을 비교한다.

## 6. 기존 도입안에 대한 보완 판단

[CSharpSongSelectionAdoption.md](CSharpSongSelectionAdoption.md)는 방향을 정하는 참고 문서로 적절하다. 이번에는 원문을 수정하지 않았으며, 구현 시 다음 차이를 반영해야 한다.

| 기존 방향 | 이번 분석의 보완 |
|---|---|
| C# 프런트엔드와 C++ 플레이어의 프로세스 분리 | 유지. 현재 직접 재생·카탈로그 명령행 모드는 없고 wWinMain의 PWSTR 인수도 이름 없이 미사용이므로 먼저 진입점을 만들어야 함 |
| C#의 설정 저장 소유 | 장기 후보. 첫 단계는 기존 C++ 작성 규칙을 유지해 이중 쓰기와 INI 변환을 피함 |
| JSON 곡 요약 | 그룹 식별자·선택 채보·실행 가능 상태·대표 프리뷰·부분 로딩 정보·RANDOM 시드가 추가로 필요 |
| Rust 파서 연결 | 별도 실험을 재사용할 후보이지 완료된 전체 파서가 아님. C# 도입과 동시 통합하지 않음 |
| WPF와 선택적 AOT | C# 언어의 실행 방식과 선택 UI 프레임워크의 AOT 지원은 별개. WPF 채택만으로 AOT 배포가 된다고 가정하지 않음 |
| 목록·설정 화면의 단계적 이전 | 곡 선택부터 적용. 설정 전체·연습·프리뷰·리절트는 각 기능의 인수인계 조건을 충족한 다음 이전 |

## 7. 다음 대화의 구현을 위한 권장 최소 범위

**C++ 목록/파서/게임을 전면 교체하지 않고, C# 목록과 상세 정보부터 별도 실행 파일로 시험하는 방향을 권장한다.** 다음은 실행 지시가 아닌 후속 구현 설계안이다.

1. **데이터 계약과 C++ 경계 준비:** HWND 없이 라이브러리 요약을 만들고 지정 채보를 재생할 수 있는 함수를 분리한다. 기존 파서·복사·그룹·난이도 규칙을 호출한다. 새 명령행 모드는 초기 UI 생성 전에 분기하도록 설계한다. WIN32 실행 파일의 표준 출력 핸들·Unicode 경로 전달·종료 코드도 명시한다.
2. **별도 C# 목록·상세 화면:** 기존 규칙이 반영된 카탈로그를 표시한다. 선택된 chartId를 단일 상태로 관리하고 가상화·취소·오류 표시를 갖춘다. 검색은 신규 기능이므로 기존 정렬·키 입력과 독립적으로 검토한다.
3. **곡 시작·복귀 연결:** C#이 채보 경로·파싱 시드·설정 경로·모드(normal/practice)를 전달한다. 초기에는 일반 플레이부터 연결하고 연습은 기존 경로로 제공한다. C++이 준비 성공/실패/취소/결과 종료를 통지한다. C#은 플레이 중 디코딩·스캔·애니메이션을 정지 또는 유휴화하고 동기 응답을 요구하지 않는다.
4. **선택 상태·설정 보존:** chartId와 groupId로 복귀 상태를 복구한다. 최초 단계에서 INI와 기록은 기존 C++이 작성한다. C# 전체 설정 UI와 새 저장 형식은 후속 확장으로 분리한다.
5. **미디어·화면 전환 호환 후 정식 사용:** 프리뷰 합성·BGA 배경·클릭 효과음·연습·리절트 배경을 기존 동작과 비교한다. 지원하지 않는 기능은 실험 화면에 명시하고 원래 실행 경로를 제공한다. 기능 검증 전에는 새 화면을 기존 실행 파일의 기본 동작으로 대체하지 않는다.

초기 프로그램 간 통신에는 버전 있는 UTF-8 파일/메시지 계약을 사용한다. named pipe 등 상시 IPC가 필요한 시점은 미리보기·취소·진행률 요구로 판단한다. 일회성 카탈로그를 한 번 내보내는 기능만으로 모든 비동기 동작이 해결됐다고 보고하지 않는다.

### 계약에 필요한 최소 데이터

| 계약 | 필수 의미 |
|---|---|
| LibrarySnapshot | schemaVersion, snapshotId, libraryRoot, 파싱 세션/시드 규칙, 정렬·그룹 정책 버전 |
| SongGroupSummary | groupId, 표시 제목, 정렬 키/순서, chartIds, 대표 프리뷰 chartId |
| ChartSummary | chartId, 정규화한 실제 경로, 원본 제목/부제/아티스트/장르, 레벨 원문, 모드, BPM, playable, blockReason, partialLoad/사유, 이미지·프리뷰 요약, played/기록 |
| StartPlayback | requestId, snapshotId, chartId/경로, parseSeed, 적용 설정 경로, 일반/연습 옵션 |
| OperationEvent | requestId, 단계, 진행률, 완료/실패/취소, 진단 정보 |
| PlaybackFinished | requestId, 성공 종료/중단/실패, 적용된 설정 변경·기록 갱신 여부, 복귀할 chartId |

목록의 배열 인덱스나 HWND는 프로세스 간 식별자로 사용하지 않는다. 오래된 재검색 결과가 최신 화면을 덮어쓰지 않게 snapshotId/requestId를 확인한다. 문자열 인코딩·경로 기준·파일 변경 후 재분석·메시지 크기/취소/타임아웃 정책을 계약에 포함한다. 원시 포인터·STL 객체·Rust 컨테이너의 소유권은 경계 밖으로 넘기지 않는다.

## 8. 후속 검증 목록과 이번 분석의 한계

이번에는 읽기와 문서 작성만 수행했다. 아래는 다음 구현에서 수행할 검증 계획이며 통과 결과가 아니다.

- 같은 폴더의 SP/DP·다른 제목·문자열 난이도·정상/불가 혼합 채보를 기존과 비교한다.
- UTF-8/CP932/CP949 메타데이터, 한글·일본어·공백·따옴표 경로를 정확히 전달한다.
- 같은 RANDOM 시드에서 카탈로그·프리뷰·재생의 분기와 노트·자산이 일치한다.
- 재검색·폴더 복사·취소·원본 잠금·플레이 준비 실패에서 상태와 파일이 일관된다.
- F1 오류, 미플레이 기록 숨김, Enter/Shift+Enter/우클릭 더블클릭 연습 진입, S 정렬·Space 설정을 보존한다.
- 프리뷰 500ms 대기, 그룹 난이도 전환 시 연속 재생, 전용 파일 실패 후 합성, 재생 시작 전 정지를 확인한다.
- 제목 이미지 로딩·QUIT·리절트 페이드·결과 배경·다음 곡 선택 복귀와 GPU 폴백을 확인한다.
- 기존 UiTests의 무효 조건을 고친 뒤 네이티브 기준선을 확보한다. 테스트를 통과했다는 과거 보고만으로 새 화면을 승인하지 않는다.
- 반복 선택·재검색·창 열기/닫기에서 관리 힙, 네이티브 메모리, GDI/OS 핸들, GPU 자원이 안정화되는지 본다. 캐시 보유와 실제 누수를 구분한다.
- C#에서 큰 목록/이미지/GC 부하를 주었을 때 플레이어가 C# 응답을 기다리지 않는지 확인하고, 프레임 P99/P99.9·오디오 버퍼 공급·자원 경쟁을 비교한다.
- SystemResources의 현재 CPU/GPU 통계와 메모리 제한은 C++ 프로세스 기준이다. 별도 C# 프로세스까지 포함하는 총량·제한으로 오해하지 않게 표시한다.

정적 분석으로 UI 처리 시간, 실제 누수량, GC 멈춤 시간, 사용자 장치의 입력·오디오 지연을 확정할 수 없다. C# 도입의 성능 개선율은 측정 전에는 제시하지 않는다. 다음 구현을 시작할 때 브랜치·HEAD·작업 중 변경을 다시 확인해야 한다.

## 9. 근거 자료

- 참조 대화: BMS 7+1키 구동기 만들기, 위 thread ID. 읽은 범위는 최근 26개 턴이다.
- [이전 C# 도입안](CSharpSongSelectionAdoption.md)
- [Main.cpp](../src/Main.cpp), [SongLibrary.cpp](../src/SongLibrary.cpp), [SongGrouping.h](../src/SongGrouping.h), [SongPreview.h](../src/SongPreview.h), [PreviewTiming.h](../src/PreviewTiming.h), [BrowseBackdrop.h](../src/BrowseBackdrop.h)
- [BmsParser.cpp](../src/BmsParser.cpp), [BmsParser.h](../src/BmsParser.h), [SystemResources.cpp](../src/SystemResources.cpp), [CMakeLists.txt](../CMakeLists.txt)
- [UiTests.cpp](../tests/UiTests.cpp), [SongGroupingTests.cpp](../tests/SongGroupingTests.cpp), [LibraryTests.cpp](../tests/LibraryTests.cpp), [DisplayTests.cpp](../tests/DisplayTests.cpp), [DiagnosticsTests.cpp](../tests/DiagnosticsTests.cpp)
- [네이티브 GPU 출력과 곡 선택 복귀](Native-Output-Lifecycle.md), [호환성 구현 기록](BMS-Compatibility-Implementation-2026-10-23.md), [프레임 병목 분석](frame_bottleneck_analysis.md), [Random BGA 수정 기록](Random-Bga-Sync.md)
- Rust 실험 기록: codex/rust-library-backend 브랜치의 docs/Rust-Backend-Experiment.md. 현재 브랜치에 병합된 파일로 취급하지 않는다.

# 설정 창 RmlUi 전환 0단계: 항목 목록과 동작 기준

작성일: 2026-10-26. 상태: 0단계 완료. 설정창 외형·이벤트·저장 로직은 기존 Win32 구현을 유지한다.

## 브랜치와 기준점

- 기존 브랜치: `codex/native-song-selector`, 마지막 통합 준비 커밋 `8766682`.
- `main` 병합 커밋: `3d09b82` (`Merge native song selection and RmlUi improvements into main`).
- UI 실험 브랜치: `codex/rmlui-settings-ui-test`. main 병합 직후부터 분기했다.
- 기준 소스: `src/Main.cpp`의 `loadKeyBindings`, `loadDisplaySettings`, `saveKeyBindings`, `saveDisplaySettings`, `settingsProc`, `settingsControl`, `scrollSettings`, `selectSettingsCategory`, `openSettings`.
- 병합과 무관했던 기존 미커밋 변경은 별도로 보존 후 실험 브랜치에 복원했다. 워크플로 삭제·README·기존 UiTests·TextRasterCache 변경을 임의로 이번 병합 커밋에 포함하지 않았다.

다음 구현은 계획서의 **1단계: 설정 적용 로직과 HWND 분리**부터다. 이번 단계에서는 RML/RCSS 설정 화면, 새 설정 컨트롤러, 공통 RmlUi 런타임을 만들지 않았다.

## 저장·적용 공통 규칙

현재의 단일 원본 상태는 C++ 전역 설정 값이다. UI 컨트롤은 값을 읽고 쓰는 입구이며, 요약 페이지는 별도 저장 상태를 가지지 않는다.

- 콤보박스 변경, 체크박스 클릭, 트랙바 이벤트는 대부분 즉시 적용하고 `saveDisplaySettings()`로 저장한다.
- 슬라이더는 `TB_ENDTRACK`만 저장하는 구조가 아니다. 다수 항목은 이동 중 발생하는 `WM_HSCROLL`마다 저장한다. 끝 이벤트에는 클릭 효과음 등의 추가 동작이 있다.
- 닫기 버튼·Esc·창 닫기는 적용한 값을 되돌리지 않는다. `WM_CLOSE`에서 키 캡처를 취소하고 표시 설정을 저장한 뒤 창을 파괴한다.
- 키 변경은 8개 레인을 모두 확정한 뒤에만 원본 배열과 INI를 갱신한다. 부분 입력 중 취소는 기존 키를 유지한다.
- 설정창 파괴 시 미리보기를 숨기고 HWND 참조를 비우며 메인 창을 다시 활성화하고 곡 목록으로 포커스를 돌리는 코드가 있다.
- 기존 INI의 모르는 섹션/키는 전체 파일 교체로 지우지 않는다. 이번 기준 테스트는 이를 포함한다.

아래의 기본값은 **설정 읽기 함수의 기본값**을 뜻한다. 유효하지 않은 FPS·색상처럼 현재 전역 값을 유지하는 항목은 명시적으로 구분한다. 사용자 설정에 저장된 값과는 무관하다.

## 설정 항목 목록

### 게임플레이

| 항목 / 컨트롤 ID | 저장 위치 | 기본값·범위·표현 | 적용 및 요약 연결 |
| --- | --- | --- | --- |
| BGA 밝기 `1021` | Display/Brightness, BGADarkness | 밝기 0–100, UI 10단위. 구형 Darkness 기본 50에서 밝기 역산; 10단위 반올림 | `gBgaBackplateDirty`; 요약 `1261` |
| 노트 배속 `1013` | Display/SpeedSteps | 기본 4=1.00x; 정수 1단위=0.25x; 읽기/표시 0.50–10.00x | 요약 `1262`, 곡 선택 속도 조작과 같은 값 |
| 노트 높이 `1027` | Gameplay/NoteHeight | 기본 12; 4–32의 짝수로 보정 | 노트 렌더링에 사용 |
| 판정 `1026` | Gameplay/JudgementProfile | 기본 2=NORMAL; BASIC/EASY/NORMAL/HARD=0–3 | 요약 `1263` |
| 게이지 `1029` | Gameplay/GrooveGaugeMode | 기본 0=NORMAL; NORMAL/EASY/HARD=0–2 | 요약 `1265` |
| 판정/콤보 텍스트 Y 위치 `1046` | Gameplay/JudgementTextYOffset | 기본 0; -256–256px | 현재 표시 라벨은 판정/콤보를 함께 언급하지만 변경 값은 JudgementTextYOffset |
| 판정선 위치 `1018` | Display/JudgeLineOffset | 기본 0; 파일 읽기는 -4096–4096 | UI에서는 화면 높이에 따른 동적 범위로 다시 제한; 트랙바 미리보기와 연결 |
| 레인 투명도 `1020` | Display/LaneTransparency | 기본 34; 0–100 | BGA 합성 캐시 무효화 |
| 레인 커버 `1022` | Display/LaneCover | 기본 0; 0–100 | 즉시 표시 갱신 |
| 기어 위치 `1024` | Display/GearPosition | 기본 1; 0–2 | BGA 합성 캐시 무효화 |
| 키 배치 변경 `1010` | KeyBindings/Scratch, Key1–Key7 | 기본 LShift/S/D/F/Space/J/K/L | 8키 완성 시 저장. 좌우 modifier 정규화, 중복/예약 키 검사 |
| 기본 키 복원 `1015` | 위와 동일 | 기본 8키 배열 | 즉시 저장·키 라벨 갱신 |
| 레인별 노트 색 `1100`–`1107` | NoteColors/Lane0–Lane7 | 0–9 팔레트 인덱스. 기본 4,7,2,7,2,7,2,7; 잘못된 인덱스는 현재 값 유지 | owner-draw 색 견본. 8개 설정을 독립 보존 |
| 구분선 `1200`–`1206` | Display/Separator0–Separator6 | 기본 첫 경계만 켬 | 체크박스·즉시 저장 |
| 사용법 `1009` | 없음 | 설명 대화상자 | 저장 설정이 아닌 명령 |

별도 보존 항목: `Gameplay/ComboTextYOffset`은 기본 0, -256–256이며 읽기/저장과 런타임 사용 코드가 있다. `1045`와 `gComboTextYSlider` 선언·이벤트 처리도 있으나 **현재 설정창 생성 코드에는 대응 슬라이더 생성이 없다.** 새 UI에 넣을지, 기존 숨은 호환 값으로 보존할지는 1단계 이후 명시적으로 결정해야 한다. 0단계에서 컨트롤을 추가하지 않았다.

### 그래픽

| 항목 / 컨트롤 ID | 저장 위치 | 기본값·범위·표현 | 적용 및 요약 연결 |
| --- | --- | --- | --- |
| FPS `1012` | Display/FPS | 기본 240; 60/144/240/400/0(무제한, 최대1500)/-1(VSync) | `gFramePacer.reset`; 요약 `1260`. 유효하지 않은 파일 값은 현재 목표 유지 |
| 화면 모드 `1023` | Display/ScreenMode | 기본 0=창, 1=보더리스, 2=전체화면 | UI 순서는 반대: 전체화면/보더리스/창. 요약 `1264`; `applyScreenMode(true)` 호출 |
| 해상도 `1048` | Display/ResolutionWidth, ResolutionHeight | 기본 0/0=자동; 둘 다 양수일 때 설정됨 | 모니터 제공 해상도 목록. 창 생성 시 목록에 없으면 자동으로 되돌림 |
| GPU 사용 `1025` | Display/GpuRendering | 기본: 구형 DirectComposition 값, 그마저 없으면 1 | 렌더러 reset, 실패 플래그/프레임 누적 초기화. GPU 상한·Reflex·DirectComposition UI 가용성 변경 |
| DirectComposition `1273` | Display/DirectComposition | 기본 1 | presenter 설정. GPU 사용이 켜져 있을 때만 표시 |
| GPU 상한 `1043` | System/GpuLimitPercent | 기본 100; 10–100 | GPU 프레임 누적 초기화. GPU 사용 여부에 따라 UI 비활성화 |
| 노트 서브픽셀 `1270` | Display/NoteSubpixel | 기본 1 | 즉시 저장·그리기 갱신 |
| 노트 안티앨리어싱 `1271` | Display/NoteAntialias | 기본 1 | 즉시 저장·그리기 갱신 |
| 저지연 기능 `1272` | Display/NvidiaReflex | 기본 0; 끔/NVIDIA Reflex | GPU·드라이버 지원·세션 상태로 UI 활성화 결정. 끄면 저지연 모드 해제 요청 |

화면 모드/해상도 변경은 판정선 위치 범위도 갱신한다. 파일의 Display/ScreenMode 값과 콤보 인덱스를 혼동하면 반대 모드로 바뀐다. 해상도 ID `1048`은 다른 창의 연습 실행 ID와 숫자가 같으므로, 후속 명령 ID는 창/도메인 구분 없이 숫자만 전역 재사용하지 않는다.

### 사운드

| 항목 / 컨트롤 ID | 저장 위치 | 기본값·범위 | 적용 |
| --- | --- | --- | --- |
| 마스터 `1030` | Sound/MasterVolume | 100; 0–100 | 믹서 볼륨 갱신 |
| 노래 `1031` | Sound/MusicVolume | 100; 0–100 | 믹서 볼륨 갱신 |
| 키음 `1032` | Sound/KeyVolume | 100; 0–100 | 믹서 볼륨 갱신 |
| 효과음 `1033` | Sound/SfxVolume | 100; 0–100 | 믹서 볼륨 갱신 |
| 버퍼 `1034` | Sound/BufferFrames | 256; 32/64/128/256/512/1024 | 버퍼 프레임 변경·대기 화면 효과음 출력을 다시 시작 |
| 곡 미리보기 `1039` | Sound/SongPreview | 기본 1; boolean | `updateBrowseMedia`로 중단/선택 상태 갱신 |
| 미리보기 볼륨 `1040` | Sound/PreviewVolume | 50; 0–100 | 미리보기 tick 및 즉시 저장; 마스터 볼륨도 영향 |

### 기타

| 항목 / 컨트롤 ID | 저장 위치 | 기본값·범위 | 적용 |
| --- | --- | --- | --- |
| 곡 선택 배경 `1035` | Browse/Background | **읽기 기본값 2=타이틀**; 0회색/1BGA/2타이틀/3사용자 | 미디어 상태 변경·그리기 갱신 |
| 배경 불러오기 `1036` | Browse/CustomBackground | 기본 빈 파일명 | 파일 선택기; Assets에 복사한 이름만 저장, 경로 입력은 filename으로 제한 |
| 배경 블러 `1037` | Browse/Blur | 20; 0–100 | 미리보기 tick·저장·그리기 갱신 |
| 배경 불투명도 `1038` | Browse/Opacity | 10; 0–100 | 위와 동일 |
| 자원 보기 `1044` | System/ShowUsage | 0; boolean | 자원 오버레이와 플레이 FPS/BPM/배속 표시가 같은 값을 사용 |
| CPU 성능 우선 `1047` | System/CpuPerformanceMode | 0; boolean | 게임 처리 스레드 우선순위 변경. 실패하면 체크/값 복구, 성공할 때 저장 |
| CPU 상한 `1041` | System/CpuLimitPercent | 100; 10–100 | 처리 루프 대기 정책에 사용 |
| 메모리 상한 `1042` | System/MemoryLimitPercent | 100; 10–100 | 적용 시 시스템 자원 코드 호출. 실패하면 이전 값·슬라이더로 복구하고 이유 표시 |

배경 읽기 기본값이 2라는 점은 과거 문서의 ‘회색 기본’ 설명과 다르다. 후속 UI가 이를 조용히 0으로 바꾸면 새 설치 동작이 변하므로, 현재 코드값을 기준으로 기록했다. 메모리 상한과 CPU/GPU 퍼센트는 모두 같은 종류의 제한이 아니며 새 UI에서 동일한 보장으로 설명하지 않는다.

### 요약·공통 명령과 비설정 데이터

| 항목 | 처리 |
| --- | --- |
| 요약 FPS/BGA/배속/판정/화면/게이지 `1260`–`1265` | 원본 콤보 선택 갱신 → 원본 설정의 동일 처리 → 요약 값 동기화·저장. 독립 INI 키 없음 |
| 카테고리 `1060`–`1064` | 게임플레이0/그래픽1/사운드2/기타3/요약4; 화면상의 순서는 요약→게임플레이→그래픽→사운드→기타 |
| 닫기 `1014`, IDOK/IDCANCEL | WM_CLOSE. 값 되돌림이 아닌 저장 후 닫기 |
| 상태/키 설명 `1016`/`1017` | 읽기 전용 표시 |
| Library/LastFolder | 설정창 항목은 아니지만 같은 INI에 존재. 이번 테스트에서 보존 확인 |
| 테스트 전용 Phase0Unknown/PreserveMe | 알 수 없는 키 보존 확인용이며 제품 설정이 아님 |

테스트 fixture는 저장 설정 **60개 키**와 위의 보존용 **2개 키**, 총 62개를 포함한다. 가족 항목인 8키·8색상·7구분선은 각각 개별 키로 센다.

## 창·입력·포커스의 기준

| 구분 | 현재 기준 | 확인 방식 |
| --- | --- | --- |
| 창 열기 | 별도 모달 성격 HWND, 폭780·높이 min(820, 작업영역높이-30), 시작 요약 탭, 메인 창 비활성화 | 소스 확인. 테스트는 동일 settingsProc의 격리된 숨김 창 생성 |
| 최소 크기 | 780×400 | WM_GETMINMAXINFO 테스트 |
| 본문 | 상단96px 아래 전용 자식 HWND, 하단 닫기·상태 영역 고정 | 소스/생성 컨트롤 목록/레이아웃 캡처 |
| 본문 높이 기준 | 게임플레이1570, 사운드700, 기타820, 그래픽·요약530 | scrollSettings 소스 확인 |
| 카테고리 진입 | 해당 페이지와 공통 컨트롤만 표시; 스크롤0으로 초기화 | 5개 탭 전체 테스트 |
| 휠·키 탐색 | 휠로 스크롤·드롭다운 닫기; Tab은 IsDialogMessageW, 포커스 컨트롤이 본문 밖이면 자동 노출 | 소스 확인. 물리 키보드 수동 조작/DPI별 평가는 후속 단계 |
| 닫기/재진입 | 변경 즉시 보존, owner 활성화, 곡 목록으로 포커스 복구, 재진입 요약 탭 | 닫기·재열기·owner 활성화 테스트. 활성 데스크톱 포커스 이동 자체는 숨김 창 테스트의 보장 범위 밖 |
| 키 캡처 | 8키 완료 시 저장, 키 해제 대기, 예약/중복 키 거부, Esc·비활성화·닫기 취소 | 부분 캡처 후 비활성화 시 기존 배열 유지 테스트; 세부 키 정책 소스 확인 |
| 미리보기 | 판정선 트랙바 등 기존 별도 오버레이, 카테고리 변경/닫기에 숨김 | 소스 확인. 현재 단계에서 장치나 미리보기 렌더러 구조 변경 없음 |

실제 생성된 입력 컨트롤 수: 게임플레이28, 그래픽9, 사운드7, 기타8, 요약6, 공통6. 정적 텍스트는 제외했다. 공통6은 카테고리5개와 닫기 버튼이다. 저장값과 입력 컨트롤 수는 다른 개념이다.

## 재현 가능한 기준 테스트

추가 파일:

- `tests/fixtures/SettingsPhase0.ini`: 개인 INI에서 가져오지 않은 합성 설정.
- `tests/SettingsPhase0Tests.cpp`: 기존 Main.cpp 설정 함수와 Win32 설정 창을 그대로 호출하는 전용 테스트.
- `CMakeLists.txt`: `BmsSettingsPhase0Tests` 등록. 제품 실행 경로/기능은 변경하지 않음.

테스트는 fixture를 빌드 출력 아래 `settings-copy-<PID>.ini`로 복사해 사용한다. 62개 값의 읽기/저장 왕복, 다섯 탭의 표시, 요약→원본 배속 동기화, 즉시 저장, Esc 성격의 IDCANCEL 닫기 후 값 유지, 재열기, 부분 키 입력 취소, 구형 Darkness/DirectComposition 호환을 확인한다.

구형 파일 예: Darkness=25·Brightness 없음 → 밝기75를 10단위 반올림하여 Brightness=80/Darkness=20. GpuRendering이 없고 DirectComposition=0이면 GPU도 꺼진다. 이 정규화는 손실 없는 텍스트 복사와 구분하며, 현재 동작을 그대로 기준으로 삼는다.

```powershell
cmake -S . -B build-rmlui-song-list
cmake --build build-rmlui-song-list --config Release --target BmsPlayer BmsSettingsPhase0Tests BmsRmlSongListTests BmsRmlSongListHostTests
ctest --test-dir build-rmlui-song-list -C Release --output-on-failure -R "BmsSettingsPhase0Tests|BmsRmlSongList.*Tests"
```

기준 자료는 `build-rmlui-song-list/Validation/SettingsPhase0/`에 생성된다.

- `native-controls.tsv`: 실제 생성된 컨트롤의 카테고리·ID·클래스·라벨.
- `category-0.png`–`category-4.png`, `gameplay-small-scrolled.png`: 숨김 창의 WM_PRINT 기반 레이아웃 자료. 일부 네이티브 콤보박스 값/타이틀은 캡처에 완전히 나오지 않을 수 있으며 값 자체는 메시지/INI 검사로 확인한다.
- 테스트 복사 INI 및 구형 INI: 실행별 재현 자료. 개인 설정·곡 폴더를 사용하지 않는다.

검증 결과: Release 기본 BmsPlayer.exe 빌드, 새 기준 테스트와 RmlUi 목록 렌더링/호스트 테스트를 확인한다. 메모리 정보 조회·GPU/Reflex 지원은 실행 환경에 따라 달라질 수 있어, 장치 적용 성공이나 전체화면 전환·음원 출력 품질은 이 테스트의 합격 근거에 넣지 않는다. 이번 테스트는 오디오 장치를 시작하거나 화면 모드를 전환하지 않는다.

## 1단계에서 우선 분리할 경계

1. 값만 받는 단순 설정(자원 보기, 퍼센트 볼륨 등)의 검증·저장. 기존 Win32 컨트롤은 계속 사용한다.
2. 요약과 원본의 단일 상태/명령 매핑. `HWND`를 모델의 저장소로 쓰지 않는다.
3. 메모리·CPU·장치 적용 실패 시 최종 실제 값과 오류 반환.
4. RmlUi 전역 Initialize/Shutdown의 공통 수명 분리는 계획서 2단계에서 진행한다.

이번에 발견한 ComboTextYOffset UI 부재, 읽기 기본 배경과 문서의 차이, 화면 모드 역순, 동적 판정선 범위는 기능 수정으로 끼워 넣지 않고 후속 작업 판단 항목으로 남겼다.

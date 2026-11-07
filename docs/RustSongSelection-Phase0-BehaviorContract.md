# Rust 곡 선택 모델 이전 — 0단계 동작 계약

## 범위와 결론

이 문서는 [`RustSongSelectionMigrationRoadmap.md`](RustSongSelectionMigrationRoadmap.md)의 0단계 산출물이다. 현재 활성 소스에서 곡 선택 모델과 기존 BMS 처리 경계, 포팅 전에 유지해야 할 동작을 기록하고 C++ 회귀 테스트를 추가한다. Rust 선택 모델, Rust 빌드 통합, FFI, 화면 교체는 아직 구현하지 않는다.

현재 활성 체크아웃 `codex/native-song-selector`의 곡 로딩과 선택기는 C++ 경로다. 다만 별도 Rust parser-helper 실험 소스가 발견되어 이 문서에서 현재 활성 구현과 구분해 기록한다.

## 현재 파서/Rust 실험 상태

### 활성 체크아웃

- `src/BmsParser.cpp`가 BMS 파일을 읽고 헤더·채널 데이터를 C++ `Chart`로 해석한다.
- `src/BmsonParser.h`와 `src/JsonValue.h`도 현재 활성 C++ 소스다.
- 활성 `CMakeLists.txt`에는 Rust 언어, Cargo, Rust backend target이 없다.
- `src/SongSelection.cpp`는 이 C++ `Chart` 목록을 입력으로 받아 그룹과 선택 상태를 구성한다.

### 별도 실험 스냅샷

`build-rust-study/source`는 `.gitignore`의 `/build*/` 규칙에 포함되는 별도 소스 스냅샷이다. `build-rust-on/CMakeCache.txt`는 이 스냅샷을 CMake source로 가리킨다. 여기에는 `BMS_USE_RUST_BACKEND` 옵션, `rust/bms_backend` crate, `src/RustBackend.cpp`, `tests/RustBackendTests.cpp`가 있다.

해당 실험은 Rust를 다음 helper 작업에 사용한다.

| Rust 작업 | C++ 연결 지점(실험 스냅샷) | 범위 |
|---|---|---|
| 줄 나누기 | `BmsParser.cpp`의 `splitLines` | 파서 입력 전처리 |
| JSON 값 읽기 | `JsonValue.h` / `RustBackend.cpp` | JSON 토큰/값 구문 분석 |
| 차트 경로 검색 | `SongLibrary.cpp`의 `discoverCharts` | 지원 확장자 파일 탐색 |
| 곡 제목 정규화 | `SongGrouping.h`의 `songTitleWithoutChartLabel` | 곡 그룹용 제목 처리 |

이 스냅샷에서도 BMS 명령과 이벤트를 기존 `Chart` 구조로 조립하는 실질적인 `BmsParser.cpp` 로직은 C++에 남아 있다. 그러므로 “Rust 파서 도입”은 Rust helper를 기존 파서 경로에 연결한 실험을 뜻하며, 현재 활성 브랜치에서 Rust가 전체 BMS parser를 대체한다는 뜻은 아니다. 이전 구현이 이 backend를 다시 가져오려면 먼저 어떤 helper를 Rust로 유지할지 확인하고, 선택 모델은 파서 내부 자료형이 아니라 C++가 완성한 `Chart` 요약값에 연결한다.

## 곡 선택 데이터 흐름

```text
C++ BmsParser / SongLibrary
          │ std::vector<Chart>
          ▼
SongSelectionModel::rebuild
          │ SongGroup 및 chart index
          ├─ Win32 ListView 입력/행 구성
          ├─ GDI drawBrowseScreen 표시
          ├─ 미리 듣기 채보/미디어 선택
          └─ 플레이 시작에 chart index 전달
```

활성 소스에서 주요 연결은 다음과 같다.

- 모델 API와 상태: `src/SongSelection.h`, `src/SongSelection.cpp`
- 그룹 ID, 제목 정규화: `src/SongGrouping.h`
- 채보 레벨 정렬: `src/ChartOrdering.h`
- 공유 미리 듣기 시점: `src/PreviewTiming.h`
- 화면, ListView 및 모델 호출: `src/Main.cpp`
- 기존 그룹화 보조 테스트: `tests/SongGroupingTests.cpp`
- 이번 단계에서 추가한 모델 계약 테스트: `tests/SongSelectionTests.cpp`, CMake target `BmsSongSelectionTests`

`SongSelectionModel`은 `std::vector<Chart>`의 인덱스를 저장한다. 따라서 차트 목록을 교체할 때 기존 선택 인덱스는 같은 catalog에만 유효하다. 향후 Rust에 전달하는 선택 데이터에도 이 수명 경계를 유지해야 하며, 재검색/재구성 후 예전 인덱스를 재사용하면 안 된다.

## 보존해야 할 동작

| 영역 | 현재 계약 |
|---|---|
| 그룹 경계 | `songGroupIdentity`는 차트 디렉터리를 정체성으로 쓴다. 동일 디렉터리의 차트는 제목·작곡가·키 모드가 달라도 한 그룹이며, 같은 제목도 디렉터리가 다르면 다른 그룹이다. |
| 대표 제목 | `canonicalSongTitles`가 채보 표기/형제 차트 제목을 정규화한다. 그룹의 표시 제목은 해당 결과를 사용한다. |
| 그룹 정렬 | 기본 오름차순이다. 라틴 문자, 한글, 기타 문자 범주 순으로 정렬한다. 같은 범주에서는 제목, 작곡가, 대표 차트 모드, 디렉터리를 비교한다. 정렬은 stable sort다. |
| 문자 비교 | 라틴 범주는 `CompareStringOrdinal` 대소문자 무시 비교를 쓴다. 한글 범주는 `ko-KR`, 기타 범주는 `ja-JP` Windows locale 비교를 쓴 뒤 소문자화 결과를 대체 비교로 사용한다. Rust 표준 문자열 순서로 그대로 바꾸면 결과가 달라질 수 있다. |
| 채보 변형 순서 | 플레이 가능한 차트를 먼저 둔다. 다음으로 숫자로 완전히 해석되는 `#PLAYLEVEL`을 오름차순 정렬하고, 자막 범주/문자 비교와 경로 비교를 tie-breaker로 사용한다. |
| 선택 상태 | 선택 그룹은 행 번호가 아니라 디렉터리 문자열로 보존한다. 그룹 정렬 후에도 같은 그룹과 현재 변형을 선택 상태로 찾는다. `clearSelection`은 모델 그룹은 보존하고 선택만 지운다. |
| 초기 변형 | 그룹의 `selectedVariant` 기본값은 0이며, 모델 정렬상 첫 채보가 선택된다. 그룹이 없거나 선택이 없으면 선택된 그룹/차트 인덱스는 -1이다. |
| 변형 이동 | 두 개 이상 변형인 선택 그룹에서 UI 호출 방식 `delta = -1` 또는 `+1`은 양 끝을 순환한다. 변형이 하나뿐이거나 선택이 없으면 false를 반환한다. |
| 정렬 토글 | `toggleSort`는 오름차순/내림차순을 뒤집는다. `clear()`와 `rebuild()`는 정렬 방향을 기본값으로 되돌리지 않는다. |
| 미리 듣기 시점 | 그룹 내부 정렬 순서에서 유효한 명시적 미리 듣기 시점이 있는 첫 차트를 대표로 선택한다. 대표의 시작/길이를 같은 그룹의 차트들에 반영한다. 이 데이터 변경도 `rebuild`의 현재 side effect다. |
| 잘못된 그룹 인덱스 | `selectGroup`은 범위 밖 인덱스에서 false를 반환하고 이전 선택을 유지한다. |

### 확인이 더 필요한 API 입력

`cycleSelectedVariant(int delta)`의 화면 호출은 `-1` 또는 `+1`이다. 현재 구현의 모듈로 계산은 임의의 큰 음수 `delta`를 일반적으로 정규화하지 않는다. 0단계에서는 이 구현 세부를 안정된 계약으로 승격하지 않는다. 후속 API는 `Previous`/`Next`처럼 두 방향만 표현하거나, 모든 signed delta를 안전하게 정규화할지 정해야 한다.

## 테스트 기준선

기존 `SongGroupingTests`는 제목·그룹 정규화 helper를 검증하지만 `SongSelectionModel`의 전체 상태 전이를 직접 테스트하지 않는다. 새 `BmsSongSelectionTests`는 합성 차트만 사용해 다음을 검증한다.

- 빈 모델, 그룹 수와 폴더 경계, 키 모드가 다른 동일 폴더 차트 병합
- 그룹 오름차순/내림차순과 기본 그룹 방향
- 플레이 가능 우선, 숫자 레벨 순서, 선택한 그룹의 두 변형 이동 및 양방향 순환
- 한 개 변형 그룹의 이동 거부와 범위 밖 선택의 no-op
- 정렬 후 디렉터리 기준 선택 및 현재 채보 유지
- `clearSelection`, `clear`, `rebuild`와 정렬 방향 지속
- 유효한 명시적 미리 듣기 채보 대표 선택 및 그룹 내 타이밍 공유
- 라틴/한글/기타 문자 분류와 현재 Windows 문자 비교의 대표 사례

테스트는 개인 곡 폴더를 읽거나 수정하지 않는다. 이 기준선은 현재 C++ 동작을 고정하기 위한 것이며 Rust와의 동등성은 이후 shadow 단계에서 별도로 측정한다.

## 0단계 완료 조건

- Rust parser/helper 실험이 활성 소스가 아닌 별도 스냅샷이라는 점과 실제 작업 범위가 기록되었다.
- 모델 경계와 기존 UI/플레이어의 의존성이 기록되었다.
- Rust 이전 때 호환해야 할 입력, 정렬, 선택, 미리 듣기 동작이 문서화되었다.
- `BmsSongSelectionTests`가 현재 C++ 구현에서 빌드되고 통과한다.
- Rust용 CMake 옵션, Rust crate, FFI, 런타임 경로, 화면 및 parser 변경은 수행하지 않는다. 이 단계의 CMake 변경은 C++ 기준선 테스트 target뿐이다.

## 검증 결과

- 새 Release 빌드 디렉터리 `build-phase0-songselection`에서 Visual Studio 18 2026 x64 구성을 생성했다. 이 검증 구성은 NVIDIA Reflex 옵션을 끄고 프로젝트를 구성했다.
- `BmsSongSelectionTests` 및 기존 `BmsSongGroupingTests`를 빌드했다.
- `ctest --test-dir build-phase0-songselection -C Release -R "BmsSong(Grouping|Selection)Tests" --output-on-failure` 결과: 2/2 통과.
- 전체 프로젝트 테스트는 실행하지 않았다. Phase 0에서 추가한 계약과 직접 관련된 모델/그룹화 테스트만 확인했다.

# Rust 곡 선택 FFI 계약 v2

## 범위와 책임 분담

Rust가 곡 선택 모델의 그룹화, 차트 변형 순서, 그룹 정렬, 선택 상태, 변형 순환을 소유한다. Win32 ListView와 화면 그리기, 키 입력, 미리 듣기 시점 공유, 플레이 시작은 C++에 남는다. C++ 어댑터는 `Chart`에서 표시/정렬용 값을 계산해 Rust에 넘기고, Rust 모델 결과를 기존 UI가 쓰는 `SongGroup` 값 목록으로 복사한다. `Main.cpp`는 Rust ABI를 직접 호출하지 않는다.

헤더는 `rust/song_selection/include/BmsSongSelectionAbi.h`, 구현은 `rust/song_selection/src/lib.rs`이다. ABI는 Windows x64 MSVC 및 `x86_64-pc-windows-msvc`를 대상으로 하며 C 레이아웃과 고정 폭 정수를 사용한다. Rust 모델은 불투명 핸들이고 C++의 `Chart`, `std::wstring`, `std::filesystem::path` 또는 내부 포인터를 보관하지 않는다.

C++는 기존 동작을 보존하기 위해 Rust 호출 전에 차트 제목 표준화, 정규화된 폴더 경로, 영문/한글/기타 제목 분류, 정확한 숫자 `#PLAYLEVEL` 값을 계산한다. Rust는 제공된 값을 이용해 그룹과 순서를 만들고, Windows `CompareStringOrdinal`/`CompareStringEx`를 호출해 기존 로캘 비교 규칙을 유지한다. C++ UI 어댑터는 그룹에 대한 미리 듣기 타이밍 공유처럼 차트 객체를 수정하는 기존 부수 동작을 적용한다.

## 차트 입력

`BmsSongChartSummaryInput`은 카탈로그 차트 하나의 값을 복사해 전달한다.

| 필드 | C ABI 형식 | 의미 |
| --- | --- | --- |
| `chart_id` | `uint64_t` | 입력 안에서 0이 아니고 유일한 ID. C++ 어댑터는 차트 인덱스+1을 사용 |
| `path` | UTF-8 slice | 차트 파일 경로 |
| `title` | UTF-8 slice | 표준화된 곡 제목 |
| `subtitle` | UTF-8 slice | 난이도/부제 |
| `artist` | UTF-8 slice | 작곡자 |
| `play_level` | UTF-8 slice | 원본 `#PLAYLEVEL` 텍스트 |
| `directory` | UTF-8 slice | 그룹 정체성으로 쓰는 정규화 폴더 경로 |
| `mode` | `uint32_t` | `Unknown=0`, `FiveKey=1`, `SevenKey=2`, `TenKey=3`, `FourteenKey=4`, `PopnNine=5`, `Other=6` |
| `playable` | `uint32_t` | `0` 또는 `1` |
| `title_group` | `uint32_t` | 제목 분류: Latin `0`, Hangul `1`, 기타 `2` |
| `subtitle_group` | `uint32_t` | 부제 비교용 동일 분류 |
| `has_numeric_play_level` | `uint32_t` | 정확한 유한 숫자 레벨이 있으면 `1`, 없으면 `0` |
| `numeric_play_level` | `double` | 정확한 숫자 `#PLAYLEVEL`; 미지정이면 무시 |

그룹 정체성은 차트 폴더이다. 그룹 표시는 첫 차트의 표준 제목, 작곡자, 모드를 사용한다. 차트 변형은 플레이 가능 우선, 정확한 숫자 레벨 오름차순, 부제 로캘 정렬, 파일 경로 정렬 순이다. 그룹은 제목 분류, 제목, 작곡자, 모드, 디렉터리 순으로 정렬한다. 내림차순은 그룹 비교 결과를 뒤집으며 차트 변형 순서는 유지한다. 선택은 폴더 경로로 보존되며 변형 순환은 플레이 불가 차트도 포함한다.

## ABI와 수명

- 모든 텍스트 포인터는 NUL 종단을 요구하지 않는 UTF-8 바이트 slice이며 `length_bytes`가 바이트 수다. 빈 문자열의 포인터는 무시한다. Rust는 입력을 검증한 뒤 소유 `String`으로 복사한다.
- 모델 핸들은 `model_create`로 생성하고, 같은 소유 스레드에서 API를 직렬 호출한 뒤 `model_destroy`로 정확히 한 번 파기한다. 동시 사용/파기는 지원하지 않는다.
- 카탈로그를 다시 만들 때마다 0이 아닌 증가 `generation`을 넘긴다. 성공한 `model_rebuild`는 입력 전체를 복사하고 그룹을 만든 후 모델 상태를 교체한다. 실패하면 이전 Rust 모델은 유지된다.
- 그룹/차트 결과 조회는 값 또는 caller-owned 출력 버퍼를 사용한다. Rust 메모리를 C++의 `free`/`delete`로 해제하지 않는다.
- `model_clear`는 카탈로그와 선택을 비우고 정렬 방향은 유지한다. `model_sort`는 방향을 유지해 다시 정렬하며 `model_toggle_sort`는 방향을 바꾼다.
- `model_select_group`, `model_clear_selection`, `model_cycle_selected_variant`가 선택을 갱신한다. 인덱스는 해당 generation의 카탈로그 결과에만 유효하다.
- 텍스트 복사 API는 필요한 크기를 먼저 보고한다. 출력 버퍼가 짧으면 일부 복사 없이 `BUFFER_TOO_SMALL`을 반환한다. 빈 문자열은 용량 0에서 성공한다.

입력 배열/문자열과 출력 저장소는 Rust 핸들 또는 서로 겹치면 안 된다. C ABI는 포인터가 실제로 유효한 메모리인지 검사할 수 없으므로, 읽기/쓰기 가능 영역과 배열 정렬은 호출자 책임이다. Rust panic은 unwind 가능한 경우 `PANIC`으로 변환하며, 프로세스 abort나 할당 실패는 복구하지 못한다.

## 상태 코드

| 값 | 이름 | 의미 |
| ---: | --- | --- |
| 0 | `OK` | 성공 |
| 1 | `INVALID_ARGUMENT` | 필수 포인터, generation, mode 또는 필드 값이 유효하지 않음 |
| 2 | `INVALID_UTF8` | 입력 문자열이 유효한 UTF-8이 아님 |
| 3 | `DUPLICATE_CHART_ID` | 입력 안에서 차트 ID 중복 |
| 4 | `INDEX_OUT_OF_RANGE` | 그룹 또는 변형 인덱스 범위 초과 |
| 5 | `BUFFER_TOO_SMALL` | 출력 용량 부족; 필요한 길이는 출력 매개변수에 기록 |
| 6 | `LENGTH_OVERFLOW` | 길이 또는 인덱스를 ABI 고정 폭 타입으로 표현할 수 없음 |
| 255 | `PANIC` | 복구 가능한 Rust panic |

## 검증

`BmsSongSelectionTests`는 Rust 어댑터가 노출하는 기존 C++ 계약을 검증한다: 동일 폴더 그룹, 플레이 가능/레벨/부제 순서, 첫 차트 메타데이터, 미리 듣기 타이밍 공유, 선택 유지, 정렬 전환, 변형 순환, 영문/한글/일본어 비교. `BmsRustSongSelectionAbiTests`는 Release의 C++/Rust ABI를 직접 연결해 입력 검증, UTF-8 복사, 범위 오류, 구조체 크기/정렬 계약을 확인한다.

CMake 기본 설정은 Rust 모델을 사용한다. Debug는 Rust 정적 라이브러리의 `/MT`와 C++ Debug의 `/MTd` CRT가 달라 기존 C++ 모델을 컴파일하며, Release 등 비-Debug 구성은 Rust 모델을 연결한다. `BMS_ENABLE_RUST_SONG_SELECTION=OFF`로 C++ 모델을 명시적으로 선택할 수도 있다.

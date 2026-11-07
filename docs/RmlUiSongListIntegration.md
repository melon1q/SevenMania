# RmlUi 곡 목록 표시 도입

2026-10-25. Rust 곡 선택 모델을 유지하면서 곡 목록의 표시 계층에 RmlUi를 도입했다. 기본 CMake 설정은 `BMS_ENABLE_RMLUI_SONG_LIST=ON`이다.

## 적용 범위

- `Assets/UI/song-list.rml`: 목록 문서와 행 컨테이너.
- `Assets/UI/song-list.rcss`: 선택 행, 난이도 색상, 플레이 기록 표시, 곡명/작곡가/변형 번호의 레이아웃.
- `src/RmlSongListRenderer.cpp`: RmlUi 수명, Windows UTF-8 파일 입력, 시스템 글꼴, DirectX 11 렌더링과 비트맵 캐시.
- `src/Main.cpp`: ListView의 현재 보이는 행과 실제 행 좌표를 값으로 전달하고 `WM_PAINT` 및 화면 캡처에서 RmlUi 결과를 표시.

Rust가 그룹화·정렬·선택·변형 순환을 소유한다. 기존 ListView가 키보드·마우스 입력, 포커스와 스크롤을 처리하므로 RmlUi에 별도 선택 상태를 만들지 않았다. 곡 정보 패널과 게임 화면은 현재 C++ 화면을 사용한다. Lua 등 스크립트 플러그인은 사용하지 않는다.

2026-10-26: 곡 선택 화면의 설정·곡 폴더 열기·다시 검색 버튼을 삭제하고 각각 Space·F2·F5 단축키로 연결했다. 세 단축키는 하단 Enter/Shift+Enter 플레이 안내와 같은 줄에 표시한다. 목록이나 남아 있는 숨김 파일 버튼에 포커스가 있어도 작동하며, 키 반복 입력으로 대화상자/검색이 중복 실행되지 않는다. 플레이·리절트·설정·연습 창에서는 곡 선택 단축키를 실행하지 않는다.

## 렌더링과 자원

보이는 행만 문서에 만들며 전체 곡 수만큼 DOM을 생성하지 않는다. 텍스트는 RML 특수 문자를 escape해 곡 메타데이터가 문서 구조로 해석되지 않게 한다. 행 좌표·크기·선택·표시 문자열·기록이 달라질 때 RmlUi 문서와 GPU 출력을 갱신한다. 변화가 없으면 캐시한 비트맵을 GDI로 복사한다.

2026-10-26: 목록 문서의 남색 바탕을 제거했다. RmlUi 출력은 투명한 premultiplied-alpha DIB로 캐시하고, 해당 목록 위치의 원래 곡 선택 배경을 먼저 그린 뒤 `AlphaBlend`로 합성한다. 선택된 행의 강조색은 유지한다. 부모 배경이 갱신되면 목록도 다시 합성하므로 빈 영역에서 정지 이미지·동영상 배경이 이어진다. 배경 갱신만으로 RmlUi geometry를 다시 만들지는 않는다.

2026-10-26: 위 배경 합성을 화면 DC에서 직접 실행하면 곡 전환 시 배경만 칠해진 중간 상태가 노출될 수 있었다. `BeginBufferedPaint`의 메모리 DC에 배경과 RmlUi 행을 모두 그린 후 성공했을 때만 `EndBufferedPaint`로 출력한다. 렌더링 실패 시에는 중간 버퍼를 폐기하고 기존 목록 표시로 전환한다. 메인 창의 생성/파괴에서 BufferedPaint를 초기화/해제하며, `WM_PRINTCLIENT` 화면 캡처도 같은 합성 경로를 사용한다. 호스트 테스트는 선택을 12회 왕복하며 선택별 출력 일치와 동일 상태 재출력의 픽셀 일치를 확인한다. 실제 모니터에서의 깜빡임 소멸 여부는 자동 픽셀 비교만으로 확정하지 않는다.

현재 GDI 기반 목록과 연결하기 위해 RmlUi 공식 DX11 렌더러로 별도 텍스처에 그린 뒤 변경 시에만 CPU로 읽어온다. 스크롤 중에는 이 readback 비용이 있으므로 GPU 직접 합성보다 빠르다고 보장하지 않는다. 실제 대형 목록의 스크롤 시간을 측정한 뒤 출력 계층을 더 옮길지 판단한다. 플레이 중에는 목록을 그리지 않는다.

Windows의 Malgun Gothic 또는 Segoe UI를 기본으로 읽고, 있으면 Meiryo/Segoe UI Symbol을 대체 글꼴로 등록한다. 글꼴 파일은 배포물에 복사하지 않는다. RmlUi는 종료 전에 geometry/글꼴 텍스처를 해제하고 그 뒤 DX11 장치와 렌더러를 파기한다. 초기화·글꼴·문서·GPU 출력이 실패하면 현재 프로세스의 목록 표시를 기존 GDI 경로로 돌린다.

## 빌드 및 디자인 수정

2026-10-26: 긴 곡 정보는 `TextMarquee.h`의 공통 시간 계산으로 스크롤한다. 왼쪽 제목·장르·작곡가·BPM과 선택된 목록 행의 제목에 적용하며, 영역에 들어가는 문자열과 선택되지 않은 행은 움직이지 않는다. 처음 1.2초 대기한 뒤 초당 48px로 왼쪽 이동하고 마지막 글자까지 노출한 상태에서 1.5초 대기한 뒤 시작 위치로 돌아간다. 선택 채보 경로가 변경되면 시간을 초기화한다. 정보 패널은 원래 영역에 클리핑하고, 목록은 글꼴 엔진으로 원문 너비를 측정해 배너·변형 번호 영역을 침범하지 않는다.

스크롤이 필요한 동안 약 33ms 간격으로 갱신하므로 동영상 배경과 오디오 미리보기가 꺼져도 동작한다. 목록 문서는 데이터 변경 시에만 재구성하고 스크롤 중에는 제목 위치만 갱신하며, 대기 구간처럼 정수 픽셀 위치가 같으면 GPU 렌더링 결과를 재사용한다. 테스트는 대기·속도·끝 대기·반복과 실제 RmlUi 출력 이동/원위치 복귀를 확인한다.

의존성은 Git submodule로 고정했다.

| 의존성 | 버전 | 커밋 |
| --- | --- | --- |
| RmlUi | 6.3 | `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` |
| FreeType | 2.15.0 | `37515cf56ba3349656049edbed89ffc6f6466f2d` |

새 checkout은 아래 명령으로 준비한다.

```powershell
git submodule update --init --recursive
cmake -S . -B build-rmlui-song-list
cmake --build build-rmlui-song-list --config Release --target BmsPlayer
```

RML/RCSS는 빌드 후 실행 파일의 `Assets/UI`로 복사된다. 소스 디자인을 수정한 뒤 다시 빌드하고 프로그램을 재시작하면 반영된다. RmlUi를 끄고 비교하려면 `-DBMS_ENABLE_RMLUI_SONG_LIST=OFF`로 구성한다. Rust 모델 옵션과는 독립적이다.

RmlUi는 MIT 라이선스이며, core의 컨테이너 라이선스도 함께 배포한다. Portions of this software are copyright © 1996–2026 The FreeType Project (https://freetype.org). All rights reserved. FreeType은 FreeType License로 사용한다. 관련 라이선스는 실행 파일 옆 `Assets/Licenses`에 복사한다.

## 검증

2026-10-26: 곡 배너를 왼쪽 정보 영역에서 제거하고 각 곡 항목의 오른쪽에 배치했다. PNG/JPEG 등의 이미지는 GDI+로 읽어 비율을 유지한 128×40 투명 썸네일로 만들며, 좁은 목록에서는 96×30으로 표시한다. 보이는 행에서 사라진 배너 텍스처는 해제하고, 파일이 없거나 읽을 수 없으면 투명하게 처리한다. 곡 제목은 80→120px, 장르·작곡가·BPM은 30→48px로 확대했다. 왼쪽/오른쪽 클릭 증감 안내와 상단 MUSIC SELECT·이동/정렬 안내를 제거하고 곡 폴더 경로는 오른쪽 하단으로 옮겼다.

이 변경은 Release 플레이어 빌드와 `BmsDisplayTests`, `BmsRmlSongListTests`, `BmsRmlSongListHostTests`로 검증했다. 배너 테스트는 한글과 특수 문자가 포함된 PNG 경로, JPEG, 누락 파일과 빈 목록 전환을 확인한다. `Validation/RmlUi/browse-layout.png`는 확대된 정보 영역과 하단 배치를 확인하는 합성 화면이다.

실제 DX11/FreeType/RmlUi를 연결한 `BmsRmlSongListTests`는 한글·일본어·영문·RML 특수 문자, 선택 변경, 동일 비트맵 캐시, 320/640/900픽셀 폭, 빈 목록과 반복 초기화/파기를 확인한다. `BmsSongSelectionTests`로 Rust 선택 모델의 기존 동작도 확인한다. 테스트가 생성하는 `Validation/RmlUi/song-list-rmlui.png`는 표시 결과를 눈으로 점검하는 자료다.

`BmsRmlSongListHostTests`는 실제 `Main.cpp`의 창과 ListView를 생성하고 메모리상의 합성 차트를 연결해 키보드 이동, 마우스 행 선택, 정렬 후 선택 유지, 변형 변경, 스크롤, 창 크기 변경과 리절트 배경 캡처를 확인한다. 사용자 설정 파일·곡 폴더를 사용하거나 오디오 장치를 시작하지 않는다.

Release/Debug 플레이어 빌드, Release/Debug 렌더링 테스트, Release ListView 통합 테스트와 Rust 선택 모델 테스트가 통과했다. RmlUi OFF 구성의 Release 플레이어 빌드와 선택 모델 테스트도 통과했다. Debug 의존성 컴파일에서 발생한 MSVC PDB 서버 오류는 샌드박스 밖에서 동일 빌드를 실행해 해결됐으며 소스 오류는 아니었다.

이번 도입의 수동 확인 항목은 실제 라이브러리에서의 긴 스크롤, 마우스 클릭 위치와 표시 행 일치, 난이도 변경, 설정창 포커스 복귀, 플레이/결과/QUIT 복귀다. 기존 해상도 확인은 사용자에게 확인받았으며 새 렌더러에 대해서는 다시 확인해야 한다.

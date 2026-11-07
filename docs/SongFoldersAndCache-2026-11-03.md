# 폴더 선곡 및 메타데이터 캐시 구현 상태

기존 SongLibrary의 검색은 Main.cpp의 로딩 worker에서 실행되며 디렉터리별 최대 4개 parser 작업을 사용한다. 기존 곡 묶음과 실제 채보 인덱스를 유지한 채 SongFolderNavigation을 추가했다.

## 실제 변경
- 폴더 행은 음수 UI ID, 실제 곡 행은 기존 group index다. Enter/더블클릭으로 진입, Backspace/Escape로 상위 복귀한다. 루트는 ALL SONGS와 직접 속한 곡, 하위 폴더를 표시한다. 폴더 이동은 메모리 인덱스만 사용한다.
- ALL SONGS는 기존 전체 곡 묶음을 표시한다. 채보 경로와 기록 키를 바꾸지 않는다. 폴더별 커서와 플레이 후 탐색 위치를 같은 실행 동안 유지한다.
- RmlUi 목록에 [DIR], DIR, [ALL SONGS], [..] UP 기본 라벨을 제공하며 스킨의 새 필수 필드는 없다.
- SongMetadataCache는 Songs/.sevenmania-index의 버전 있는 바이너리 메타데이터 캐시다. 경로/seed/크기/mtime/부모 폴더 mtime를 검증하고 손상되거나 오래된 항목만 기존 parser로 재분석한다. 파일당 임시 파일을 완전히 쓴 뒤 MoveFileEx의 원자적 교체를 사용한다. 쓰기 실패는 곡 로딩을 막지 않는다.
- UI 검색에만 metadataOnly를 켠다. 기존 scanSongFolder 기본 호출 계약은 전체 채보 반환으로 유지한다. 캐시에 노트나 오디오/영상 픽셀은 저장하지 않는다. 최초 분석도 기존 parser를 사용하므로 조건부 명령을 별도 축약 parser로 재해석하지 않는다.
- 미리듣기 worker는 catalogOnly 채보를 재분석한 후 기존 AudioEngine 준비를 사용한다. 플레이 준비는 기존 파일 재분석/리소스 준비를 유지한다.

## 검증
Release BmsPlayer 및 BmsLibraryTests 빌드 성공. BmsLibraryTests 통과: 루트/중첩/ALL/복귀/커서, 기존 캐시 로드, 크기가 바뀐 채보 재분석, 삭제 반영, 손상 복구, 기존 import/취소/긴 경로 테스트.
SevenManiaSkinBindingTests의 기존 기록/스킨 렌더링 회귀도 통과했다.
BmsSongSelectionTests는 variant selection should wrap forward에서 실패했다. 이번 변경은 SongSelection.cpp/SongSelectionRust.cpp/rust의 순환 코드를 수정하지 않았다. 해당 테스트 실패를 해결했다고 보고하지 않는다.
기록 저장 형식과 normalizedRecordPath는 수정하지 않았다. 수동 UI/프로세스 재시작/대규모 라이브러리 프레임 시간은 검증하지 않았다.

## 아직 완료되지 않은 요구사항
- 캐시 목록을 먼저 화면에 표시한 뒤 증분 검증을 계속하는 2단계 UI 게시: 현재 기존 splash/로딩 완료 후 한 번 게시한다. 전체 재분석은 피하지만 파일 목록/속성 확인은 매번 한다.
- 실제 빈 폴더 행: 현재 채보 경로로부터 폴더 트리를 만들므로 채보 없는 폴더는 표시하지 않는다. 비어 있는 Songs는 ALL SONGS만 표시하며 안전하게 동작한다.
- 기존 동일 디렉터리 기반 곡 묶음의 추가 안전성 개선: 같은 폴더의 관련 없는 곡도 기존 규칙에 따라 난이도 묶음이 될 수 있다. 이번에는 Rust/C++ 묶음 계약을 변경하지 않았다. 모든 채보는 기존 난이도 전환으로 접근한다.
- 캐시만 사용한 authored BGA 미리보기의 완전 동등성: 캐시에는 BGA 이벤트가 없다. 실제 플레이는 전체 파일 재분석을 사용하므로 플레이 BGA는 유지한다.
- 수정 시각/크기를 유지한 변경 및 기존 미디어 파일 내용만 교체한 경우는 탐지하지 못한다. 캐시 폴더를 지우면 전체 재구축한다. 삭제된 채보의 .meta 잔여 파일 정리는 아직 하지 않지만 검색 결과에는 나오지 않는다.
- 폴더별 커서의 디스크 영속화와 ROOT SONGS 별도 가상 행은 없다. 루트 직접 곡은 루트 목록에서 표시한다.

따라서 전체 요청 완료가 아니라 폴더 탐색/증분 캐시의 구현 및 검증된 기반이다. 다음 작업은 위 항목을 우선 완료해야 한다.

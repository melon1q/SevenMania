# 곡 폴더 탐색 및 항목 라벨 보완

기존 자동 펼침을 재작성하지 않고 곡 그룹 판별을 보완했다. 실제 채보가 있고 하위에 채보가 없으며 하나의 곡 그룹인 말단 디렉터리만 상위 목록에 펼친다. 여러 독립 그룹의 Misc는 폴더로 유지해 내부에서 각각 표시한다. 혼합/중간 분류/빈 폴더는 유지한다. 기존 그룹 규칙이 모호한 차분을 여러 그룹으로 나눈 경우 폴더로 남는 보수적 대체 동작이다. 파일 이동이나 디렉터리 수정은 하지 않았다.

DIR 중복 원인은 UI 유형 칸 DIR와 제목의 [DIR] 접두사가 동시에 존재한 것이었다. SongEntryPresentation.h로 유형/라벨을 분리하고 제목은 원래 폴더명만 전달한다. RmlUi는 유형 칸에 DIR 또는 ..를 그리며 ALL SONGS에는 별도 유형 문자를 넣지 않는다. native fallback은 유형에 따라 접두사를 한 번만 그린다. 문자열에서 임의로 [DIR]를 찾아 삭제하지 않는다. 상위 이동 바의 청회색/선택 스타일, 일반 폴더의 메탈릭 스타일은 유지했다. 실제 곡 제목/정보/기록은 기존 Chart 데이터와 바인딩을 계속 쓴다.

변경: SongFolderNavigation.h, SongEntryPresentation.h, RmlSongListRenderer.h, Main.cpp 연결부, LibraryTests.cpp. .semaskin 형식과 기록 저장 키, 캐시 포맷, 파서/플레이/판정/오디오/FramePacer는 변경하지 않았다. Main은 기존 그룹 인덱스를 탐색 모듈에 전달하며 실제 폴더와 곡 경로는 그대로다.

검증: Release BmsPlayer.exe 업데이트 성공. BmsLibraryTests/BmsSongSelectionTests/SevenManiaSkinBindingTests 통과. 폴더 트리를 전체 순회해 모든 채보 경로가 정확히 한 번씩 노출됨을 검사했다. 대회→곡, 다중 차분, Misc 여러 곡, 루트 직접 곡, 깊은 유니코드 중간 분류, 혼합/빈 폴더, ALL 접근, 메타데이터 캐시 전후 분류 동일성을 검사했다. 기존 그룹·기록·캐시 검증도 같은 테스트에서 실행됐다.

BmsRmlSongListTests는 기존 제목 스크롤 복귀 검증(song title did not return to its original position)에서 재실행해도 실패했다. 이 작업은 스크롤 계산/renderer.cpp를 수정하지 않았지만 이번 실패의 원인은 확정하지 못했으므로 전체 렌더 회귀 통과라고 보고하지 않는다. 실제 앱 수동 조작, 플레이 진입, 폴더 이동 프레임 타임은 미검증이며 성능 개선 숫자를 제시하지 않는다.

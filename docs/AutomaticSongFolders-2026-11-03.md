# 곡 폴더 자동 표시 및 상위 이동 바

파일 이동/삭제/폴더 재배치는 하지 않는다. SongFolderNavigation의 메모리 인덱스 표시 규칙만 변경했다.

## 판단 기준
채보가 직접 존재하는 디렉터리이며 더 아래 디렉터리에는 채보가 없는 경우 말단 곡 폴더로 판단해 상위 목록에 그 디렉터리의 곡 그룹들을 펼친다. 여러 그룹이면 모두 별도 행을 제공하며 기존 난이도 전환을 유지한다. 채보가 직접 있으면서 하위에도 채보가 있으면 분류 폴더로 유지해 양쪽에 접근한다. 오디오/이미지용 하위 디렉터리 개수는 판정 기준으로 쓰지 않는다. 여러 단계의 분류 폴더와 빈 폴더도 기존 인덱스로 유지한다. ALL SONGS/기록 키/캐시 직렬화/기존 그룹 규칙은 변경하지 않았다. 폴더 진입 때만 메모리로 재분류하며 프레임마다 재분류하거나 디스크를 검색하지 않는다.

## 스타일
SongEntryKind(Song/Folder/ParentFolder/AllSongs)를 값 기반 렌더 행에 추가했다. 기본값 Song으로 기존 호출을 유지한다. RmlUi는 folder-entry/parent-folder/all-songs CSS 클래스를 출력하며 parent-folder만 청회색(#8BA8BA → #456780 → #253B4E) 메탈릭으로 강조한다. 선택 상태는 같은 계열의 밝은 색과 테두리를 사용한다. 하위 폴더는 기존 회색 테마와 DIR 라벨을 유지한다. 네이티브 fallback도 부모 행의 색과 실제 폴더 라벨을 그린다.
폴더 라벨은 인라인 난이도 색상 대신 CSS 색상을 따르므로 독립적인 스타일 변경이 가능하다. .semaskin의 필수 필드나 에디터 저장 형식은 바꾸지 않았다. 현재 폴더 색상은 공유 RCSS로 설정하며 개별 패키지/에디터에서 색상·배경 이미지를 지정하는 신규 속성은 추가하지 않았다.

## 변경 파일
SongFolderNavigation.h, Main.cpp의 행 종류 연결 및 native fallback, RmlSongListRenderer.h/.cpp, Assets/UI/song-list.rcss, LibraryTests.cpp, RmlSongListTests.cpp.

## 검증
Release 기본 BmsPlayer.exe 빌드 성공. BmsLibraryTests, BmsSongSelectionTests, SevenManiaSkinBindingTests, BmsRmlSongListTests 통과. 말단 폴더/차분 접근/분류 폴더/중첩/혼합/추가·삭제 후 재분류, 기존 캐시·기록·스킨 회귀를 실행했다. 곡·폴더·상위 이동의 비선택/선택 6상태 PNG를 실제 RmlUi renderer로 생성하고 부모 행의 구별 및 변경 캐시 무효화를 검사했다. 생성된 부모 선택/비선택 이미지를 시각적으로 확인했다.
전체 앱 창을 수동으로 조작하는 테스트와 이동 중 프레임 시간 측정은 하지 않았다. 렌더 이미지 검증을 실제 앱 전체 UI 테스트로 보고하지 않는다. 자동 펼침으로 표시 행 수가 달라지면 기존 숫자 커서 복원 위치가 조금 달라질 수 있지만 실제 채보 선택/기록 경로는 유지한다.

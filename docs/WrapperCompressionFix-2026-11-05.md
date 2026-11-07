# 논리 폴더 래퍼 압축 수정 — 2026-11-05

## 실제 원인

실제 경로 C:/Users/yakko/Documents/BMS/yamajet_is_sugoi/yamajet_is_sugoi에는 foon_10n.bms, foon_5n.bms가 직접 있고 classic 아래에 cyc-kami.bms, kami.bms가 있다. 바깥 폴더는 채보가 없고 유효한 자식이 하나다. 실제 파싱 결과 하위 그룹은 3개이다.

기존 SongFolderNavigation::build는 descendants[directory]==1(하위 채보 디렉터리 개수)인 말단에서만 압축을 시작했다. 실제 안쪽 폴더는 직접 채보와 classic을 함께 가지므로 이 조건에 걸렸다. 또한 groups[directory].size()==1을 요구하여, 다른 사례에서는 동일 디렉터리의 서로 다른 곡/리소스 그룹 수까지 래퍼 압축을 막았다. 스캔 누락이나 UI의 인덱스 우회가 원인은 아니었다.

## 설정·실행 경로 확인

APPDATA/BmsPlayer/keybindings.ini의 OriginalFolders=0으로 스마트 모드이고 등록 Root1은 C:/Users/yakko/Documents/BMS이다. 조사 당시 기본 BmsPlayer.exe 수정 시각은 2026-11-05 16:27:54였다. 시각만으로 이전 바이너리의 모든 코드 일치를 증명하지는 않는다. 이번 수정 후 기본 경로로 Release 빌드를 성공시켰다.

Main.cpp 1428 부근의 기존 목록 생성은 configure/build 후 folders와 accepts를 사용한다. worker 결과 게시에는 invalidate가 이미 연결되어 있다. preservedFolders는 초기 기본값이 비어 있고 사용자용 영구 설정은 아직 없다. ROOT/등록 루트 경계는 의도적으로 유지한다. 진단 명령은 새로운 SongFolderNavigation 인스턴스로 인덱스를 생성하여 오래된 인덱스의 영향을 배제했다.

## 수정 파일과 함수

- src/SongFolderNavigation.h: build에서 물리 노드의 유효 직계 자식 관계를 이용한 aliases_를 계산한다. 직접 채보가 없는 단일 자식 부모는 그룹 수와 무관하게 대표 표시 위치로 압축한다. 기존 말단 표시 부모도 이 관계로 변환한다. 여러 자식 패키지, 등록 루트, preservedFolders, 원본 모드는 유지한다. 실제 곡 그룹/파일 경로는 병합하거나 변경하지 않는다.
- 같은 파일의 back/build: 압축된 위치 복원 및 상위 이동을 논리 대표 위치로 보정한다.
- 같은 파일의 trace: 물리 경로, UI 위치, 표시 부모, 직접 채보 수, 유효 직계 자식 수, 하위 그룹 수, 압축 여부·원인, 반환 폴더와 채보 경로를 출력한다.
- tests/LibraryTests.cpp: A~D 및 실제 사례와 같은 혼합 구조, 실제 진입 횟수와 남는 하위 폴더 수를 검사한다. --navigation-trace 진단 명령은 실제 yamajet 하위 트리만 읽고 파싱한다. 새 스캐너·캐시·DB를 만들지 않는다.

Main.cpp는 이번 작업에서 변경하지 않았다. 기존 사용자 변경도 보존했다.

## 전후 트리

수정 전 기존 코드의 표시 관계 및 실제 폴더 구조로 확인한 계층:

    BMS
      yamajet_is_sugoi
        yamajet_is_sugoi
          네 채보(직접 2개 + classic 말단 생략 2개)

수정 후 실제 하위 트리 진단:

    BMS
      yamajet_is_sugoi
        foon_10n.bms
        foon_5n.bms
        classic/cyc-kami.bms
        classic/kami.bms

채보 경로를 예시로 표시한 것이며 게임의 곡 제목/차분 그룹 표시 규칙은 그대로다. 바깥 폴더에서 한 번 진입하면 채보 4개가 모두 accepts로 반환되고 추가 폴더는 없다. BMS 루트의 다른 곡을 전부 파싱한 결과는 아니며 실제 UI의 숨겨진 채보 필터 등은 이 반환 데이터 이후 적용된다.

## 진단 사용

선택적 SEVENMANIA_NAV_TRACE 환경 변수에 로그 파일의 절대 경로를 설정한 프로세스에서만 파일 로그를 기록한다. UI 위치나 인덱스가 바뀔 때 기록하며 동일 위치의 반복 build에서는 출력하지 않는다. UTF-8로 저장한다. 기본 실행에서는 출력하지 않는다.

검증용 명령:

    BmsLibraryTests.exe --navigation-trace C:/Users/yakko/Documents/BMS

실제 진단 출력: build-rmlui-song-list/Validation/yamajet-navigation.txt
로그의 직계 자식 수는 키음 폴더 등 비채보 폴더를 제외한 유효 탐색 자식 수이다.

## 테스트와 한계

A: 서로 다른 제목 2개가 있는 단일 래퍼 — 한 번 진입 후 두 채보 반환, 불필요한 폴더 0개.
B: 같은 곡의 normal/hyper 차분 — 같은 검증, 차분 보존.
C: Package 안의 단일 래퍼 + Other 독립 곡 — Package 유지, Package에 한 번 진입하면 곡들 표시, 불필요한 래퍼 0개.
D: Wrapper/One/Two/Inner 연쇄 — 한 번 진입으로 채보 표시.
추가: 직접 채보 + classic 혼합 구조도 한 번 진입으로 접근.

기존 T01~T12 관련 자동 회귀(BmsLibraryTests), 그룹/차분 순환, 기록·스킨 저장/조회/렌더링 및 재생 호환 테스트를 수행했다. 전체 사용자 라이브러리를 게임 UI로 수동 탐색하거나 키음·BGA·프리뷰를 청취한 것은 아니다. 원본 보기에서는 압축되지 않는 것이 정상이다. 추후 외부 코드가 경로를 제자리 수정하면 기존 invalidate 규칙을 지켜야 한다.

최종 빌드/테스트 로그: build-rmlui-song-list/Validation/wrapper-final-build.log, wrapper-tests.log.

최종 결과: 기본 BmsPlayer Release 빌드 성공. BmsLibraryTests, BmsSongSelectionTests, BmsCompatibilityPlaybackTests, SevenManiaSkinBindingTests, SevenManiaSkinApplicationTests 5/5 통과(4.81초).

# BMS 라이브러리 논리 탐색 Phase 0~3 — 2026-11-05

## 1. 기존 코드 감사

SongLibrarySources가 루트 등록·정규화·중복을 관리하고 SongScanner의 snapshotSongLibraries/scanSongLibraries가 기존 SongMetadataCache와 scanSongFolder를 연결한다. SongLibrary의 재귀 검색은 채보 발견 시 하위 검색을 중단하지 않는다. 지원 확장자(BMS/BME/BML/PMS/BMSON/MBM), 키음·BGA 해석과 캐시를 유지했다.

호출 흐름: Main의 라이브러리 로딩 → snapshotSongLibraries 또는 scanSongLibraries → scanSongFolder → 기존 파서/캐시 → worker 소유 결과 게시 → UI 스레드가 gCharts 갱신 → SongSelectionModel 그룹 구성 → SongFolderNavigation::build → 기존 목록 생성(Main.cpp 1428 부근) → 실제 채보 인덱스로 프리뷰/플레이. normalizedRecordPath 및 captureSevenManiaSkinData 연결은 유지했다.

물리/논리 탐색은 이미 original_ 분기로 통합되어 있었다. C++/Rust 곡 그룹화는 백엔드 선택 구조이므로 제거하지 않았다. 별도 가상 트리 클래스·스캐너·DB를 만들 필요가 없었다. 사용되지 않는 코드를 추측하여 삭제하지 않았다.

기준 회귀 테스트: 기존 BmsLibraryTests/BmsSongSelectionTests 2/2 통과(0.79초). 사용자 전체 라이브러리의 스캔 시간·메모리·선곡 진입 시간은 측정하지 않았다.

## 2. 문제 원인

기존 build는 말단 채보 디렉터리를 한 단계만 상위로 표시했다. 여러 중간 디렉터리가 있으면 남은 깊이만큼 탐색해야 했다. 또한 build 호출마다 채보 경로와 조상 관계를 다시 계산했다. 스캔 누락이 아니라 목록 공급을 위한 표시 부모 계산의 문제였다.

## 3. 구현 내용

Phase 1: 기존 독립 Chart 배열·실제 경로·캐시·worker를 재사용했다. 스캐너/기록 키/파일 시스템은 변경하지 않았다.

Phase 2: SongFolderNavigation에 표시 부모, 위치별 자식 폴더, 위치별 물리 채보 디렉터리 인덱스를 유지한다. 입력 데이터·개수·원본 모드·루트·보존 폴더 변경 시 재구축하고, 일반 폴더 이동은 인덱스를 조회한다. 동일 개수의 재스캔 게시에도 invalidate를 명시 호출한다.

단일 곡 그룹으로 이어지고 직접 채보나 다른 곡 디렉터리가 없는 연쇄만 추가 압축한다. 곡 그룹 판단은 기존 canonicalSongTitles/songGroupIdentity를 재사용하며 그룹 자체를 병합하지 않는다. 패키지와 혼합 폴더는 보존한다. preservedFolders는 내부 보존 지점이며 설정 UI/영구 저장 기능은 이번 범위에 추가하지 않았다. 변경 후 invalidate 또는 build로 반영한다.

기존 ROOT 폴더 전용 정책과 ROOT SONGS/ALL SONGS, 등록 루트 경계를 유지한다. 등록 라이브러리에 진입하면 깊은 단일 곡과 일반 곡을 같은 단계에서 볼 수 있다. 스마트 모드는 빈 폴더를 숨기며 원본 모드는 기존의 명시적 빈 폴더 접근을 유지한다. 잘못된 현재 위치는 표시 부모 또는 ROOT로 복구한다. original_ 경로가 논리 압축을 사용하지 않는 기존 탐색 대안이다.

Phase 3: 기존 folders/accepts/displayParent/enter/back 인터페이스와 UI를 그대로 사용한다. Main에는 데이터 게시 시 인덱스 무효화만 연결했다. 폴더 이동은 디스크 스캔이나 BMS 파싱을 호출하지 않는다. 폴더 정렬은 기존 경로 집합 순서를 유지한다.

## 4. 파일별 변경

- src/SongFolderNavigation.h: 연쇄 압축, 캐시된 자식/표시 위치 인덱스, 내부 보존 경계, 원본 보기 경계 처리.
- src/Main.cpp: worker 결과를 반영하는 두 위치에 invalidate 호출.
- tests/LibraryTests.cpp: 연쇄·패키지·혼합·루트 직접 곡·다중 차분·보존 폴더·동일 개수 갱신·전체 경로 집합 테스트. 빈 폴더 호환 테스트는 원본 보기에서 수행하도록 변경.
- docs/LibraryLogicalNavigation-2026-11-05.md: 이 보고서.

CMake, 외부 라이브러리, 렌더러, 스킨 API는 변경하지 않았다.

## 5. Main.cpp 변경 통계

작업 전 6,378줄, 작업 후 6,378줄. 순증가 0줄/순감소 0줄. 기존 두 데이터 게시 문장에 invalidate만 추가했다(WM_LIBRARY_INDEXED 및 초기 로딩 결과 적용 위치). 탐색 알고리즘은 모두 기존 SongFolderNavigation에 있다. HEAD 대비 보이는 다른 Main.cpp 변경은 기존 사용자 작업이며 이 커밋에 포함하지 않는다.

## 6. 테스트 결과

| 항목 | 결과 및 근거 |
|---|---|
| T01 | 통과: 기존/신규 합성 채보 경로 접근 검사 |
| T02 | 통과: 단일 중간 폴더와 일반 곡 동일 단계 |
| T03 | 통과: FREEDOM DiVE/Extra/More 연쇄 압축 |
| T04 | 통과: BOF/A·B 패키지 유지 |
| T05 | 통과: Mixed 직접 채보 및 Extra 채보 각각 한 번 접근 |
| T06 | 통과: 등록 루트 직접 채보와 ROOT SONGS 기존 테스트 |
| T07 | 통과: 같은 폴더의 모든 차분 경로 집합 보존 |
| T08 | 통과: 기존 SongSelectionTests의 다른 제목/리소스/폴더 분리 및 차분 순환 |
| T09 | 통과: 스마트 빈 폴더 제외, 원본 빈 폴더 복원 호환 |
| T10 | 자동 통과: SkinBindingTests의 기존 기록 저장·조회·재로딩·다른 채보 분리 |
| T11 | 자동 통과: CompatibilityPlaybackTests와 SkinApplication/BindingTests. 실제 사용자 곡의 수동 키음·BGA·프리뷰 청취는 미실행 |
| T12 | 통과: 기존 중복·중첩·복수 루트의 모든 채보 경로 집합 비교 |

최종 관련 CTest 6/6 통과(8.76초): BmsCompatibilityTests, BmsCompatibilityPlaybackTests, BmsLibraryTests, BmsSongSelectionTests, SevenManiaSkinBindingTests, SevenManiaSkinApplicationTests. Release BmsPlayer 빌드 성공 및 기본 build-rmlui-song-list/Release/BmsPlayer.exe 갱신. 기능별 배포 실행 파일은 만들지 않았다.

검증 중 원본 보기에서 외부 조상 루트가 내부 ROOT를 다시 자식으로 연결하는 순환을 발견했다. 소유 루트가 다른 부모 연결을 차단한 뒤 전체 회귀 테스트를 통과했다.

## 7. 성능 영향

최종 실행의 합성 채보 8개에서 캐시 조회 build 10,000회는 20.2081ms였다. 전체 라이브러리나 화면 렌더링 성능 수치가 아니다. 동일 조건의 수정 전 측정이 없어 개선율은 보고하지 않는다.

재구축은 고유 채보 디렉터리와 조상 경로를 순회하고 map/set을 사용한다. 경로 깊이와 기존 그룹 식별 계산 비용을 포함하므로 엄밀한 O(N)이라고 주장하지 않는다. 조회 단계에서는 전체 채보를 다시 분류하지 않는다. 기존 UI의 전체 그룹 필터링 자체는 이번에 재작성하지 않았다. 추가 메모리는 인덱스 크기에 비례하며 실제 바이트 사용량은 측정하지 않았다.

## 8. 호환성

실제 Chart::path, normalizedRecordPath, 기존 그룹 인덱스와 화면 요소는 유지한다. 입력·하이라이트·스크롤·프리뷰는 기존 연결을 그대로 사용한다. 자동 회귀 범위는 위 표와 같으며 실사용 수동 검증을 대체하지 않는다.

## 9. 잔여 위험

매우 깊거나 큰 실제 라이브러리의 성능, 장시간 실행, 실제 컨트롤러 탐색 및 재생 후 스크롤 위치의 수동 검증은 미실행이다. 배열을 제자리에서 수정하면서 경로/그룹 관계를 바꾸는 새 호출자는 invalidate를 호출해야 한다. 보존 폴더 설정은 내부 API만 제공한다. 폴더/곡 표시명을 새로 재작성하지 않고 기존 UI 메타데이터 표시를 유지했다. 원본 모드와 ALL SONGS는 접근성 확인을 위한 기존 대안으로 유지한다.

## 10. 요약

기존 스캔·캐시·그룹화·UI를 재사용하고 SongFolderNavigation의 표시 관계만 확장했다. 단일 곡 연쇄는 압축하고 패키지·혼합 폴더·다중 루트는 보존한다. Main.cpp는 늘리지 않았다. 관련 Release 빌드와 6개 회귀 테스트를 통과했다.

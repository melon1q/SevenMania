# ROOT 곡 표시 정책 수정

## 원인과 실제 경로

기존 스마트 탐색은 말단 채보 디렉터리를 무조건 한 단계 위에 표시했다. Songs 바로 아래 곡 폴더도 예외가 없어 ROOT에 곡이 출력됐다. 메타데이터 캐시의 실제 경로와 `displayParent()`를 추적했다. 아래 경로는 `build-rmlui-song-list/Release/Songs` 기준이다.

| 곡 | 실제 채보 예시 | 이전 표시 부모 | 변경 후 표시 부모 |
|---|---|---|---|
| FREEDOM DiVE | `FREEDOM DiVE (by xi)-4f77dc99c4037d39/FD_FDP.pms` | Songs (ROOT) | 해당 물리적 곡 폴더 |
| Cutter | `[EmoCosine]Cutter-f479728125773687/_cutter_13_SPH.bms` | Songs (ROOT) | 해당 물리적 곡 폴더 |
| CAT ROTATION | `katagiri_catrotation_ogg-e9b016d68b6ec732/_7A.bms` | Songs (ROOT) | 해당 물리적 곡 폴더 |

별도로 존재하는 `2020-de4f97ba31012a55/[EmoCosine]Cutter/…`와 `Songs-34f81cf22075c7ce/katagiri_catrotation_ogg-e9b016d68b6ec732/…`는 기존처럼 각각 2020 분류 폴더와 Songs-… 분류 폴더에서 표시한다. 같은 제목의 파일을 합치거나 이동하지 않았다.

## 구현

- `src/SongFolderNavigation.h`: ROOT에서는 일반 곡을 받지 않으며, ROOT 바로 아래 채보 폴더는 생략하지 않는다. 실제 표시 부모도 그 물리적 폴더로 변경하므로 단순히 곡을 숨긴 처리가 아니다.
- Songs에 직접 있는 채보의 표시 부모는 `ROOT SONGS` 가상 폴더다. 기존 폴더 항목 벡터/선택 경로로 연결하며, 진입·뒤로 가기·커서·저장 위치 복원을 지원한다. 실제 절대 경로의 동명 디렉터리와 가상 위치는 구분한다.
- ROOT보다 깊은 분류 폴더에서는 기존 말단 폴더 생략 및 혼합 디렉터리 보존 규칙을 유지한다. ROOT 바로 아래 물리적 곡 폴더로 들어갔을 때 자동으로 ROOT로 되돌아가지 않도록 보정했다.
- 기존 ALL SONGS 고정 항목과 외부 라이브러리 루트 연결은 유지한다.
- 실제 `Chart::path`, 그룹화, 기록 키, 캐시 형식/내용, 스킨/에셋, 렌더러는 변경하지 않았다. 가상 위치 저장은 기존 탐색 상태 파일 규격을 재사용한다.
- `tests/LibraryTests.cpp`: ROOT SONGS 분리·진입·복원·복귀, ROOT 음악 0개, 전체 경로 집합 보존 검사를 추가했다. 가상 폴더가 추가됐으므로 테스트에서 물리적 폴더를 고정 행 번호 대신 경로로 찾는다.

## 빌드와 테스트

기본 `build-rmlui-song-list/Release/BmsPlayer.exe` Release 빌드 성공 및 업데이트.

관련 CTest 4/4 통과: BmsLibraryTests, BmsSongSelectionTests, BmsSongLibraryUiTests, SevenManiaSkinBindingTests. 기존 격리 기록 저장·조회 및 스킨 바인딩 검사를 포함한다.

실제 캐시의 788개 채보를 사용한 추가 검사:

- 스마트 탐색: ROOT 음악 0개, 35개 탐색 위치를 통해 788개 채보 각각 정확히 한 번 접근.
- 원본 탐색: ROOT 음악 0개, 135개 탐색 위치를 통해 동일한 788개 채보 각각 정확히 한 번 접근.
- 합성 fixture는 Songs 직속 채보, 여러 차분, 복수 곡, 중간 분류, 혼합 폴더, 다중/중첩 라이브러리 루트를 포함한다. ALL SONGS는 전체 집합으로 별도 검사한다.

실제 경로 검증 명령은 `BmsLibraryTests.exe --root-policy <Songs 절대 경로>`다. 기존 캐시를 읽으며 BMS 파일/캐시 항목을 재작성하지 않는다. 로그는 `build-rmlui-song-list/Validation/root-policy/`에 보관하고 커밋에서 제외한다.

수동 게임 화면 조작 및 실곡 플레이는 이번에 수행하지 않았다. 이전 작업에서 남은 Host 재그리기 픽셀 비교 문제를 수정하거나 전체 회귀 테스트가 모두 통과했다고 주장하지 않는다.

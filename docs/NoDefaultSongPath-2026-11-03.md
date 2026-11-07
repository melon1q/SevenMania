# 기본 곡 경로 없음

## 변경 사항

- 게임과 런처의 새 기본 등록 목록은 빈 목록이며 화면에는 `(없음)`으로 표시한다.
- 실행 파일 옆 Songs가 존재해도 자동 등록·검색하지 않는다. 기존 LastFolder 값도 검색 루트로 자동 복원하지 않는다.
- 이전 설정에 명시적으로 저장된 외부 등록 경로는 계속 읽는다. 자동 기본 Songs는 원래 등록 목록에 저장하지 않았으므로 이를 별도로 추가하지 않는다.
- 모든 등록 경로는 동일하게 취급하며 첫 번째 경로/마지막 경로도 해제할 수 있다. 원본 곡 파일이나 플레이 기록은 삭제하지 않는다.
- F2 또는 기타 설정/런처의 폴더 추가에서 원하는 경로를 직접 등록한다. 사용자가 명시적으로 선택하면 실행 파일 옆 Songs도 등록할 수 있다.
- 등록 경로가 없으면 실제 라이브러리 스냅샷·스캔은 빈 결과를 반환하고 게임의 곡 목록도 빈 상태로 유지한다. 등록 없이 Songs를 만들지 않는다.

## 경로·캐시 분리

기존 구현은 첫 번째 곡 루트를 탐색 홈·캐시 저장 위치로도 사용했다. 기본 곡 경로를 제거하면서 이 역할을 분리했다.

- 실제 등록 목록: 사용자 경로만 저장. 기존 `[SongLibrarySources] Root1...` 형식/UTF-16 경로 인코딩을 유지한다.
- 캐시: 설정 파일 부모의 `.sevenmania-index/libraries/<루트 해시>/`에서 기존 메타데이터 캐시를 사용한다.
- 탐색 상태: 같은 관리 영역의 Navigation 아래에 보관한다. ROOT는 모든 등록 라이브러리의 탐색 홈이며 특정 곡 폴더가 아니다.
- 기존 Songs 캐시나 외부 곡 폴더 안의 파일을 삭제/이동하지 않는다. 저장 위치 변경으로 등록 후 첫 검증에서 캐시를 재생성할 수 있다. 메타데이터 바이너리 규격과 실제 채보/기록 키는 변경하지 않았다.
- 독립적인 저수준 스캐너 API의 명시적 경로 사용은 유지한다. 이 API와 진단 명령은 게임/런처의 자동 기본 경로 등록과 별개다.

## 검증

기본 BmsPlayer.exe, SevenMania.exe 및 SevenManiaLauncher.exe Release 빌드 성공.

관련 CTest 7/7 통과: BmsLibraryTests, BmsSongSelectionTests, BmsSongLibraryUiTests, SevenManiaLauncherTests, SevenManiaLauncherUiTests, SevenManiaSkinBindingTests, BmsSettingsPhase6Tests.

검사에는 다음을 포함한다.

- 기존 Songs에 실제 채보가 있어도 초기 등록/스캔 결과가 비어 있음
- 런처 초기 경로 목록의 `(없음)` 표시
- 명시적 폴더 등록·캐시 재사용·등록 해제
- 게임 설정에서 첫 경로부터 모두 해제한 뒤 실제 목록이 0개이며 재로드해도 빈 등록 상태 유지
- 등록 해제 후 원본 파일 보존
- 실제 게임을 종료한 뒤 모든 루트를 해제하고 SevenMania.exe를 런처 없이 다시 실행: 기존 Songs/LastFolder/캐시가 있어도 실제 ListView 항목 수 0개 확인
- 기존 설정창·차분 전환·기록/스킨 바인딩 회귀 검사

로그와 격리 실행 결과는 `build-rmlui-song-list/Validation/no-default-*.log` 및 기존 LauncherUI 검증 폴더에 보관한다. 빌드 산출물/캐시/개인 곡 폴더는 커밋하지 않는다.

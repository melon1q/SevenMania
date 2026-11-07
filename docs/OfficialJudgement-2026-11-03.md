# SevenMania 공식 판정 엔진 SM-JUDGE-1

## 구현 결과

일반 건반과 롱노트 헤드에 다음 확정 표를 적용했다. 단위는 ms이며 모든 경계는 포함한다. 입력 타임스탬프는 double 정밀도를 유지하고 정수로 반올림하지 않는다.

| RANK | PGREAT | GREAT | GOOD | BAD FAST | BAD SLOW |
|---|---:|---:|---:|---:|---:|
| 0 | 7 | 20 | 39 | 128 | 135 |
| 1 | 13 | 30 | 68 | 155 | 170 |
| 2 | 17 | 43 | 106 | 183 | 205 |
| 3 | 21 | 60 | 135 | 210 | 240 |

`deltaMs = (입력 시각 - 노트 시각) * 1000`이며 기존 연습/채보 재생 속도 보정은 유지한다. 시간 출처는 기존 `gAudio.positionSeconds()`이고 오디오·입력 오프셋을 추가하지 않았다. 렌더링 FPS를 판정 시간으로 사용하지 않는다.

## 파일 및 주요 연결

- `src/OfficialJudgement.h`: constexpr 정수 표, 규격 ID, 할당 없는 ms 평가 함수. BAD 밖은 optional의 빈 값으로 반환하며 노트를 소비하지 않는다.
- `src/ChartRules.h`: 일반 건반/스크래치 정책 분리, `judgeLaneOffset`, `nearestLaneNote`, `laneNoteExpired`. 기존 `judgeChartOffset`은 특수 노트와 확장 판정용으로 유지한다.
- `src/BmsParser.h`, `src/BmsParser.cpp`: `rankDefaulted`, `judgementSpec`, 비치명적 warnings 추가. 활성 헤더의 마지막 유효 0~3 정수만 적용한다. 생략/모두 무효이면 2. 무효 값은 이전 유효값을 보존하고 warnings 및 Windows 디버그 로그에 남긴다. 비활성 IF는 무시한다.
- `src/SongMetadataCache.h`: RANK·기본값 여부·규격 ID 저장/복원. 캐시 버전 2→3. 구형 캐시는 거부 후 기존 재분석 경로로 재생성한다. 채보/곡 경로 및 기록 키는 변경하지 않는다.
- `src/Main.cpp`: `pressLane`은 정책별 후보 검색과 단일 계산 결과를 기존 판정/FAST·SLOW/LN 시작에 전달한다. `updatePlaybackFrame`은 방향별 지연 마감으로 미입력 노트를 처리한다. 스크래치와 건반의 서로 다른 마감 때문에 먼저 나온 스크래치가 뒤 건반의 미입력 처리를 막지 않도록 개별 검사한다.
- 같은 파일의 `PlayRecord`, `loadPlayRecords`, `savePlayRecord`, `finishPlayback`: 최고 점수·콤보·클리어 각각의 획득 규격과 RANK를 독립 저장한다.
- `tests/OfficialJudgementTests.cpp`, `tests/OfficialJudgementIntegrationTests.cpp`, `CMakeLists.txt`: 새 단위/통합 테스트 2개 등록.

## 특수 노트 및 기존 기능

스크래치 레인 0/8은 기존 사용자 판정 프로필과 RANK 배율을 유지한다. PopnNine의 0/8은 건반으로 취급한다. 일반 건반 공식 표에는 사용자 Easy/Normal/Hard 프로필 배율을 곱하지 않는다. 기존 설정은 스크래치와 LN 종료 등 기존 경로에 계속 영향을 준다.

LN/CN/HCN 헤드는 건반일 때 공식 표를 사용한다. LN 유지·해제·CN 종료의 `currentJudgementWindows().goodMs`, `resolveHold`의 기존 종료 판정 및 HCN 정책은 그대로 유지한다. 새 BAD 범위를 LN 해제 허용치로 사용하지 않는다.

이미 지원하던 DEFEXRANK/EXRANK 및 HELLJUDGE는 삭제하지 않았다. 이러한 확장 판정이 실제 적용된 채보는 기존 계산을 사용하고 `LEGACY-EXTENDED`로 구분한다. 따라서 그 채보에는 SM-JUDGE-1 표가 적용되지 않는다.

BAD 밖 입력은 기존처럼 빈 입력/키음 경로로 전달한다. 히트 배열을 통해 중복 판정을 막고 인접 후보는 기존처럼 가장 가까운 노트(동률이면 뒤 후보)를 선택한다. 미입력은 BAD SLOW를 초과한 뒤 기존 POOR 이벤트로 처리한다.

1,000,000점, S/AAA/AA/A/B/C/F, 게이지 규칙, 콤보 규칙, 스킨 바인딩, 판정 제한 모드 및 연습 모드의 기존 실행 로직은 변경하지 않았다. Reflex, FramePacer, BGA, 컨트롤러 및 오디오 엔진은 이 작업에서 수정하지 않았다.

## 기록 호환성

기존 normalizedRecordPath 및 Play_ 섹션을 유지한다. 신규 키는 ScoreJudgeSpec/ScoreJudgeRank, ComboJudgeSpec/ComboJudgeRank, ClearJudgeSpec/ClearJudgeRank이다. 키가 없으면 LEGACY, RANK -1로 읽는다. 새 점수가 기존 최고 점수보다 낮거나 같으면 과거 점수의 규격을 덮어쓰지 않는다. 최고 콤보와 클리어는 독립 갱신한다. 연습 모드는 기존처럼 기록을 저장하지 않는다. 온라인 기능과 규격별 별도 랭킹 저장소는 추가하지 않았다.

## 실행한 검증

Release BmsPlayer 및 SevenManiaLauncher 빌드 성공. 기본 `build-rmlui-song-list/Release/BmsPlayer.exe` 갱신. 기존 POST_BUILD에 따른 SevenMania.exe도 유지한다. 기능별 배포 실행 파일은 추가하지 않았다.

최종 CTest 10/10 통과(총 5.10초):

- SevenManiaOfficialJudgementTests: 표 전체, 양방향 포함 경계 및 경계 바로 밖 소수점, BAD 비대칭, NaN, 기본값.
- SevenManiaOfficialJudgementIntegrationTests: RANK 파싱/중복/잘못된 값/IF, 캐시 왕복, 확장 판정 보존, 스크래치/Popn 구분, 인접 후보/중복/과도한 조기 입력, 늦은 미입력 마감, 동일 타임스탬프 판정, 실제 pressLane의 FAST·점수·중복 방지, LN 헤드/종료 및 1,000,000점, 판정 제한 등급 호환, LEGACY/새 기록의 독립 갱신과 재로딩.
- BmsCompatibilityTests, BmsCompatibilityPlaybackTests, BmsCoreTests, BmsLibraryTests, BmsSongSelectionTests, BmsLongNoteTests, SevenManiaSkinBindingTests, SevenManiaSkinApplicationTests.

로그: `build-rmlui-song-list/Validation/official-tests.log`, `official-final-confirm-build.log`, `official-launcher-build.log`.

빌드 중 일시적인 SongLibrary.obj 접근 오류가 있었으며 순차 빌드 재시도 및 최종 빌드가 성공했다.

## 미검증 및 제한

실제 컨트롤러 입력, 다양한 FPS에서의 실시간 하드웨어 플레이, 장시간 플레이, P95/P99 프레임 시간 및 물리적 입력 지연은 측정하지 않았다. FPS 독립성 자동 테스트는 동일 타임스탬프를 주는 논리 검증이며 모니터별 실측이 아니다. 판정 제한의 등급 연결은 검증했지만 창 메시지를 통한 전체 자동 재시작을 새 테스트로 재현하지 않았다. 실시간 SLOW 표시의 눈으로 보는 검증도 별도다. 기존 스킨 자동 렌더 테스트 통과가 모든 사용자 스킨의 수동 검증을 의미하지는 않는다.

온라인 도입 전에는 연습·속도·확장 옵션과 스크래치/LN 종료 정책을 포함한 경기 적격성 및 규격별 기록 분리를 별도로 확정해야 한다. 이번 버전은 기존 로컬 최고 기록을 삭제하거나 판정 규격별로 재분류하지 않는다.

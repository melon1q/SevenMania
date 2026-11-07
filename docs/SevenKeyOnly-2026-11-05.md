# 7+1키 진입 제한과 yamajet 건반 표시 확인 — 2026-11-05

## 구현

GameplayModePolicy.h에서 플레이 허용 모드를 SevenKey 하나로 정의했다. Main.cpp의 선택 시점, 실제 채보 재파싱 후 음원/BGA 준비 전, 최종 플레이 전환에서 각각 검사한다. 일반/연습 진입과 초기 메타데이터가 오래된 경우를 함께 방어한다. 지원하지 않는 모드는 이유와 판별 모드를 표시한다. RmlUi 행의 blocked 표시도 같은 정책을 사용한다. 파서의 타 모드 지원 및 채보·기록·스킨 파일은 삭제하거나 변환하지 않았다.

## 실제 파일 확인

C:/Users/yakko/Documents/BMS/yamajet_is_sugoi 하위 4개 파일의 전체 채널과 실제 파싱 결과를 확인했다.

| 파일 | PLAYER | 노트 채널 | 현재 파서 모드 |
|---|---|---|---|
| foon_10n.bms | 3 | 12,14,15,26 | TenKey |
| foon_5n.bms | 1 | 12,14,15,16 | FiveKey |
| classic/cyc-kami.bms | 1 | 12,14,15,16 | FiveKey |
| classic/kami.bms | 1 | 12,14,15,16 | FiveKey |

foon 파일은 제목에도 5keys/10keys가 명시되어 있다. classic 파일에는 6·7번 건반 채널(18/19)이 없다. 그러나 비어 있는 레인이 있는 7키 BMS는 사용 채널만으로 원작 의도를 단정할 수 없으므로, 화면에서 보았다는 별도 7키 파일의 정확한 지정은 여전히 필요하다. 원본 파일을 임의로 7키로 승격하거나 바꾸지 않았다.

기존 SkinApplicationHost.inl의 updateSkinJudgementAnchor는 SevenKey/Unknown에서만 스킨 기어를 활성화한다. Main.cpp의 drawGameScreen은 FiveKey 등에서 기본 사각형 건반을 출력한다. 따라서 확인된 파일의 기본 건반 출력은 이미지 디코딩 실패가 아니라 모드별 렌더 분기다. 이번에는 비지원 모드 자체의 진입을 막아 해당 대체 경로로 진입하지 않도록 했다. 독립된 SevenKey 텍스처 로딩 버그가 확인되었다고 주장하지 않는다.

## 검증

기본 BmsPlayer.exe Release 빌드 성공. 전체 모드의 허용 정책을 검사했고 실제 yamajet 4개 파일 모두 현재 정책에서 차단됨을 별도 진단 명령으로 확인했다. 진단 출력: build-rmlui-song-list/Validation/yamajet-modes.txt(mode=1은 FiveKey, 3은 TenKey).

SkinBindingTests에는 기본 스킨의 gear-key-1~7 이미지 리소스 존재와 눌림 상태에 따른 픽셀 변화 검사를 좌/중/우 각각 추가했다. 실제 자동 렌더 검사이며 수동 하드웨어 플레이 검증은 아니다.

최종 관련 테스트 5/5 통과(4.61초): BmsCompatibilityTests, BmsCompatibilityPlaybackTests, BmsCoreTests, SevenManiaSkinBindingTests, SevenManiaSkinApplicationTests. 로그: mode-build.log, mode-gear-build.log, mode-tests.log(모두 build-rmlui-song-list/Validation).

## 제한

소스 코드의 플레이 진입 방어는 구현했지만 모든 실제 UI/컨트롤러 경로를 수동 조작하지는 않았다. 파서가 FiveKey로 추론하는 모호한 BMS를 자동으로 SevenKey로 재해석하지 않는다. 목록에서 차단된 항목을 완전히 제거하지는 않는다. 별도 7키 파일의 문제 재현이 확인되면 그 파일의 모드/스킨 적용 경로를 다시 확인해야 한다.

# 요약·곡 정보 박스 등장 애니메이션 수정 (2026-11-07)

첨부된 Release/Assets/Skins/DefaultReference.semaskin의 settingssummary와 songinformation 요소에 Zoom In이 지정되어 있었다. 원인은 스킨 좌표가 아니라 SkinApplication::drawDc의 공용 cachedScreen 비교였다. 같은 프레임에서 select 다음에 오버레이를 합성할 때마다 화면 변경으로 판단하여 screen.enter를 재발생시켰다. 불투명도가 매번 시작값으로 돌아갔다.

화면별 최초 진입 상태와 합성 캐시 상태를 분리하고, 기본 장면 전환은 별도로 추적한다. 요약 박스와 곡 정보 박스를 실제로 여는 경로에서 enterScreen을 명시적으로 호출한다. 따라서 매 프레임 재시작하지 않으며, 닫았다 다시 열면 다시 재생한다. 스킨 파일 자체는 수정하지 않았다.

검증: 수정 전 동일한 회귀 테스트가 `animated overlay never became visible`로 실패했다. 수정 후 8개 프리셋 × 2개 오버레이의 Select/오버레이 반복 합성 192프레임과 재열기 16회가 통과했다. Direct2D의 메모리 렌더링 결과를 읽어 완료 상태의 패널 픽셀을 확인했다. 실제 데스크톱 화면 조작이나 수동 게임 실행 검증은 하지 않았다.

기존 Release 구성의 BmsPlayer 및 SevenManiaSkinApplicationTests 빌드 성공. 기본 BmsPlayer.exe를 갱신했다.

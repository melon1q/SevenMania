# 등장 애니메이션 프리셋 (2026-11-07)

에디터 우측 하단 `등장 애니메이션...`에서 선택 요소 또는 현재 장면 전체에 적용한다. select, play, result, songinformation, songintro, settingssummary 모두 같은 메뉴를 사용한다. None은 프리셋만 제거하며 기존 키프레임 트랙은 유지한다. 프리셋은 Zoom In, Zoom Out, Drop In, Bounce In, Rotate In, Wipe In, Blur In, Elastic In이며 기본 시간은 0.6초다.

요소 JSON의 `extensions.entrance`에 `preset`, `duration`(초), `delay`(초)를 저장한다. 기존 스킨은 이 항목이 없으면 기존 방식으로 출력한다. 화면 진입 이벤트에서 재시작하며, 원래 게임 데이터·판정·오디오 시간은 변경하지 않는다. 기존 키프레임 애니메이션 결과에 등장 변환을 합성한다.

Blur In은 Gaussian 후처리 대신 기존 텍스처를 3×3 다중 샘플로 그리는 근사 흐림이다. 처음 0.6초 동안 해당 요소의 그리기 호출이 늘어난다. 이미지 파일을 재디코딩하거나 CPU 픽셀을 매 프레임 수정하지 않는다. 나머지 프리셋은 변환·불투명도·클리핑으로 처리한다. 재생 종료 후 기본 그리기 경로를 사용한다.

게임 및 에디터 Release 빌드 성공. SevenManiaSkinAnimationTests 통과: 8종 시작/종료 상태, 60/120/144/240Hz 계산, JSON 설정, 잘못된 프리셋 거부 및 기존 키프레임 테스트. 실제 화면에서의 시각 확인 및 장시간 성능 측정은 수행하지 않았다.

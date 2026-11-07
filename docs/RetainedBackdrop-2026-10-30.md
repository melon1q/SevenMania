# 기본 GPU 배경 보존 경로 적용 (2026-10-30)

성능 보고서의 첫 개선 대상으로 전체 화면의 반복 GPU 업로드를 줄였다. 기존 하이브리드 렌더러를 기본으로 활성화했으며, 기본 배포 빌드도 BMS_HYBRID_RENDERER=ON으로 다시 구성했다. 기존 CMake 캐시가 OFF인 개발 환경에서는 명시적으로 ON으로 재설정해야 한다.

배경 generation이 그대로면 GPU 배경을 재사용하고 기어/HUD의 CPU 영역만 전송한다. 영상·이미지 이벤트로 배경이 바뀌면 기존처럼 갱신한다. GPU 출력이 꺼져 있거나 실패한 경우의 CPU 경로도 유지된다. 이 변경은 영상 디코더나 CPU 확대 자체를 GPU 디코딩으로 바꾸는 작업은 아니다.

검증:
- BmsGpuTests: 배경 generation 재사용/변경, 음영·레인 투명도, 영역 이동과 출력 큐 지연 검사.
- BmsDisplayTests: 실제 drawGameScreen을 연속 렌더링하여 배경 재합성과 전체 화면 업로드가 생략되는지 추가 검사. 테스트 화면에서 전송량 2,995,296 → 2,213,744바이트(26.09% 감소). 프레임의 CPU 영역 비율에 따라 효과가 달라진다.
- SevenManiaSkinApplicationTests: 실제 게임 화면과 적용된 스킨을 함께 GPU 렌더링하고 캡처하며 CPU readback이 추가되지 않는지 검사 추가.
- SevenManiaSkinRenderTests: 기존 스킨 렌더링 회귀 검사.

위 수치는 전송 바이트 검사 결과이며 현재 사용자 곡의 FPS나 물리 입력 지연 개선 측정값이 아니다. 기본 실행 파일 경로는 build-rmlui-song-list/Release/BmsPlayer.exe다. 사용자 배포 스킨은 빌드 전에 백업한 뒤 복원한다.

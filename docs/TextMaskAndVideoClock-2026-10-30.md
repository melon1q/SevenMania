# 판정 글자 모양/색상 분리 및 영상 따라잡기 제거 (2026-10-30)

## 영상

실험용 지연 감지 자동 seek, 12프레임 단위 분할 처리, needsMore/catchingUp 재요청 루프 및 240회 처리 제한을 제거했다. 새로운 요청 없이 목표를 향해 반복 작업하는 별도의 따라잡기 상태는 없다.

기본 재생은 요청된 시각의 프레임을 순차 디코딩으로 선택한다. 처음 연습 구간으로 진입할 때의 seek, 명시적인 시간 되감기, 반복 재생은 정상 재생 기능으로 유지했다. EOF에서는 한 업데이트 안에서 파일을 계속 반복해 읽지 않고 다음 시각 갱신의 정상 반복 경로를 사용한다. 공개 프레임의 PTS/요청 시각은 관측 정보로 남아 있으며 재생을 보정하지 않는다. 이전 BgaAssetPreparation 문서의 '뒤처진 영상' 절은 이번 변경으로 폐기됐다.

## 판정·콤보 글자

- 글자/글꼴이 바뀔 때만 검정·흰색 이중 외곽선이 있는 기본 이미지와 내부 글리프 알파 마스크를 생성한다.
- 색상과 PGREAT phase를 모양 캐시 키에서 제외했다. GPU는 기존 이미지를 유지하며 브러시 색으로 마스크를 채운다.
- 기본 스킨과 기존 native 판정 출력 모두 적용했다. CPU 백버퍼 출력도 같은 마스크를 색칠하며, 원래 확대·중앙 정렬·클리핑은 유지한다. GDI 직접 출력은 오류/출력 경로 호환용으로 유지한다.
- 스킨의 render target 교체 시에는 해당 target의 이미지·마스크·브러시를 새로 만든다. 마스크 메모리는 비트맵 예산에 포함한다.
- 콤보 숫자 자체가 바뀌면 새 글자 모양이 필요하므로 그때는 래스터 생성과 업로드가 발생한다. 글리프별 숫자 atlas 도입은 이번 작업 범위 밖이다.
- 색상 합성 방식이 달라져 예전 GDI 색상별 래스터와 안티앨리어싱 경계 픽셀의 완전 일치를 보장하지는 않는다. 글꼴·외곽선 모양은 동일한 GDI 경로로 만들며 색상 변화가 외곽선/배경에 번지지 않는 것을 검사했다.

GPU 마스크 적용은 [Direct2D FillOpacityMask](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1rendertarget-fillopacitymask(id2d1bitmap_id2d1brush_d2d1_opacity_mask_content_constd2d1_rect_f_constd2d1_rect_f))를 사용하고, 호출 동안 필요한 ALIASED 모드를 설정한 뒤 호출자 상태를 복원한다.

## 검증

- 동일 문구에서 색상을 여러 번 바꾸고 PGREAT을 켜도 textLayouts/textUploads 증가 0회.
- 색 변화가 내부 마스크 영역에만 생기며 외곽선·배경은 유지됨을 픽셀 검사. 콤보 문구 변경 시에는 모양 갱신 검사.
- CPU 마스크의 투명/반투명/불투명 색상 합성과 크기 변경·클리핑 검사.
- 정상 영상 시각 선택·반복·되감기·POOR 독립 시계 및 기존 준비/취소 검사.
- BmsVideoTests, BmsGpuTests, BmsDisplayTests, SevenManiaSkinRenderTests, SevenManiaSkinApplicationTests, SevenManiaSkinBindingTests 총 6개 통과.
- 실제 하드웨어 D3D11 스킨 렌더링 검사 및 사용자 배포 스킨 preview 통과. PGREAT 이미지도 확인했다.

기본 build-rmlui-song-list/Release/BmsPlayer.exe를 갱신하고 사용자 배포 스킨을 복원했다. 이번 수치는 캐시 재사용·기능 검증이며 실제 플레이 FPS 개선률 측정값은 아니다.

# 기본 스킨의 이미지 판정·콤보 적용

- 현재 사용 중인 `DefaultReference.semaskin`의 디자인을 기준으로 판정 6종과
  숫자 0–9의 투명 PNG를 추가했다. 기존 다른 이미지·곡 선택/리절트/기어 배치는 유지한다.
- PGREAT은 기존 표기인 `GREAT`에 정적인 무지개색을 넣었다. 나머지는
  기존 판정 색상, 콤보는 밝은 흰색이며 검정/흰색 외곽선을 사용한다.
- `play.judgementCombo`의 `hud.judgementMode`와 `hud.comboMode`를 모두
  `image`로 지정했다. 리소스는 `default-hud-pgreat` 등 6개와
  `default-hud-0`–`default-hud-9`이다. PNG 경로는
  `assets/images/hud/judgement/`, `assets/images/hud/combo/`이다.
- 폰트 경로가 내부적으로 사용하던 기준 좌표 `(660,520)`을 이미지 요소에도
  명시했다. 예전 JSON의 y=570을 그대로 사용하면 모드 전환 시 50px 내려가는
  차이가 있어 이를 피했다. 사용자 스킨 탭의 위치 보정은 그대로 적용된다.
- 원본 PNG: `Assets/Skins/DefaultHudImages/`. 재생성/패키징 도구:
  `tools/UpdateDefaultHudImages.py`. Oxanium Regular와 OFL을 사용하며,
  라이선스도 패키지에 포함한다. Pillow는 제작 도구에만 필요하다.
- 게임/에디터 빌드에 항상 실행되는 기본 스킨 복사 target을 추가했다.
  실행 파일이 이미 최신이어도 변경된 패키지를 복사한다. 실험에서 오래된
  출력 스킨을 새 것으로 복원하면서 실행 파일의 수정 시간은 그대로임을 확인했다.
- 현재 사용자 설정은 예전 `RecoveredDefault-2026-10-30.semaskin` 선택을
  해제하고 기본 스킨 및 패키지 지정 모드(-1/-1)를 사용하도록 변경했다.
  다른 설정과 복구 스킨 파일은 유지한다. 설정 백업은 빌드 검증 폴더에 있다.

검증: 스킨 관련 CTest 8개, 판정 6종·콤보 자릿수 전환·256회 이미지 갱신,
에디터 저장/재로딩, 실제 엔진 12초 자동 입력 재생을 확인했다.
이미지 모드는 아틀라스 1개, 폰트 대체 glyph 0개로 준비되었다.
장시간 플레이 성능이 개선되었다는 주장은 하지 않는다.

검증 로그/미리보기: `build-rmlui-song-list/Validation/DefaultHudImages/`.
실제 재생 기록: `build-rmlui-song-list/Validation/HudFrameStudy/default-adopted-final-original-11-r1.*`.

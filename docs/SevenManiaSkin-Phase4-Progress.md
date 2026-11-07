# 4차 애니메이션 — 부분 구현 상태

현재 4차 전체 완료가 아니다. 기본 키프레임 평가와 렌더 연결만 구현했다.

- 추가: src/skin/SkinAnimation.h, tests/SkinAnimationTests.cpp, tests/fixtures/skins/animation-basic.semaskin
- 수정: SkinRenderer, SkinApplication, SkinApplicationHost.inl, Main.cpp의 읽기 전용 판정 알림, CMakeLists.txt
- 요소 extensions.animations 배열에 트랙을 선언한다. 요소 자체가 대상이다. id/property/duration/keys(time,value), easing, loop, trigger, finish, policy를 사용한다.
- 지원 속성: x/y(절대 좌표), scale(중심 기준 배율), rotation(도), opacity(0~1). 같은 속성은 배열 뒤쪽 트랙이 우선한다.
- easing: linear, ease-in, ease-out, ease-in-out. 구간별 보간이다.
- finish: hold 또는 restore. policy: restart 또는 ignore. 중첩 실행은 없다.
- screen.enter는 준비 및 CPU 화면 진입에 연결했다. judgement는 기존 판정 표시 함수 호출 시 전달한다. 게임 계산을 변경하지 않는다.
- 시간은 steady_clock UI 경과 시간이다. 게임 시간/일시정지 구분은 아직 없다.
- 프레임 평가에 JSON/I/O/이미지 디코딩을 하지 않는다. 기본 화면용 캐시도 애니메이션이 있으면 갱신한다. 완료 hold 트랙의 불필요한 UI 재그리기 최적화는 남아 있다.

예:
```json
"extensions": {"animations": [
  {"id":"slide", "property":"x", "duration":2, "loop":true,
   "easing":"ease-in-out", "trigger":"screen.enter", "finish":"hold", "policy":"restart",
   "keys":[{"time":0,"value":100},{"time":2,"value":700}]}
]}
```

미구현: Tint RGB, 스프라이트 FPS/프레임 순서/UV 애니메이션, 중첩 정책, 게임 시간/일시정지, screen.exit/combo.break/result/song 이벤트, 에디터 애니메이션 UI. 에디터는 기존 extensions 보존만 하며 전용 편집/실시간 재생 루프 연결은 없다. loader 단독 호출의 전용 트랙 검사는 아직 없고 renderer 준비 시 검증한다. 이 제한을 완성된 4차 기능으로 보고하지 않는다.

검증: BmsPlayer 빌드, 보간·반복·완료·4개 주사율 시간 평가·JSON·잘못된 duration 단위 검사 및 기존 스킨 검사. 실제 빠른 연속 판정의 시각 결과, 일시정지 동기화, 에디터 미리보기/저장 비교, 장시간 게임 플레이는 검증하지 않았다.

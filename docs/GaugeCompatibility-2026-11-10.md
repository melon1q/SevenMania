# 게이지 호환 계산기 작업 상태 (2026-11-10)

기본 실행은 기존 `GrooveGauge`를 계속 사용한다. 명시적인 시험 옵션으로만
7KEYS 계산기를 실제 게임에 연결한다. 아래 미구현 항목을 완료하기 전에는
완전한 구동기 호환성을 주장할 수 없다.

## 조사 결과

- 기존 `src/GrooveGauge.h`는 NORMAL/EASY/HARD 및 별도 확장 규칙을 사용한다.
- `src/Main.cpp`의 `showTimingJudgement`와 HCN 처리, 지뢰 피해 경로가
  게이지에 각각 연결되어 있다. 판정의 EMPTY_POOR 구분과 롱노트 이벤트를
  먼저 대조해야 새 계산기로 안전하게 교체할 수 있다.
- `src/ChartRules.h`는 명시된 양수 TOTAL을 우선하고, 나머지는 노트 수에
  따른 기본값을 사용한다. BMSON 상대 TOTAL은 `src/BmsonParser.h`에서
  이미 변환되므로 새 계산기에서 다시 변환하면 안 된다.
- 기록과 클리어 램프는 기존 경로를 유지한다. 마이그레이션이나 램프 우선순위는
  아직 검증하지 않았다.

## 추가한 계산기

`src/GaugeCompatibility.h`의 `Bank`는 beatoraja의 7KEYS와 그 내장 LR2
프로필의 표를 사용하며 6개 일반 게이지와 3개 코스 게이지의 값을 독립적으로
보관한다. 각 판정은 배열에 적용되며, HARD의 감소 보정은 감소 직전 값에
대한 엄격한 미만 비교를 사용한다. 해당 구동기의 LR2 프로필은 HARD의
30% 미만 보정을 사용하며, 이것을 LR2oraja의 32% 규칙과 동일하다고
취급하지 않는다. 이후 고정된 LR2oraja 소스의 계산을 실행해 독립
프로필과 대조했으며 상세 범위는 `GaugeIndependentComparison-2026-11-11.md`에 기록한다.

계산기는 이미 해석된 양수 TOTAL과 양수 노트 수를 받는다. 비정상 TOTAL이나
0노트는 `configure`가 `false`를 반환하므로 호출자는 결과를 확인해야 한다.
이 계산기는 미기입 TOTAL을 결정하지 않으며, 실패한 설정 뒤의 이전 상태를
사용해서도 안 된다. 계산 결과의 완전한 참조 구동기 일치는 검증되지 않았다.

## 참조 및 라이선스

수치와 계산 순서는 2026-11-10에 확인한 beatoraja master의
[GaugeProperty.java](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/play/GaugeProperty.java),
[GrooveGauge.java](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/play/GrooveGauge.java)를
기반으로 했다. 2026-11-11 확인한 참조 커밋은
`43073536893d7a95be42a85044ffa91143c053a9`이다. 계산기는 해당 구현을 변형한 코드이며 GPL-3.0-only
표시와 원본 라이선스 사본 `docs/licenses/beatoraja-GPL-3.0.txt`를 포함한다.
배포 또는 통합 전에 SevenMania 전체의 라이선스 호환성과 소스 제공 의무를
확인해야 한다. 이 문서는 프로젝트 전체의 라이선스를 지정하지 않는다.

## 수행한 검증과 남은 작업

Release 단위 테스트에서 0노트 및 잘못된 TOTAL 거부, NORMAL 증감과
EMPTY_POOR 구분, 최대·최소값, 생존형 실패 후 회복 불가, HARD의
10/20/30/40/50% 경계를 검사한다. 기준 구현을 실행하여 동일 이벤트
시퀀스의 값을 비교한 테스트는 아직 없다. 실제 게임 플레이와 성능 측정도
실시하지 않았다.

## 2026-11-11 독립 계산기 확장

- `Bank::mineDamage`는 살아 있는 각 게이지에 지뢰의 피해량을 직접 적용한다.
  판정표와 GUTS는 적용하지 않고 게이지별 최소치를 지킨다.
- `Bank::hcnTick`은 한 번의 HCN 몸통 틱을 GREAT 또는 BAD의 0.5배로
  적용한다. 틱 발생 시점과 누르고 있는 상태의 판단은 호출자의 책임이다.
  기준은 고정한 참조 커밋의 `JudgeManager.java`이다.
- `GaugeAutoShift.h`는 참조 `BMSPlayer.java`의 NONE, CONTINUE,
  SURVIVAL TO GROOVE, BEST CLEAR, SELECT TO UNDER의 선택 로직을
  독립 계산기에 추가한다. 표시 게이지 변경과 실패 신호만 처리하며,
  각 게이지 값은 이벤트 발생 시점부터 독립적으로 누적한다.
- 유효하지 않은 채보로 `configure`에 실패한 경우 이전 곡 값이 남지 않도록
  계산기 상태를 비운다.
- 자동 전환은 실제 게임 판정, 설정, 기록에 아직 연결되지 않았다.
  원본 Java 실행 결과와의 비교도 수행하지 않았다.

## 실플레이 시험 경로

`BmsPlayer.exe --gauge-compat-test`로 시작하면 기존 NORMAL/EASY/HARD
선택에 7KEYS 계산기를 사용한다. 시험 실행 시 다음 옵션으로 6종을 직접
선택하고 자동 전환을 설정할 수도 있다.

```text
--gauge-kind=assist-easy|easy|normal|hard|ex-hard|hazard
--gauge-profile=7keys|lr2|lr2oraja
--gauge-auto-shift=none|continue|survival-to-groove|best-clear|select-to-under
--gauge-bottom=assist-easy|easy|normal|hard|ex-hard|hazard
```

예: `BmsPlayer.exe --gauge-compat-test --gauge-kind=hard --gauge-auto-shift=survival-to-groove`.
이 값들은 설정 파일에 쓰지 않으며 지정한 경우 선곡 화면의 기존 게이지
선택보다 우선한다. `--gauge-kind`를 생략하면 기존 선택값을 사용한다.
자동 전환은 모든 게이지가 곡 시작부터 같은 이벤트를 받는 은행을 사용하고,
실제 표시 게이지와 클리어·실패 판정도 전환된 값을 사용한다.
시험 모드의 `lr2`는 beatoraja 내장 LR2 테이블을 뜻한다. LR2oraja와
동일하다는 의미가 아니다. `lr2oraja`는 독립적으로 검증된 세 번째 계산
프로필이다. 일반 플레이의 LR2oraja 시험은 미기입 BMS TOTAL과 BMSON 1.0
상대 TOTAL에 LR2oraja 기준 기본값을 사용한다. 연습 모드에서는 기존
SevenMania의 TOTAL 처리를 유지하며 기준 구동기와 비교하지 못했다.
시험 경로에서는 각 판정·지뢰·HCN 틱 직후 전환을 평가한다. 참조 구현의
프레임 처리 시점과 차이가 있으므로 동시 입력에서의 완전 일치는 미검증이다.

일반 판정, 지뢰 피해, 기존 HCN 틱,
게이지 고갈 및 클리어 표시가 기존 게임 경로를 통해 연결된다. 결과 기록은
시험 모드에서 저장하지 않는다. 선택한 채보의 전용 게이지 규칙은 기존대로
우선하며, 노트 수가 0이라 새 계산기를 설정할 수 없는 경우 기존 계산으로
돌아간다. 실행 옵션 없이 시작하면 기존 동작을 유지한다.

이 경로는 검증용이며 아직 EMPTY_POOR 입력 이벤트, HCN 판정 상태별 시간
누적의 원본 일치, 6종 및 GAS의 설정 UI, 클리어 램프와
기록 연동을 지원하지 않는다. HCN 틱 발생은 기존 스케줄러를 그대로 사용하므로
beatoraja와 완전히 동일하다고 보장하지 않는다.

`build-note-image` Release 빌드 및 게이지·판정·기록·롱노트 관련 8개
회귀 테스트는 통과했다. 별도의 `BmsCompatibilityPlaybackTests`는 실제
WASAPI 시작과 지뢰 입력 검사를 지난 뒤 CPU/GPU 노트 픽셀 비교에서
실패했다. 해당 실패의 원인은 아직 확인하지 못했다. 실제 사용자 채보를
완주하며 확인하는 수동 플레이 및 성능 측정은 수행하지 않았다.

남은 작업: 판정별 이벤트와 LN/CN/HCN·지뢰의 의미 비교 및 게임 연결, 전체
TOTAL 처리, LR2oraja 검증, Gauge Auto Shift의 게임 연결, 플레이 실패와
코스 진행, 스킨/UI, 결과·기록 마이그레이션, 기준 구동기와의 이벤트별
대조 및 회귀 테스트. 이 항목은 구현된 것으로 간주하지 않는다.

# BMS 사양 호환성 종합 점검 보고서

작성일: 2026-10-23
대상: BmsPlayerCpp의 현재 작업 트리. 기준 HEAD는 `bce7bf9`이며, 이미 존재하던 staged/unstaged 변경도 포함하여 실제 개발 중인 소스를 분석했다. HEAD만의 감사 결과가 아니다.

**결론:** 7+1키의 기본 BPM·키음·BGM 재생 경로는 구현되어 있다. 그러나 현재 상태를 beatoraja 수준의 BMS 호환으로 보기는 어렵다. 특히 RANDOM의 고정 분기, 일부 마디 길이 누락, 판정/게이지 헤더 무시, LN 종류 미구분, BGA 이벤트별 영상 재생 부재가 실제 플레이 결과를 바꿀 수 있다. 실행 가능 여부와 사양을 충실히 재현하는지는 별개다.

이번 작업은 분석 보고서만 추가했다. 소스·테스트 코드는 수정하지 않았고, 배포 실행파일 교체·Git 커밋도 하지 않았다. 기존 테스트를 다시 빌드하고 실행했으므로 무시된 빌드 디렉터리와 테스트용 출력은 생성/갱신될 수 있다.

## 1. 판단 기준과 참고 자료

| 분류 | 의미 |
|---|---|
| ① 정상 구현됨 | 명시한 범위에서 파싱→데이터→실제 소비 경로가 연결됨. 모든 파생 사양/환경의 무결성을 보장하는 표현은 아님 |
| ② 부분 구현됨 | 일부 문법/정상 예제는 동작하나, 의미 누락·다른 동작·연결되지 않은 부분이 있음 |
| ③ 미구현 | 해당 명령의 의미를 처리하는 파서 또는 소비 경로가 없음. 조용히 무시하는 경우도 포함 |
| ④ 의도적으로 비활성화됨 | 명시적인 건너뛰기·미지원 경고·실행 차단 코드가 있음. 과거 의사결정의 이유까지 추정하지는 않음 |
| ⑤ 코드만으로 확인 불가 | 구조는 확인되지만 설치 코덱·장치·실제 출력처럼 환경/실측이 필요한 주장 |

각 항목의 **구현 분류**와 **검증 범위**는 별도다. “테스트 통과”는 기존 테스트가 검사한 주장만 검증한다. 테스트가 없는 결함을 정상으로 간주하지 않았다.

비교 자료:

- [BMS Command Memo](https://hitkey.bms.ms/cmds.htm): 2014-07-11 갱신 자료. 보편적인 단일 규격서가 아니라 실행기별 차이를 정리한 메모이므로, 오래된 확장을 모든 실행기의 필수 사양으로 취급하지 않았다.
- [beatoraja 확장 문서](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/manual/extension.txt): PREVIEW, LNMODE, DEFEXRANK 등의 문서. 문서 자체는 2017년 작성되어 최신 소스와 함께 참고했다.
- [공식 jbms-parser의 BMSDecoder.java](https://raw.githubusercontent.com/exch-bms2/jbms-parser/master/src/bms/model/BMSDecoder.java), [Section.java](https://raw.githubusercontent.com/exch-bms2/jbms-parser/master/src/bms/model/Section.java): 헤더와 채널 처리 비교.
- [beatoraja LaneRenderer.java](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/src/bms/player/beatoraja/play/LaneRenderer.java), [JudgeManager.java](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/src/bms/player/beatoraja/play/JudgeManager.java), [GrooveGauge.java](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/src/bms/player/beatoraja/play/GrooveGauge.java): 파싱 이후 소비 경로 비교.

외부 소스는 점검일에 열람한 `master`이므로 추후 변할 수 있다. beatoraja 실행본을 직접 실행하거나 특정 배포본에 들어 있는 jbms-parser JAR와 소스의 일치를 검증하지 않았다. 이하 “참고 소스”는 이 한계를 전제로 한다. Memo에 있다는 이유만으로 beatoraja에도 있다고 단정하지 않았다. 예를 들어 참고 BMSDecoder에서 ELSEIF/ELSE, SWITCH, STP, BASEBPM, 동적 EXRANK의 처리 근거는 찾지 못했다.

## 2. 항목별 요약

상세 근거·잠재 문제·권장 조치는 다음 절에 있다. 위치의 함수는 별도 표기가 없으면 `BmsParser::parse`다.

| 기능 | 분류 | 핵심 근거 / 현재 상태 | 검증 |
|---|---|---|---|
| 초기 `#BPM` | ① | [BmsParser.cpp:391](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:391), [BmsParser.cpp:419](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:419) 양의 BPM을 초기 시계에 적용 | 기존 파서 테스트 통과 |
| 기본 BPM 변경 `#xxx03` | ① | [BmsParser.cpp:485](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:485) 16진수 BPM을 시간·스크롤에 반영 | 기존 파서 테스트 통과 |
| 확장 `#BPMxx` / `#xxx08` | ① | [BmsParser.cpp:391](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:391), [BmsParser.cpp:489](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:489) 정의 참조·소수 BPM 처리 | 기존 파서 테스트 통과 |
| `#STOPxx` / `#xxx09` | ② | [BmsParser.cpp:494](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:494) 일반 STOP은 연결됨. 중첩 STOP 처리 등이 참고 구현과 다름 | 일반/고속/연속 STOP 테스트 통과; 호환 차이 존재 |
| `#RANDOM` | ② | [BmsParser.cpp:330](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:330) 범위를 읽지만 결과를 항상 1로 고정 | 고정 1번 결과에 의존하는 기존 테스트만 통과 |
| `#IF / #ELSEIF / #ELSE / #ENDIF` | ② | [BmsParser.cpp:335](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:335) 조건 스택은 있으나 RANDOM 상태가 부정확 | IF/ELSE 일부만 검증; ELSEIF·복잡한 중첩 미검증 |
| `#ENDRANDOM` | ③ | [BmsParser.cpp:358](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:358) 명령을 인식하고 넘어갈 뿐, 난수 스코프 복구 없음 | 전용 테스트 없음 |
| `#WAVxx` 및 BGM `#xxx01` | ② | [BmsParser.cpp:362](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:362), [AudioEngine.cpp:170](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:170), [AudioEngine.cpp:492](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:492) 정상 재생 연결. 임의 복구 규칙이 사양 의미를 바꿀 수 있음 | 파서·오디오 기존 테스트 통과 |
| 마디 길이 `#xxx02` | ② | [BmsParser.cpp:147](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:147), [BmsParser.cpp:302](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:302) `:0.5` 가능, `:2`는 데이터 줄로 인식하지 못함 | 소수 길이만 기존 테스트 통과; 정수 결함은 정적 확인 |
| `#LNOBJ` | ② | [BmsParser.cpp:521](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:521) 길이 변환은 함. 이후 종류별 판정 구분 없음 | 변환 테스트 통과; 표준 LN 판정 일치 미검증 |
| `#LNTYPE 1` / 롱노트 채널 | ② | [BmsParser.cpp:546](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:546), [LongNoteHold.h:14](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/LongNoteHold.h:14) 시작/끝을 묶으나 해제/점수 규칙은 자체 구현 | 변환·현재 홀드 규칙 테스트 통과 |
| `#LNTYPE 2` | ④ | [BmsParser.cpp:423](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:423), [BmsParser.cpp:762](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:762) 명시적으로 실행 차단 | 차단 테스트 통과 |
| `#SCROLL` | ④ | [BmsParser.cpp:400](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:400) 미지원 경고. `SC` 채널도 타임라인에 연결 안 됨 | 전용 테스트 없음 |
| Invisible Note | ④ | [BmsParser.cpp:440](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:440) 31–39 채널을 명시적으로 버림 | 공음 교체 의미 미검증/미지원 |
| `#RANK` | ③ | [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.h:57](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:57), [Judgement.h:13](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Judgement.h:13) 저장/판정 적용 없음 | 자체 판정 테스트만 통과; 헤더 연동 미지원 |
| `#EXRANK` / `#DEFEXRANK` | ③ | [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [Judgement.h:35](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Judgement.h:35) 정의·변경 이벤트·비율 반영 없음 | 미검증 |
| `#TOTAL` | ③ | [GrooveGauge.h:14](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/GrooveGauge.h:14), [Main.cpp:1508](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1508) 노트 수·사용자 모드만 게이지에 전달 | 자체 게이지 테스트만 통과; 헤더 연동 미지원 |
| `#LNMODE` | ③ | [BmsParser.h:12](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:12), [LongNoteHold.h:14](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/LongNoteHold.h:14) LN/CN/HCN 구분 필드·동작 없음 | 종류별 동등성 미검증 |
| Mine Note | ④ | [BmsParser.cpp:455](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:455) 미지원 경고 후 이벤트를 생성하지 않음 | 피해·입력·표시 미검증/미지원 |
| BGA 및 레이어 | ② | [BmsParser.cpp:509](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:509), [Main.cpp:771](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:771), [Main.cpp:2252](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2252) 이미지 합성은 연결. 블랙 키·기본 POOR·영상 이벤트 등 누락 | 파서·래스터·단일 AVI 테스트 통과 |
| `#BASEBPM` | ③ | [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [ScrollTimeline.h:11](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/ScrollTimeline.h:11) 초기 BPM 기준의 내부 좌표와 다른 명령이며 미처리 | 미검증 |
| `#SWITCH` 및 관련 명령 | ③ | [BmsParser.cpp:330](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:330), [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394) 조건 제어 없음. 각 CASE 안의 내용이 함께 들어갈 수 있음 | 미검증 |
| `#SPEED` | ③ | [BmsParser.cpp:117](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:117), [BmsParser.cpp:455](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:455) `SP` 채널/정의·보간 경로 없음 | 미검증 |
| `#STP` | ④ | [BmsParser.cpp:399](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:399) 경고만 남기며 정지 시간은 적용하지 않음 | 전용 테스트 없음 |
| `#PREVIEW` | ① | [BmsParser.cpp:652](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:652), [SongPreview.h:66](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/SongPreview.h:66), [Main.cpp:963](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:963) 파일 선택→디코드→선택 화면 재생 연결 | 파서·프리뷰 기존 테스트 통과; 모든 형식은 미검증 |
| `#TEXT` / `#TEXTxx` | ③ | [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.h:57](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:57) 문자열 이벤트/표시 경로 없음 | 미검증 |
| `#CHARSET` | ③ | [BmsParser.cpp:62](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:62), [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394) 선언 무시. UTF-8→CP932 자동 변환만 구현 | UTF-8·CP932 기존 테스트 통과 |
| 모든 코덱·실제 장치에서의 A/V 지연 일치 | ⑤ | [AudioEngine.cpp:86](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:86), [Main.cpp:1743](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1743), [VideoBga.h:17](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/VideoBga.h:17) 구조만으로 모든 환경 보장 불가 | 아래 8절의 제한된 환경만 검증 |

## 3. 필수 기능 상세

### 3.1 BPM / 직접·확장 BPM — ① 정상 구현됨

**위치와 연결:** [BmsParser.cpp:391](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:391)에서 초기/확장 정의를 수집하고, [BmsParser.cpp:445](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:445)에서 마디 위치를 박자로 변환한다. [BmsParser.cpp:475](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:475)의 시간 누적 및 [BmsParser.cpp:485](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:485)의 03/08 처리에서 당시 BPM에 따라 초 단위 이벤트를 만든다. [ScrollTimeline.h:11](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/ScrollTimeline.h:11)의 `scrollPositionSeconds`와 [Main.cpp:2828](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2828)의 노트 좌표 계산, [AudioEngine.cpp:208](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:208)의 BGM 샘플 프레임 예약, [Main.cpp:1701](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1701)의 입력 판정이 이 시간을 사용한다.

양의 초기 BPM, 03의 16진수 정수 BPM, 08의 소수 포함 확장 BPM은 실제 재생 경로가 있다. BPM 변경과 STOP이 같은 박자이면 BPM이 먼저 적용된다. 08/09의 영문 ID 대소문자 정규화도 있다.

**잠재 문제:** 미정의/유효하지 않은 초기 BPM은 120으로 복구한다. `#EXBPMxx` 별칭은 미지원이다. 직접 BPM의 `std::stoi`는 소비 길이를 확인하지 않으므로 `1G`처럼 일부만 해석되는 잘못된 값에 대한 엄격성이 부족하다. 음수 BPM 및 BASE62는 지원 범위가 아니다. 같은 위치의 중복/03·08 경쟁은 입력 순서 기반이며 참고 실행기와 모든 우선순위가 같다는 테스트는 없다.

**권장:** 정상 경로를 유지하면서 별칭, 전체 토큰 검증, 미정의 값 진단, 동일 위치 병합 정책을 추가한다. 음수 BPM의 오래된 연출은 별도 호환 옵션으로 검토한다.

**검증:** 기존 타이밍·직접 BPM·확장 BPM·고속 BPM 테스트 통과. 앞의 중복/비정상 입력 및 모든 실곡 출력은 미검증.

### 3.2 STOP — ② 부분 구현됨

**위치와 연결:** [BmsParser.cpp:392](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:392) 정의, [BmsParser.cpp:190](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:190) 우선순위, [BmsParser.cpp:494](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:494) 정지 적용. 시간은 `STOP값 × 60 / 당시 BPM / 48`로 증가한다. 정지 구간의 내부 스크롤 속도는 0으로 설정하며, 같은 박자의 노트·BGM·BGA는 정지 시작에 놓는다. 이미 울리는 음원과 영상 자체를 정지시키는 기능은 아니다.

이 순서는 Memo에 정리된 BPM/음원 이후 STOP 처리와 맞는다. [BMS Command Memo의 STOP 설명](https://hitkey.bms.ms/cmds.htm)

**잠재 문제:**

- **정의 없는 STOP은 의도적으로 지연 0으로 간주하며, 해당 위치에서 경고도 남기지 않는다.** [BmsParser.cpp:499](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:499)의 실험적 복구 정책이다. 이 경우 “정상 재현”이 아니라 강제 실행이다.
- 같은 위치에 여러 09 객체가 있으면 모두 누적한다. 기존 테스트도 이 동작을 정답으로 고정한다. 참고 Section은 위치별 map에 저장하므로 같은 위치가 겹치면 마지막 값으로 대체하는 경로다. [참고 Section 구현](https://raw.githubusercontent.com/exch-bms2/jbms-parser/master/src/bms/model/Section.java)
- 음수 정의는 버리고, 분수 STOP은 실수 그대로 적용한다. 역사적 실행기별 차이가 있는 영역이다.

**권장:** 누락 정의는 계속 실행하더라도 복구 내역을 남긴다. 동일 위치 STOP을 병합할지 누적할지 목표 호환 정책을 정한 뒤 기존 테스트도 그 정책에 맞게 조정한다. 음수/분수는 명시적인 지원 범위를 문서화한다.

**검증:** 일반·분수 마디와 결합·동일 박자 BPM/BGM/BGA·STOP을 가로지르는 LN·고속 STOP의 현재 동작은 테스트 통과. beatoraja와 중복 STOP 결과의 동등성은 보장되지 않는다.

### 3.3 RANDOM 조건문 — ② 부분 구현됨, ENDRANDOM 의미는 ③

**위치:** [BmsParser.cpp:285](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:285), [BmsParser.cpp:330](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:330), [BmsParser.cpp:335](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:335), [BmsParser.cpp:358](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:358).

`#RANDOM n`이 유효해도 `randomValue = 1`만 실행한다. IF/ELSEIF/ELSE는 현재 값과 조건 스택을 비교하며 활성 여부에 따라 데이터·정의를 걸러낸다. ENDIF는 부모 활성 상태를 복구하지만 ENDRANDOM은 아무 상태도 복구하지 않는다. `#SETRANDOM n`도 인식만 하고 값을 대입하지 않는다.

**호환 문제:** 2번 이상 분기는 선택되지 않는다. 독립 난수의 중첩·스코프 복원이 없고 비활성 블록에서도 제어 명령을 먼저 처리한다. 괄호가 맞지 않는 조건문이나 잘못된 인수에 대한 진단이 부족하다. 단순 IF/ELSE가 동작한다는 이유로 RANDOM 지원 완료로 판단할 수 없다.

**권장:** 난수/조건 상태를 분리한 스택, 실제 난수 또는 재현 가능한 seed/선택 기록, SETRANDOM, ENDRANDOM 복원 및 잘못된 블록 검증을 구현한다. 곡 목록 분석·실제 로딩·프리뷰 사이에는 같은 선택 결과를 재사용해야 한다.

**검증:** [ParserTests.cpp:204](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/ParserTests.cpp:204)의 기존 테스트는 RANDOM 2에서 IF 2가 아닌 ELSE가 나오는 것을 검사한다. 난수의 다양성, ELSEIF, ENDRANDOM 복원, 중첩 조건은 미검증이다.

### 3.4 WAV 및 BGM — ② 부분 구현됨

**연결:** [BmsParser.cpp:362](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:362) 정의 → [BmsParser.cpp:507](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:507) BGM 이벤트 → [BmsParser.cpp:564](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:564) 필요한 샘플 해석 → [AudioEngine.cpp:86](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:86) `decodeFile` → [AudioEngine.cpp:170](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:170) `prepare` → [AudioEngine.cpp:492](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:492) `fillBuffer`. 입력 키음은 [Main.cpp:1701](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1701)에서 `AudioEngine::trigger`로 보낸다. 여러 BGM 줄은 이벤트를 추가하므로 동시 재생이 가능하다.

Ogg Vorbis는 내장 디코더를 먼저 사용하고, 다른 형식/실패 시 Media Foundation을 사용한다. 파일이 없으면 같은 stem의 WAV/OGG/MP3, 일부 깨진 한국어 파일명, gm_001→gm_000 규칙으로 복구한다. 누락 음원 때문에 이미 읽은 노트를 모두 버리지는 않는다.

**호환 문제:** [BmsParser.cpp:241](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:241)의 `loadSharedDefinitions`는 다른 채보의 WAV 선언을 조건문 평가 없이 수집한다. 원래 무음이어야 하는 미정의 ID가 다른 난도/비활성 분기의 음원으로 바뀔 수 있다. gm_001 대체도 다른 샘플을 울릴 가능성이 있다. WAV/BMP 파일명의 따옴표는 PREVIEW와 달리 제거하지 않는다. 전체 사전 디코딩 512MB 제한은 음원 집합에 따라 로딩을 차단할 수 있다. 기본 키음 입력은 다음 믹싱 블록에 전달되므로 시간 데이터가 정확해도 실제 장치 지연은 별도다.

**권장:** 표준 모드의 미정의 WAV는 무음으로 유지하고, 곡별 복구는 별도 호환 옵션/진단으로 구분한다. 공통 경로 파서, 조건부 정의 처리, 코덱별 자료 세트를 추가한다.

**검증:** WAV 생성·디코드·BGM 믹싱, MP3 UI 음원, 현재 WASAPI 장치·버퍼/다성 재생 및 프리뷰 경로는 기존 테스트 통과. 전체 Ogg/기타 코덱 조합, 모든 샘플레이트/비트 깊이, 장치별 물리 지연은 미검증.

### 3.5 마디 길이 #xxx02 — ② 부분 구현됨

**위치:** [BmsParser.cpp:147](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:147) `tryDataLine`, [BmsParser.cpp:302](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:302) 길이 저장, [BmsParser.cpp:445](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:445) 마디 누적.

유효한 길이를 `4 × 비율`의 박자 길이로 계산하고 노트·BGM·BGA·BPM·STOP의 위치 변환에 공통으로 사용한다.

**확정적인 정적 결함:** `tryDataLine`은 전체 길이 9 이상을 요구한다. `#00102:2`는 8자라서 데이터 줄로 인식하지 못하고 일반 헤더 경로로 넘어간 뒤 무시된다. 예를 들어 BPM 120에서 001마디를 2배로 하고 002마디에 노트를 두면, 정상 기대 위치는 6초지만 현재 코드의 예상 위치는 4초다. `#00102:2.0`은 통과한다. **이 예제는 코드로 도출했으며 이번에 새 fixture로 실행하지 않았다.**

양의 값이더라도 16 초과는 거부한다. 비율 정의만 있는 마지막 마디는 maxMeasure 계산에 포함되지 않는 점도 길이/종료 판단의 경계다.

**권장:** 데이터 줄의 최소 길이와 02 전용 토큰 검증을 분리한다. `:1, :2, :0.5, :16.5` 및 복합 BPM/STOP/BGA 시간을 검증한다.

**검증:** 기존 `:0.5` 테스트 통과. 한 자리 정수 및 16 초과는 미검증.

### 3.6 LNOBJ / LNTYPE / 롱노트 채널 — ②, LNTYPE 2는 ④

**위치:** [BmsParser.cpp:397](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:397) 선언, [BmsParser.cpp:521](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:521) LNOBJ 변환, [BmsParser.cpp:546](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:546) 51–59 짝짓기, [BmsParser.h:12](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:12) Note, [LongNoteHold.h:14](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/LongNoteHold.h:14) LongNoteHold, [Main.cpp:1701](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1701) 시작, [Main.cpp:1743](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1743) 유지, [Main.cpp:4578](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:4578) 키 해제, [Main.cpp:2859](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2859) 렌더링.

LNOBJ 직전 같은 레인의 일반 노트를 시작점으로 바꾸고 마커를 제거한다. 롱노트 채널은 레인별 시간순으로 시작/끝 두 개씩 묶는다. 길이는 BPM/STOP을 포함한 초 단위이며 몸통·꼬리 렌더링까지 연결되어 있다. 1P 7키와 스크래치가 대상이다.

**호환 문제:**

- Note에 종류와 끝점 sampleId가 없다. 롱 채널 끝점 키음은 저장/재생되지 않는다.
- 모든 LN에 동일한 자체 해제 규칙을 쓴다. 끝 근처 해제는 PGREAT, 너무 이르면 POOR, 계속 누른 채 끝+허용창을 넘기면 BAD다. 일반 LN의 종료 자동 처리, CN의 끝 판정, HCN의 유지/회복 게이지를 구분하지 않는다.
- 0.1초마다 올라가는 콤보는 HUD용 자체 규칙이다. [Main.cpp:752](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:752)에서 게이지/정확도 점수에 반영하지 않으므로 **HCN 구현 근거가 아니다.**
- 중복/교차 LN, 같은 시각의 일반 노트, 홀수 끝점 처리에 정보 손실 가능성이 있다. 현재는 겹친 일반 노트를 지우고, 홀수 남은 이벤트는 경고한다.
- LNTYPE 2는 명시적으로 차단한다. 이 제한을 LNMODE 미지원과 혼동하면 안 된다.

**권장:** 파싱 표기(LNOBJ/RDM/MGQ)와 플레이 종류(LN/CN/HCN)를 분리하고, 끝점 음원·노트 수/점수 단위·게이지·스크래치 규칙을 함께 설계한다.

**검증:** LNOBJ·LNTYPE 1 변환, STOP 포함 길이, LNTYPE 2 차단, 현재 홀드 경계/재시작/콤보 tick은 기존 테스트 통과. beatoraja의 LN/CN/HCN 결과 동등성은 미검증이며 현재 구조로는 지원 완료가 아니다.

## 4. 중요 확장 기능 상세

### 4.1 SCROLL — ④ 의도적으로 비활성화됨

[BmsParser.cpp:400](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:400)은 SCROLL 헤더에 미지원 오류를 추가할 뿐 정의를 저장하지 않는다. [BmsParser.cpp:117](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:117) `parseChannel`은 10진수 우선/16진수 fallback이어서 `SC`와 `SP`를 해석하지 못하고, [BmsParser.cpp:455](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:455)도 이 채널들을 타임라인에 넣지 않는다.

`Chart.scroll`과 [ScrollTimeline.h:11](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/ScrollTimeline.h:11)은 **BPM/STOP으로 유도한 내부 이동 좌표**다. 명령어 `#SCROLL`의 배율 지원이 아니다. 참고 소스는 SCROLL 정의/SC 채널과 SPEED 정의/SP 채널을 별도로 관리하고 렌더러에서 소비한다. [Section](https://raw.githubusercontent.com/exch-bms2/jbms-parser/master/src/bms/model/Section.java), [LaneRenderer](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/src/bms/player/beatoraja/play/LaneRenderer.java)

**문제/권장:** 현재는 연출 없는 상태로 실행될 수 있다. 채널을 명시적으로 정규화하고 배율 타임라인을 추가하되, 역방향 이동에서는 [Main.cpp:2833](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2833)의 위치 기준 lower_bound가 전제하는 단조성도 재설계해야 한다. 현재 지원을 넓게 표시하지 않는다. 전용 검증 없음.

### 4.2 Invisible Note — ④ 의도적으로 비활성화됨

[BmsParser.cpp:440](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:440)에서 31–39를 건너뛰며 숨은 노트/공음 전환 이벤트를 만들지 않는다. [Main.cpp:1731](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1731)의 빈 키 입력은 일반 노트의 과거/첫 샘플을 찾아 재생한다.

**문제:** Invisible Note가 의도한 빈 키음 변경을 재현하지 못한다. 화면에 보이지 않는다는 것만으로 지원이라고 볼 수 없다.

**권장:** 점수·노트 수·렌더링과 분리된 레인별 숨은 키음 타임라인을 만들고 입력 시 조회한다. 2P는 현재 플레이 모드 제한과 함께 따로 다룬다. 의미 검증 테스트 없음.

### 4.3 RANK / EXRANK / DEFEXRANK — ③ 미구현

[BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394) 헤더 수집과 [BmsParser.h:57](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:57) Chart에 필드가 없다. [Judgement.h:13](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Judgement.h:13) `judgementWindows`는 사용자 설정 Basic/Easy/Normal/Hard의 고정 창이며 [Main.cpp:1730](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1730)에서 이를 사용한다. 곡의 RANK와 설정창 판정 모드는 연결되어 있지 않다.

**문제:** 같은 채보라도 지정 판정보다 느슨하거나 엄격해질 수 있다. 정적 DEFEXRANK와 정의+이벤트 방식의 동적 EXRANK는 다른 기능이며 둘 다 없다.

**권장:** 곡 판정값·사용자 오버라이드 우선순위를 정하고 판정 상태를 시점별로 조회한다. 참고 beatoraja 문서의 DEFEXRANK 기준은 EASY 100이며 Memo의 다른 실행기 예시와 그대로 혼용하면 안 된다. [beatoraja 확장 문서](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/manual/extension.txt)

**검증:** 고정 판정 경계 테스트 통과만 확인. 헤더 연동·동적 변경은 미지원/미검증.

### 4.4 TOTAL — ③ 미구현

[BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394)에서 무시되고, [Main.cpp:1508](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1508)은 `notes.size()`와 사용자 게이지 모드만 [GrooveGauge.h:14](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/GrooveGauge.h:14) `GrooveGauge::configure`에 넘긴다. 회복량은 `200 / noteCount` 기반이다.

**문제:** 채보가 지정한 TOTAL에 따른 회복 난도를 재현하지 못한다. LN의 시작/끝을 어떻게 세는지도 TOTAL과 함께 맞춰야 한다.

**권장:** TOTAL 저장·미지정 fallback·모드별 회복식을 구현하고 실제 판정 소비에 연결한다. 현재 게이지 테스트는 자체 수식을 검증할 뿐 TOTAL 호환 테스트가 아니다.

### 4.5 LNMODE — ③ 미구현

[BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.h:12](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:12), [LongNoteHold.h:14](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/LongNoteHold.h:14)에 선언/종류/종류별 처리가 없다. 참고 beatoraja의 `1=LN, 2=CN, 3=HCN`과 선언 우선 규칙을 적용하지 않는다. [확장 문서](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/manual/extension.txt), [JudgeManager](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/src/bms/player/beatoraja/play/JudgeManager.java)

**권장:** 3.6절의 타입·끝점·판정·게이지 설계를 함께 진행한다. UI 옵션만 추가해서는 해결되지 않는다. 종류별 검증 없음.

### 4.6 Mine Note — ④ 의도적으로 비활성화됨

[BmsParser.cpp:455](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:455)은 D1–D9 및 그 뒤 E9까지의 넓은 수치 범위를 미지원 경고로 처리한다. 실제 Note/이벤트는 만들지 않는다. [BmsParser.h:12](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:12)에는 피해량/지뢰 타입도 없다.

**문제:** 지뢰가 없어지고 누르고 있어도 피해가 없다. WAV00 폭발음·렌더링·접촉 판정이 모두 연결되지 않는다. 범위 검사도 D/E 레인 채널을 정확히 열거하는 방식이 아니다.

**권장:** 일반/롱노트와 다른 이벤트 타입, 값의 피해량 해석, WAV00, 입력 유지 구간 접촉, 게이지와 점수 제외 규칙을 구현한다. 참고 소스의 MineNote 생성과 JudgeManager 피해 처리처럼 양쪽 연결을 확인해야 한다. [JudgeManager](https://raw.githubusercontent.com/exch-bms2/beatoraja/master/src/bms/player/beatoraja/play/JudgeManager.java)

**검증:** 지원 경로 없음. 실제 지뢰 채보 재생 미검증.

### 4.7 BGA / BGA 레이어 — ② 부분 구현됨

**연결:** [BmsParser.cpp:363](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:363) BMP/BGA 정의 → [BmsParser.cpp:509](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:509) 04/06/07/0A 이벤트 → [Main.cpp:771](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:771) `updateBgaState` → [Main.cpp:2252](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2252) `drawBgaBackplate` → [BgaRaster.h:115](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BgaRaster.h:115) `BgaRaster::scaled` / [BgaRaster.h:151](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BgaRaster.h:151) `draw`. CPU와 GPU 경로가 같은 배경 합성 결과를 사용한다.

정지 이미지는 베이스·레이어·레이어2·POOR 순서로 합성하며 PNG 알파, crop, 캐시, 화면 크기 변경을 처리한다. POOR 발생 시 약 2.5초의 표시 경로도 있다.

**주요 누락/문제:**

1. **전통적 레이어의 검정 투명 처리 부재.** BgaRaster는 파일 알파만 사용한다. 알파 없는 BMP의 검정 부분은 불투명하게 복사되므로 BASE를 가린다. Memo의 LAYER 설명과 다른 동작이다. [BMS Command Memo](https://hitkey.bms.ms/cmds.htm)
2. **기본 POOR BMP00 미설정.** 정의는 저장해도 플레이 시작 시 활성 이미지를 빈 값으로 초기화한다([Main.cpp:1522](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:1522)). 06 이벤트 전의 기본 miss 이미지가 선택되지 않는다.
3. **영상은 폴더에서 고른 한 개 fallbackVideo.** [BmsParser.cpp:711](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:711)에서 선택하고 하나의 재생기를 연다. BMP 이벤트마다 영상 열기/교체/재시작·다중 레이어 영상 재생을 하지 않는다. 이미지 이벤트는 진행해도 영상 자체는 독립 단일 경로다.
4. **영문 BMP ID 시작 시각 조회 불일치.** 이벤트 ID는 대문자, bitmaps 키는 소문자다. [BgaTimeline.h:20](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BgaTimeline.h:20)의 `bgaVideoStartSeconds`는 `find(event.imageId)`를 그대로 사용한다. 예: BMP0A와 04의 0A가 연결된 영상은 작성된 시작 이벤트를 놓쳐 첫 음원 시각으로 fallback할 수 있다. 정지 이미지 렌더러는 소문자화하므로 서로 결과가 다르다. **정적 확인, 직접 재현 미실행.**
5. **확장자/미정의 이미지 정책 차이.** BMP 경로는 WAV처럼 확장자 대체 탐색을 하지 않는다. 미정의 이미지 이벤트는 그 레이어를 못 그리지만 아래 fallback 영상은 계속 보일 수 있어, 암전을 의도한 채보가 달라질 수 있다.
6. `#BGAxx` crop은 구현되지만 목적지 배치가 640×480을 기준으로 한다. 고전 캔버스 규칙과의 일치, 음수 목적지와 영상 crop은 추가 검증이 필요하다. 모든 구현의 캔버스 정책이 같다는 의미는 아니다.
7. `.m1v/.m2v`는 현재 fallback 영상 확장자 목록에 없다. MF 코덱 지원 여부와 별개로 탐색 단계에서도 제한된다.

**권장:** 먼저 ID 정규화, 레이어별 블랙 키, 기본 POOR를 수정한다. 이후 BMP 자산별 영상 상태/이벤트 재시작/시킹을 설계하고 저작된 타임라인을 기준으로 재생한다. 옵션 fallback과 정식 BMS 이벤트를 구분한다.

**검증:** 타이밍 계산·PNG 알파/crop·래스터·단일 생성 AVI의 첫 프레임/시킹/재시작 테스트는 통과했다. 위 누락들을 검증하는 fixture와 실곡의 CPU/GPU 시각 비교는 이번에 실행하지 않았다.

### 4.8 BASEBPM — ③ 미구현

[BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394)에서 무시한다. [ScrollTimeline.h:11](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/ScrollTimeline.h:11)/[BmsParser.cpp:479](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:479)의 초기 BPM 기반 내부 스크롤 좌표가 BASEBPM 선언을 대신하지 않는다.

**문제/권장:** 이 헤더를 사용하는 채보의 기준 배속을 재현하지 못한다. 오디오용 BPM과 표시 배속의 기준값을 별도로 정의해야 한다. 오래된 실행기 종속 확장이므로 우선순위는 SCROLL보다 낮게 두어도 된다. 참고 LaneRenderer의 `basebpm` 변수명만으로 해당 헤더 지원이라고 추론하지 않았다. 전용 검증 없음.

## 5. 추가 확장 기능 상세

| 기능·분류 | 소스/함수와 현재 동작 | 잠재 문제 / 권장 수정 | 검증 |
|---|---|---|---|
| `#SWITCH / #SETSWITCH / #CASE / #SKIP / #DEF / #ENDSW` — ③ | [BmsParser.cpp:330](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:330)–[BmsParser.cpp:400](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:400)에 제어 상태가 없음. 각 명령은 알 수 없는 헤더로 무시 | CASE 안의 노트/WAV/BGM은 활성 일반 데이터로 합쳐질 수 있음. fallthrough·SKIP·DEF·중첩을 포함한 전처리기를 만들거나, 지원 전에는 무조건 합치지 않도록 진단 | 미검증 |
| `#SPEED` / `#SPEEDxx` — ③ | [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.cpp:117](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:117), [BmsParser.cpp:455](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:455)에 정의와 SP 이벤트 없음 | 사용자 노트 배속 옵션과 별개. 목표 문법의 보간/시간 의미를 정하고 파서→렌더 좌표까지 연결. 참고 Section/LaneRenderer에서 SPEED가 별도로 소비됨 | 미검증 |
| `#STP` — ④ | [BmsParser.cpp:399](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:399) 미지원 경고만 추가 | 마디.위치 및 ms 정지를 적용하지 않아 이후 전체 싱크가 달라질 수 있음. STOP 테이블과 다른 단위로 파싱한 뒤 공통 시간 이벤트로 정규화 | 미검증 |
| `#PREVIEW` — ① | [BmsParser.cpp:652](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:652) 후보 경로, [SongPreview.h:66](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/SongPreview.h:66) 준비, [SongPreview.h:150](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/SongPreview.h:150) 재생, [Main.cpp:963](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:963) 곡 선택에서 소비 | 명시 파일/대체 확장자/자동 후보/채보 합성의 우선순위가 있음. 명시 프리뷰 파일을 고르면 그 파일의 시작부터 재생하는 경로라 다른 preview-offset 확장과 함께 쓸 때 의미를 문서화할 필요 | 파서 및 실제 프리뷰 기존 테스트 통과. 모든 형식 미검증 |
| `#TEXT / #TEXTxx`, 관련 텍스트 채널 — ③ | [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.h:57](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:57), [Main.cpp:2807](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2807)에 명령용 텍스트 이벤트/소비가 없음 | 판정/HUD 텍스트가 있다는 사실과 별개. 문자열 정의·시각·지속/교체 정책을 구현 | 미검증 |
| `#CHARSET` — ③ | [BmsParser.cpp:62](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:62) `decodeBytes`는 UTF-8 BOM 제거/유효 UTF-8 시도 후 CP932. 선언을 읽지 않음 | EUC-KR/CP949 본문, UTF-16 BOM은 오해석 가능. 한국어 파일명 복구는 본문 인코딩 지원이 아님. BOM 우선과 제한된 자동 감지/설정 override를 먼저 추가하고 필요 시 오래된 CHARSET도 지원 | UTF-8/CP932만 기존 테스트 통과; 다른 본문 인코딩 미검증 |

SWITCH·STP·TEXT·CHARSET은 역사적/실행기별 확장이다. “Memo에 있음”과 “현행 beatoraja 공통 지원”을 구분해야 한다. [BMS Command Memo](https://hitkey.bms.ms/cmds.htm)

## 6. 추가로 발견한 명령과 범위 제한

| 항목 | 분류·소스 | 현재 동작 / 문제 / 권장 |
|---|---|---|
| `#SETRANDOM` | ③ [BmsParser.cpp:330](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:330) | 인수는 읽지만 적용하지 않음. 고정값 분기와 스코프 구현 필요. 전용 테스트 없음 |
| `#EXBPMxx` | ③ [BmsParser.cpp:391](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:391) | BPMxx 별칭을 인식하지 못함. 필요하면 동일 테이블로 정규화. 전용 테스트 없음 |
| `#BASE 62` | ③ [BmsParser.cpp:320](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:320), [BmsParser.cpp:362](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:362) | 헤더 무시·ID 대문자화/키 소문자화로 대소문자 구분 ID 충돌. BASE36은 일반적으로 사용 가능하나 BASE62를 지원한다고 표시하면 안 됨. 채널과 객체 ID 진법을 분리할 것. 전용 테스트 없음 |
| `#VOLWAV` | ③ [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [AudioEngine.cpp:517](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/AudioEngine.cpp:517) | 사용자 볼륨만 적용. 곡 헤더 배율을 별도로 보존/반영할 것. 전용 테스트 없음 |
| `#PATH_WAV` | ③ [BmsParser.cpp:631](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:631) | 기본적으로 채보 부모 경로에 결합. 헤더 기준 폴더를 해석하지 않음. 전용 테스트 없음 |
| `#TITLE / #SUBTITLE / #ARTIST / #GENRE / #PLAYLEVEL` | ① [BmsParser.cpp:403](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:403), [Main.cpp:2458](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2458) 및 곡 선택 표시 | 저장과 선택 화면 소비 확인. 기본 UTF-8 메타데이터 일부는 파서 테스트 통과. 다중 SUBTITLE 등의 오래된 중첩 정의는 마지막 값만 남음 |
| `#DIFFICULTY` | ② [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.cpp:417](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:417), [BmsParser.h:57](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:57) | 임시 headers에는 들어가지만 Chart에 카테고리를 보존하지 않음. PLAYLEVEL과 분리한다는 주석만 있고 명시 난도 정보가 후속 표시까지 이어지지 않음. 테스트는 PLAYLEVEL이 덮이지 않는지만 확인. 명시 카테고리를 저장하고 제목/파일명 추정보다 우선할 것 |
| `#SUBARTIST / #COMMENT / #BANNER` | ③ [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.h:57](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.h:57) | 저장/표시 연결 없음. 메타데이터 필드와 UI 소비 추가. 전용 테스트 없음 |
| `#STAGEFILE / #BACKBMP` | ① [BmsParser.cpp:650](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:650), [BmsParser.cpp:689](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:689), [Main.cpp:2544](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2544), [Main.cpp:2644](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/Main.cpp:2644) | 이미지 경로가 titleImage/finishImage에 연결되고 로딩/결과 화면에서 소비. 경로 파싱 테스트 통과. 다양한 실이미지 시각 출력은 미검증 |
| `#PREVIEWPOINT / #PREVIEWSTART / #PREVIEWDURATION` | ① [BmsParser.cpp:408](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:408), [BmsParser.cpp:652](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:652), [SongPreview.h:66](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/SongPreview.h:66) | 이 프로젝트가 수용하는 추가 메타데이터. START가 POINT보다 우선, 양의 DURATION은 1–120초 제한. 채보 합성 프리뷰에 반영. 메타데이터·프리뷰 테스트 통과. 모든 실행기 공통 사양이라고 주장할 근거는 없음 |
| `#BGAxx` | ② [BmsParser.cpp:364](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:364), [BgaRaster.h:115](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BgaRaster.h:115) | crop 정의와 이미지 표시 연결 있음. 고전 캔버스/목적 좌표 의미는 추가 비교 필요. 현재 자체 crop 테스트만 통과 |
| `#VIDEOFILE / #EXBMP / #ARGB / #SWBGA / #@BGA` 및 opacity 채널 | ③ [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394), [BmsParser.cpp:455](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:455), [BgaRaster.h:151](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BgaRaster.h:151) | 명령에 따른 정의·이벤트·소비 없음. 폴더 영상 자동 선택/PNG 알파와 다른 기능. 목표 확장부터 명시적으로 선택해 구현. 전용 테스트 없음 |
| `#WAVCMD`, 소재/옵션 계열 `#MATERIALSWAV / #MATERIALSBMP / #OPTION` | ③ [BmsParser.cpp:394](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:394) | 명령 처리 없음. 현 시점에는 인지 가능한 미지원 진단과 지원 범위 문서가 우선. 전용 테스트 없음 |
| `#PLAYER` 및 5/10/14키·PMS | ② / ④ [BmsParser.cpp:171](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:171), [BmsParser.cpp:425](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:425) | 모드는 식별하나 실행은 SevenKey만 허용. 지원 표시는 7+1키로 한정. 모드 차단 기존 테스트 통과 |
| bmson | ③ [BmsParser.cpp:263](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:263), [BmsParser.cpp:147](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:147) | 현재 BMS 텍스트 파서의 별도 형식 지원으로 확인되지 않음. JSON 파서/모델 변환이 필요. 이번 감사의 핵심 BMS 텍스트 범위 밖이며 재생 미검증 |

위 “미구현”은 저장소의 src 명령 검색, 파서의 전체 헤더 분기, Chart 필드와 소비 경로를 대조한 결과다. 런타임에서 무시되는 명령의 모든 변형을 실제 파일로 시험했다는 의미는 아니다.

## 7. 권장 수정 우선순위

| 우선순위 | 대상 | 이유·구현 방향 |
|---|---|---|
| 높음 | 한 자리 02, 영문 BGA ID | 정상적인 짧은 문법으로 타이밍/영상 시작이 달라짐. 작은 파서/정규화 수정 후 재현 fixture 필요 |
| 높음 | RANDOM/SETRANDOM/ENDRANDOM 및 SWITCH 진단 | 다른 채보 분기가 실행되거나 여러 분기가 합쳐짐. 한 번 정한 분기 결과를 목록/프리뷰/본 재생에서 유지 |
| 높음 | RANK/DEFEXRANK/TOTAL | 눈으로 파일이 재생되는 것과 관계없이 판정 난도·클리어 결과가 달라짐 |
| 높음 | LNMODE + LN/CN/HCN와 끝점 음원 | 길이만 파싱하는 현재 모델을 확대하고 판정·게이지·점수·키음·렌더링을 함께 검증 |
| 높음 | BGA 블랙 키/BMP00/이벤트별 영상 | 전통 이미지 레이어와 단일 영상 외의 연출에 구조적 누락 |
| 중간 | STOP 중복 정책·누락 진단, WAV 복구 분리 | 강제 실행과 사양 호환을 명확히 구분. 복구 때문에 원본 의미가 조용히 바뀌지 않게 할 것 |
| 중간 | Invisible / Mine / SCROLL / SPEED | 공음·피해·이동 연출이 플레이에 영향을 줌. 별도 타입과 타임라인을 설계 |
| 중간 | BASE62·본문 인코딩·공통 경로 파서 | 일본어/한국어·대소문자 ID·파일 탐색 오류를 구조적으로 해결 |
| 낮음 또는 수요 확인 | BASEBPM, STP, TEXT, SWITCH 완전 구현과 기타 역사적 확장 | 실곡 수요와 목표 실행기 지원을 먼저 정한 뒤 구현. 지원 전에도 진단은 개선 |

지원 예정 여부와 무관하게 `partialLoad/playable`의 뜻을 명확히 해야 한다. [BmsParser.cpp:760](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/src/BmsParser.cpp:760)은 일부 오류가 있어도 7키 노트가 남으면 실행을 허용한다. SCROLL/STP/Mine 경고가 이 정책으로 통과하는 것은 기능 지원이 아니다. 반대로 RANK/TOTAL/SWITCH처럼 모르는 명령은 경고조차 없으므로 partialLoad 플래그만으로 모든 비호환을 검출할 수 없다.

## 8. 실행한 기존 테스트와 한계

환경: Windows, `build-performance-hybrid` / Release, Visual Studio 18 2026. 캐시에서 mimalloc·oneTBB·하이브리드 렌더러·NVIDIA Reflex ON, Tracy OFF를 확인했다. 이 설정을 변경하지 않았다.

소스 변경 없이 기존 대상만 다시 빌드했고 빌드는 성공했다. mimalloc new/delete 재정의 관련 기존 C4559 경고는 있었으며 빌드 실패는 아니었다.

```powershell
cmake --build build-performance-hybrid --config Release --target BmsCoreTests BmsLongNoteTests BmsJudgementTests BmsGrooveGaugeTests BmsBgaTests BmsVideoTests BmsAudioTests --parallel 4
ctest --test-dir build-performance-hybrid -C Release -R '^(BmsCoreTests|BmsLongNoteTests|BmsJudgementTests|BmsGrooveGaugeTests|BmsBgaTests|BmsVideoTests|BmsAudioTests)$' --output-on-failure
```

**결과: 7/7 통과, 실패 0, 총 5.05초.** 테스트 프로그램 수이며 개별 assertion 수가 아니다.

| 테스트 | 확인한 범위 | 이번 결과로 보장하지 않는 것 |
|---|---|---|
| [ParserTests.cpp:57](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/ParserTests.cpp:57) BmsCoreTests | BPM/STOP/분수 마디·키 매핑·LN 변환·일부 분기·인코딩·샘플 복구·PREVIEW | 전체 확장 문법, 랜덤 다양성, 1자리 02, 모든 실곡 |
| [AudioTests.cpp:105](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/AudioTests.cpp:105) BmsAudioTests | WAV 디코드·믹싱, 실제 WASAPI 버퍼/다성/underrun 회복, 프리뷰, 연습 배속·시킹, MP3 UI 음원 | 모든 코덱/드라이버, 물리 입력-출력 지연, 청감상 모든 실곡 싱크 |
| [LongNoteTests.cpp:1](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/LongNoteTests.cpp:1) BmsLongNoteTests | 현재 홀드 시작/해제 경계·유지·클리핑·리셋·tick | 표준 LN/CN/HCN에 대한 적합성 |
| [JudgementTests.cpp:1](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/JudgementTests.cpp:1) BmsJudgementTests | 현재 고정 판정창 경계 | RANK/EXRANK 연동 |
| [GrooveGaugeTests.cpp:1](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/GrooveGaugeTests.cpp:1) BmsGrooveGaugeTests | 현재 Normal/Easy/Hard 수식과 클리어/실패 | TOTAL 연동 |
| [BgaTests.cpp:20](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/BgaTests.cpp:20) BmsBgaTests | 색·리사이즈·PNG 알파·crop·캐시·누락 파일·tint | BMP 블랙 키·BMP00·이벤트 기반 다중 영상 |
| [VideoBgaTests.cpp:33](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/tests/VideoBgaTests.cpp:33) BmsVideoTests | 생성한 비압축 AVI의 첫 프레임·시킹·재시작·비동기 준비/취소·누락 영상 | 모든 실곡 코덱, 장시간 A/V drift, 영문 BMP ID 파서 결합 |

상세 실행 로그: [LastTest.log:1](C:/Users/yakko/Documents/Codex/2026-10-09/new-chat-2/outputs/BmsPlayerCpp/build-performance-hybrid/Testing/Temporary/LastTest.log:1)

이번에는 새 테스트나 fixture를 추가하지 않았다. UI/전체 인게임 실곡 재생·화면 캡처·beatoraja와 나란히 실행하는 비교도 하지 않았다. 따라서 정적으로 드러난 결함 외의 실제 체감 지연, 오디오 출력과 영상의 물리적 싱크, GPU/CPU 표시의 픽셀 일치는 **미검증**이다. 해당 검증을 하지 않은 점을 일반 기능이 미구현이라는 근거로 사용하지도 않았다.

## 9. 다음 검증에 필요한 사례

코드를 고치는 다음 작업에서는 최소한 아래 사례를 별도 적합성 fixture로 두는 것이 좋다. 이번 작업에서는 생성하거나 실행하지 않았다.

1. 동일 채보에서 02의 `2`와 `2.0`이 정확히 같은 노트/BGM/BGA 시간을 만드는지.
2. RANDOM의 전체 분기, SETRANDOM, 비활성 블록 내부 중첩, ENDRANDOM 복원, 같은 선택 결과 재로딩.
3. RANK/DEFEXRANK/TOTAL 변경으로 판정창과 실제 게이지가 바뀌는지.
4. LNOBJ·롱 채널 각각의 LN/CN/HCN, 끝점 음원, 겹침·스크래치·중간 이탈/복귀.
5. BMP0A의 작성 시각, BMP00 POOR, 검정 BMP 레이어, 영상 교체/같은 영상 재시작/다중 영상.
6. 동일 위치 STOP 중복의 목표 실행기 비교, 누락 STOP 및 음원 복구의 진단 표시.
7. Invisible의 공음 변경, 지뢰 입력 피해/WAV00, SCROLL/SC와 SPEED/SP의 정지·역방향·보간.
8. BASE62 대소문자 ID, UTF-16 BOM/EUC-KR 본문, 다양한 파일 확장자와 샘플 규격.

# BMS 호환성 구현 및 검증 결과

작성일: 2026-10-23. 이전 `BMS-Compatibility-Audit-2026-10-23.md`는 수정 전 상태의 기록으로 보존했다.

감사의 필수·중요·추가 항목과 6절의 추가 발견 항목을 대상으로 파싱 이후의 오디오·판정·게이지·CPU/GPU 표시 경로를 연결했다. bmson 및 5/10/14키·PMS도 포함한다. 다만 **모든 역사적 실행기 버전과의 완전한 동일성, 임의 코덱, 물리 A/V 지연을 검증했다는 뜻은 아니다.** 특히 구형 OPTION의 공개 자료에 수치가 없는 시각 효과는 이 프로젝트의 명시적인 재현 프로필을 사용한다.

## 1. 구현 범위

| 대상 | 현재 처리 및 소비 경로 | 검증 |
|---|---|---|
| BPM / 03 / BPMxx·08 / EXBPMxx | 직접 BPM 토큰 전체 검증, 확장 별칭. 초 단위 시계와 시각 타임라인, 키음·BGM 예약에 적용 | 기존 타이밍 검사 + 17번 샘플 |
| STOP·09 | 같은 위치의 마지막 09 선언을 적용. 소수 STOP 허용, 음수 값은 절댓값 사용. 미정의 값은 지연 0으로 실행하되 복구 진단 표시 | 중복·연속·고속·LN 교차 검사, CPU/GPU STOP 정지/재개 픽셀 검사 |
| RANDOM / SETRANDOM / IF / ELSEIF / ELSE / ENDIF / ENDRANDOM | 실제 난수, 난수·조건 스택 분리, 중첩 스코프 복원. 세션/경로별 같은 seed로 목록·프리뷰·재로딩의 분기 유지 | 64개 seed의 4분기, 중첩 및 비활성 분기, 02번 |
| SWITCH / SETSWITCH / CASE / SKIP / DEF / ENDSW | 선택값, CASE fallthrough, SKIP, 기본 분기, 중첩 조건 평가 | 02번 및 제어 흐름 assertion |
| WAV / BGM·01 | 독립 BGM 줄 유지, 파일 확장자 대체. 사양 경로와 기존 sibling-WAV/GM 복구를 구분하고 복구 진단 제공 | 기존 오디오·파서 검사, 실제 믹서 |
| 마디 길이·02 | 한 자리 정수와 소수 모두 허용. 이후 이벤트·LN 시간에 적용 | 01번 정수/소수의 동일한 6초 위치 |
| LNOBJ | 여러 종료 ID, 일반 채널의 LN 변환, 끝점 키음, 혼합 LNOBJ/롱 채널 처리 | 22번, 파서 및 실제 입력/끝점 오디오 검사 |
| LNTYPE 1 / 2, 롱 채널 | RDM의 시작·끝 쌍과 MGQ의 연속 슬롯/마디 연결을 각각 처리 | 06–07번, 기존 롱노트 검사 |
| LNMODE 1/2/3 | LN 자동 종료, CN 해제 판정, HCN 이탈 후 복귀 및 유지 게이지. CN/HCN 끝점 점수·TOTAL용 개수 포함 | 실제 게임의 press/key-up/update 경로, 자동 종료와 tail trigger 검사 |
| SCROLL·SC | signed 시각 속도 적분. 정지/역방향은 노트 검색의 단조성 가정을 해제. 판정·오디오 시계는 유지 | 04번, 좌표/STOP 화면 검사 |
| Invisible·3x/4x | 화면에 노트를 만들지 않고, 해당 레인의 최신 공음 변경을 실제 키 입력에서 사용 | 05/12번, trigger queue 검사 |
| RANK / DEFEXRANK / EXRANK·A0 | 곡의 초기/동적 비율을 실제 입력·miss·LN 판정창에 적용 | 05번, 판정 함수·입력 검사 |
| TOTAL | 헤더 또는 기본 TOTAL 식을 게이지 증가량에 반영. Mine/HCN/강제 게이지와 연결 | 05번 게이지 증가/피해 수치 및 실제 입력 |
| Mine·Dx/Ex | 표시, 입력 유지 중 교차 피해, WAV00, Hard 실패 처리 | 05/12번, 실제 gauge 감소 검사 |
| BGA·04/06/07/0A | BASE/POOR/LAYER/LAYER2, BMP00 기본 POOR, 검정색 키 합성, POORBGA 모드 | 10번, BMP 합성 픽셀, 영상 레이어 검사 |
| 이벤트 기반 영상 | 이벤트별 시작 시각, 같은 BMP ID 재호출 시 재시작, 레이어별 독립 재생. 오디오 시작 전에 첫 프레임 준비 | 합성 AVI 첫 프레임·진행·재시작·레이어별 서로 다른 색 검사 |
| BASEBPM | 초기 BPM과 분리된 시각 기준 BPM. 오디오 이벤트 시간은 원래 BPM으로 계산 | 17번의 6초 노트 및 시각 위치 |
| SPEED·SP | 기본 1.0부터 이벤트 사이 선형 보간, 실시간 노트 이동에 적용 | 04번의 중간값 1.5 검사 |
| STP | 마디/위치와 밀리초를 STOP과 별개로 누적 | 03번의 3.25초 노트 |
| PREVIEW 계열 | 기존 별도 음원/합성 프리뷰 유지. bmson preview_music도 연결 | 기존 오디오/프리뷰 검사, 23번 |
| TEXT·99 | 문자열 정의와 시각 이벤트를 저장하고 플레이 HUD에서 현재 문자열 표시 | 05번 textAt 검사, 23번 표시용 샘플 |
| CHARSET | UTF BOM 우선, UTF-8/CP932/CP949 선언 처리. BOM 있는 UTF-16/32 LE/BE 지원 | 실제 CP949 BMS 제목, Unicode decoder 검사 |
| BASE 16/36/62 | 객체 ID와 채널 해석 분리. BASE62 대소문자 ID 충돌 방지. BASE 헤더가 뒤에 있어도 적용 | 08/30번, 실제 WAV 조회 |
| VOLWAV / WAVCMD | 곡/샘플 볼륨, 음정, 재생 길이를 ID별 디코딩 버퍼에 적용 | 09번, 실제 PCM 프레임 수 검사 |
| PATH_WAV | WAV 기준 디렉터리에 적용 | 20번 및 내보낸 상대 경로 디코딩 검사 |
| TITLE / SUBTITLE / ARTIST / GENRE / PLAYLEVEL | 기존 메타데이터 경로 유지 | 기존 파서·목록 검사 |
| DIFFICULTY | 명시 난도 필드를 저장해 제목/파일명 추정보다 우선 사용 | 파서/곡 목록 회귀 검사 |
| SUBARTIST / COMMENT / BANNER | 아티스트 보조 표시, F1 상세 정보, 곡 선택 이미지로 연결 | 메타데이터 assertion. 모든 이미지/폰트의 육안 검사는 미실시 |
| STAGEFILE / BACKBMP | 기존 타이틀/결과 배경 경로 유지. bmson의 대응 이미지 연결 | 기존 파서/화면 검사 |
| BGAxx / @BGAxx | 고전 256 캔버스 crop/목적 좌표, @BGA의 width/height 문법, 음수 source clip 보정 | 기존 crop 회귀 및 10번 |
| VIDEOFILE / VIDEO F/S | 명시 fallback 영상, 작성 FPS에 따른 영상 시계 배율 | AVI의 실제 프레임 색 검사 |
| EXBMP / ARGB·A1–A4 | 이미지/레이어별 알파 및 색 키를 CPU 합성에 적용하고 GPU 배경에 반영 | 10번 실제 픽셀 알파/블랙 키 검사 |
| opacity·0B–0E | BASE/LAYER/LAYER2/POOR 각각 16진수 00–FF. 00을 빈 객체로 버리지 않도록 처리 | 31번 FF→00 이벤트 검사 |
| SWBGA·A5 | 레인 입력으로 시작하는 프레임 애니메이션, 반복/유지 시간/색·알파 | 10번 키 입력 후 프레임/종료 시각 검사 |
| MATERIALSWAV / MATERIALSBMP / `<folder>file` | 실행파일 옆 Materials 및 대체 공유 소재 경로. 채보 폴더와 구분 | 26/27/29번 파일 조회와 실제 이미지 디코딩 |
| OPTION | HI-SPEED/BASE-SPEED, CHANGE-HS, HIDDEN, RANDOM, NOTES, CRAZY, GAMELEVEL/EXTRA, 구형 별칭, HDX BGAMODE/LONGMODE. 판정/게이지/오디오/렌더에 반영 | 21/24/32번 및 함수·게이지·입력·영상 검사. 아래 프로필 한계 참고 |
| PLAYER, 5/10/14키/PMS | 모드 차단 해제, 레인 geometry·키 입력·hold·beam·피드백 확장 | 13–16번 실제 CPU/GPU 캡처, GPU readback의 노트 위치/색 일치 검사 |
| bmson | JSON 모델, pulse BPM/STOP/SCROLL, 메타데이터, 음원 슬라이싱·continuation·겹친 키음 합성, LN/Mine/Invisible/BGA | 11번의 pulse 시각, slice 프레임 수, 실제 BGM 믹싱 검사 |

## 2. 소스 구조

- `BmsParser.cpp::BmsParser::parse`: BMS 명령·타이밍·자산·진단 조립.
- `BmsControlFlow.h::BmsControlFlow`: 조건문과 난수 스코프.
- `BmsText.h::decodeBmsText`: 인코딩 선언/BOM/자동 감지.
- `BmsonParser.h::parseBmson`, `JsonValue.h::JsonReader`: bmson 읽기와 pulse→기존 Chart 변환.
- `ChartRules.h`: 판정 비율, TOTAL, 보간 속도, 숨은 키음/텍스트 조회, 레인 레이아웃.
- `ChartOptions.h`: 정적 채보 변경, 강제 옵션과 구형 시각 효과 프로필.
- `AudioEngine.cpp::prepare`: ID별 WAVCMD/VOLWAV 및 bmson slice/group PCM 생성.
- `LongNoteHold.h`, `GrooveGauge.h`: LN 종류별 종료, HCN tick 및 TOTAL/강제 게이지.
- `Main.cpp::pressLane`, `windowProc`의 key-up, `updatePlaybackFrame`, `drawGameScreen`: 실제 입력·음원·판정·화면 소비.
- `BgaRaster.h`, `BgaSwitchState.h`, `BgaVideoLayers.h`, `AsyncVideoBga.h`, `VideoBga.h`: 이미지/영상 합성 및 이벤트별 시계.
- `CompositionPresenter.h`: 회전 노트 sprite의 GPU clipping.

자산이 누락되거나 영상 디코딩에 실패해도 가능한 채보는 실행하며 F1 상세 사유에 남긴다. 기존 강제 WAV 복구가 필요한 곡은 기본 동작을 유지한다. 원본 선언만 검사하려면 `BmsParser(false, seed, false)`로 복구를 끌 수 있다.

## 3. 호환 프로필과 남아 있는 검증 한계

1. **판정:** RANK의 상대값은 25/50/75/100/125, EXRANK는 NORMAL=100 비율로 해석한다. beatoraja의 현행 규칙과 오래된 확장 문서 표현이 달라 현행 소스를 기준으로 선택했다. 이 게임의 사용자 판정 프리셋을 그 비율로 조정하므로 beatoraja의 모든 비대칭 판정창/스킨 판정 규칙과 완전히 같다는 의미는 아니다.
2. **LN:** 끝점 점수와 자동 종료/해제/HCN 복귀를 구분했다. 유지 중 0.1초마다 화면 콤보를 갱신하는 기존 게임 기능은 유지한다. 이것은 beatoraja 원본 콤보 규칙과 별도의 이 게임 표현이다.
3. **구형 OPTION:** nanasigroove 1.552의 공개 표를 기준으로 GTP/GLV를 해석한다. 1.2x나 조사되지 않은 2.x의 동작을 동시에 재현하지 않는다. CHANGE-HS·wave·clone·size 등의 진폭/주기는 공개 수치가 없으므로 `ChartOptions.h`에 고정한 프로필이다. 화면 스킨 자체의 원본 재현은 대상이 아니다. 알 수 없는 vendor/값은 조용히 성공 처리하지 않고 부분 로드 사유로 기록한다. `#CHANGEOPTION`·A6은 이번 감사 목록에 없으며 추가 구현하지 않았다.
4. **bmson:** 1.0.0 및 legacy 필드 별칭, beat-5k/7k/10k/14k와 popn-9k를 지원한다. 임의의 generic-nkeys/keyboard 계열 mode_hint는 진단 후 차단한다. 사용자 요청에 있는 5/10/14/PMS 이외의 별도 악기 모드는 확장하지 않았다.
5. **인코딩:** 선언/BOM 없는 CP932와 CP949는 항상 구분할 수 없다. 휴리스틱을 사용하며 선언/BOM이 우선한다. 모든 IANA charset 이름을 지원하는 범용 변환기는 아니다.
6. **오디오/영상:** WAV와 생성한 비압축 AVI, 기존 테스트용 MP3 및 실제 WASAPI 경로를 검사했다. 모든 압축 BGA 코덱·드라이버, 장시간 실곡 drift, 물리 입력/오디오/모니터 지연과 beatoraja 실행본의 나란한 비교는 **미검증**이다. OS Media Foundation 코덱 및 기존 메모리 한도의 영향을 받는다.
7. **시각 검사:** CPU/GPU의 주요 노트 좌표·색을 실제 GPU readback으로 비교하고 5/10/14/PMS 캡처를 생성했다. 모든 구형 효과 조합의 전체 화면 픽셀 동일성은 검증하지 않았다.

따라서 “감사에서 지적한 명령의 처리 경로를 구현하고 샘플을 검증함”과 “모든 실행기의 모든 변형을 완벽히 지원함”을 구분한다. 후자는 이번 결과로 주장하지 않는다.

## 4. 재현 및 테스트

환경: Windows / Visual Studio 18 2026, `build-performance-hybrid`, Release. 기존 mimalloc·oneTBB·hybrid·NVIDIA Reflex 옵션 유지.

```powershell
cmake --build build-performance-hybrid --config Release --parallel 4
ctest --test-dir build-performance-hybrid -C Release -E '^BmsUiTests$' --output-on-failure
```

`BmsCompatibilityTests`는 새 BMS와 합성 WAV/BMP/AVI를 생성하고 모델·시간·규칙을 검사한다. `BmsCompatibilityPlaybackTests`는 숨겨진 테스트 창에서 실제 게임 입력/판정/게이지/CPU/GPU 경로와 MF 영상·PCM 믹서를 검사한다. 실행 중인 사용자 게임이나 개인 곡 파일은 사용하지 않는다.

최종 결과: **21/21 통과, 실패 0, 총 14.47초**. 새 폴더 `CompatibilityFresh`에서도 생성·재생 검증을 통과했고, 내보낸 **35개 샘플 전체의 파싱/음원 디코딩도 통과**했다. CN 종료의 실제 해제 오차에 따른 GREAT/GOOD 세분화를 보강한 뒤 관련 재생·홀드·화면 검사 3개도 통과했다. 전체 GUI 조작 테스트 `BmsUiTests`는 이번 실행에서 제외했다.

새 폴더에 fixture 생성 후 재생 검증하고, `tools/export_compatibility_samples.py`로 `samples/Compatibility`에 내보낸다. 내보낸 폴더도 검증기의 세 번째 인수로 지정하면 상대 경로와 **모든 샘플의 음원 디코딩**을 다시 검사한다.

```powershell
build-performance-hybrid/Release/BmsCompatibilityTests.exe build-performance-hybrid/CompatibilityFresh
build-performance-hybrid/Release/BmsCompatibilityPlaybackTests.exe build-performance-hybrid/CompatibilityFresh
python tools/export_compatibility_samples.py build-performance-hybrid/CompatibilityFresh samples/Compatibility
build-performance-hybrid/Release/BmsCompatibilityPlaybackTests.exe build-performance-hybrid/CompatibilityFresh samples/Compatibility
```

단계별 로그는 빌드 폴더의 `Testing/Temporary/LastTest.log`, CPU/GPU 캡처는 fixture 출력 폴더에 있다. 빌드·캡처·개인 파일은 Git 커밋 대상에서 제외하며 합성 샘플과 생성기만 포함한다.

## 5. 참고 자료

- [BMS Command Memo](https://hitkey.bms.ms/cmds.htm)
- [구형 OPTION 조사 자료](https://hitkey.nekokan.dyndns.info/option.htm)
- [jbms-parser BMSDecoder](https://github.com/exch-bms2/jbms-parser/blob/master/src/bms/model/BMSDecoder.java), [Section](https://github.com/exch-bms2/jbms-parser/blob/master/src/bms/model/Section.java)
- [beatoraja BMSPlayerRule](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/play/BMSPlayerRule.java), [JudgeManager](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/play/JudgeManager.java), [확장 문서](https://github.com/exch-bms2/beatoraja/blob/master/manual/extension.txt)
- [bmson 공식 사양 소스](https://github.com/bemusic/bmson-spec/blob/master/doc/index.rst)

문서/소스를 참고해 자체 구현했으며 외부 파서 구현을 복사하거나 새 외부 런타임 라이브러리를 추가하지 않았다.

검증한 실행파일은 `build-performance-hybrid/Release/BmsPlayer.exe`이며 `build/Release/BmsPlayer.exe`에도 같은 SHA-256의 실행파일을 반영했다. 기존 실행파일은 무시된 `build-compatibility-backup/normal-release-BmsPlayer.exe`에 보관했다. 개인 설정·곡·기록은 교체하지 않았다.

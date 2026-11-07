# SevenMania 스킨 3차 구현: 게임 데이터 바인딩

작성일: 2026-10-28

## 조사 결과

1차 공통 모델의 binding/text 필드와 SkinLoader::prepare 검증, 2차의 Direct2D 레이어·이미지 캐시·contain 출력을 확장했다. 별도의 폰트 엔진이나 외부 라이브러리를 추가하지 않았다.

- `bms::Chart`는 title/artist/playLevel을 wstring, initialBpm과 TempoEvent.bpm을 double로 보관한다. 난이도 원문은 숫자라고 보장되지 않는다. 기존 `ChartOrdering.h::numericPlayLevel()`로 숫자 해석이 가능한 경우만 제공한다.
- `ResultScore::value()`가 현재/최종 점수이며 같은 객체가 리절트까지 유지된다. `rank()`도 기존 함수다. 스킨용 점수/랭크 계산을 추가하지 않았다.
- `PlayFeedback::combo()/maxCombo()`와 `GrooveGauge::value()`가 기존 읽기 전용 접근점이다.
- 판정 HUD는 `gJudgementText`, `gJudgementUntil`을 사용한다. PGREAT와 GREAT의 표시 문자열이 모두 GREAT이므로 세부 판정을 임의로 복원하지 않는다.
- 리절트의 실제 표시 상태는 PRACTICE / FAILED / CLEAR / NO CLEAR다. 기존 drawResultScreen의 분기를 그대로 읽는다.
- 기존 GDI 글리프 캐시는 `TextRasterCache.h`이다. 검정/흰 배경의 DrawTextW 결과로 premultiplied 픽셀을 만드는 기능을 재사용한다. DirectWrite/별도 폰트 렌더러를 추가하지 않았다.
- 곡 가져오기/재생 준비 작업은 별도 worker의 gLoadResult를 만들고 UI 완료 경로에서 gCharts에 반영한다. 캡처는 UI 소유 스레드에서만 수행하며 loading/splash 동안에는 값을 제공하지 않는다. worker의 결과 구조는 읽지 않는다.

## 추가 파일

| 파일 | 역할 |
| --- | --- |
| `src/skin/SkinData.h`, `SkinData.cpp` | 키 레지스트리, 형식 있는 값, 소유 스냅샷, 공급자, 바인딩 컴파일·서식 변환 |
| `src/skin/SevenManiaSkinData.h` | 실제 게임 상태를 읽는 캡처 함수 선언 |
| `tests/SkinBindingTests.cpp` | 실제 Main.cpp/점수 계산 함수를 포함하는 바인딩·텍스트 통합 검증 |
| `tests/SkinRenderTestSupport.h` | 2·3차 테스트가 공유하는 DirectX 표면/픽셀 읽기/PNG 저장 도구 |
| `tests/fixtures/skins/data-binding.semaskin` | select/play/result의 최소 텍스트·숫자 검증 패키지 |
| `tests/fixtures/skins/generate-binding-fixture.py` | Python 표준 라이브러리만 사용하는 샘플 재생성 도구. 게임 실행 의존성이 아님 |
| `docs/SevenManiaSkin-Phase3-Implementation.md` | 이 보고서 |

## 기존 파일 수정

- `src/skin/SkinPackage.h`: SkinValue, TextAlign/TextVerticalAlign, SkinTextStyle, SkinNumberFormat, defaultValue 추가. ZIP/LR2와 무관한 공통 모델을 유지한다.
- `src/skin/SkinLoader.cpp`: 새 텍스트·숫자·기본값 속성 파싱 및 공통 모델 검증 추가.
- `src/skin/SkinRenderer.h/.cpp`: text/number 레이어, update, 진단 조회, 표시 문자열 조회, GDI 폰트 수명 관리 추가. 이미지와 텍스트를 같은 z 순서로 그린다.
- `src/Main.cpp`: 조건부 헤더와 읽기 전용 `captureSevenManiaSkinData()`만 추가했다. 기존 곡 선택/판정/점수/게이지/오디오/입력 로직을 바꾸지 않았다.
- `CMakeLists.txt`, `cmake/SkinSystem.cmake`: 새 모듈·테스트 및 기본 플레이어의 캡처 기능 연결. 전용 통합 테스트 이외의 기존 Main.cpp 테스트 타깃에는 캡처 코드를 강제하지 않는다.
- `tests/SkinTests.cpp`: 잘못된 텍스트/서식 속성 10종 추가.
- `tests/SkinRenderTests.cpp`: 기존 GPU 검증 코드를 공통 테스트 도구로 이동했다. 이미지 검증 내용과 assertion 수는 유지했다.
- `docs/SevenManiaSkinFormat-v1.md`: 새 JSON 규칙과 기본값, 폰트, 텍스트 출력 제한 반영.

## 바인딩 구조와 수명

`Main.cpp 읽기 전용 캡처 → SkinDataProvider → const SkinDataSnapshot → SkinBindingResolver → SkinRenderer::update → draw` 순서다.

레지스트리는 이름·내부 ID·자료형·화면 범위를 고정 테이블로 관리한다. prepare 시 각 text/number의 이름을 ID로 바꾼다. 잘못된 키·화면·number 자료형 연결은 진단 목록에 한 번 기록하고 해당 요소의 기본값을 사용한다. JSON 코드/표현식/템플릿/콜백을 실행하지 않는다. 이미지 속성 바인딩은 아직 실행하지 않는다.

공급자만 값을 쓸 수 있으며, 엔진 객체를 가리키는 포인터/콜백은 스냅샷에 없다. 스냅샷에는 string/int64/double/bool/값 없음만 저장한다. 문자열도 소유 사본이다. 스킨이 이 사본을 바꾸더라도 엔진 점수·판정·게이지를 변경할 경로가 없다.

호스트는 화면의 데이터를 읽을 때 beginFrame → set/setWide → endFrame을 실행한다. 매 프레임 제공되지 않은 값은 endFrame에서 제거하고, 화면이 바뀌면 이전 화면 값 전체를 비운다. renderer에 null/비활성/다른 화면 스냅샷을 넘겨도 이전 동적 표시를 기본값으로 돌린다. 제공된 빈 문자열·0·false는 결측이 아니다.

provider.snapshot()의 const 참조는 다음 갱신 전까지 현재 시점의 뷰다. 공급자 갱신과 renderer.update는 같은 소유 스레드에서 순차 호출한다. 필요하면 SkinDataSnapshot을 값으로 복사해 안전한 과거 사본을 보관할 수 있으나 그 사본이 최신 상태로 자동 변경되지는 않는다. 여러 스레드가 같은 provider/renderer를 동시에 사용하지 않는다.

실제 캡처 함수는 게임 시작/UI 소유 스레드 ID를 확인한다. 다른 스레드에서는 엔진 상태나 provider를 건드리기 전에 false를 반환한다. UI 스레드에서만 읽으므로 게임 상태 잠금이나 worker 대기를 추가하지 않는다. 원래 UI를 교체하거나 매 프레임 호출을 강제하지 않았으며, 개발용 통합 경로에서 실제 게임 상태를 읽어 표시한다.

## 지원 키와 실제 의미

| 키 | 자료형 | 범위 | 원본/의미 |
| --- | --- | --- | --- |
| song.title | string | 모든 화면 | 선택/활성 Chart.title |
| song.artist | string | 모든 화면 | Chart.artist |
| song.bpm | double | 모든 화면 | 선택·리절트는 initialBpm, 플레이는 기존 HUD와 같은 현재 TempoEvent.bpm |
| chart.level | double | 모든 화면 | numericPlayLevel로 해석 가능한 실제 playLevel만. 그 외 결측 |
| chart.levelText | string | 모든 화면 | playLevel 원문 |
| play.combo | int64 | play | PlayFeedback.combo() |
| play.score | int64 | play | ResultScore.value(), 현재 0~1,000,000 체계 |
| play.gauge | double | play | GrooveGauge.value(), 0~100 |
| play.judgement | string | play | 현재 표시 유효 시간 안의 gJudgementText, 만료 시 결측 |
| play.paused | bool | play | gPaused |
| result.score | int64 | result | 같은 ResultScore의 최종 값 |
| result.rank | string | result | ResultScore.rank() |
| result.clearType | string | result | PRACTICE / FAILED / CLEAR / NO CLEAR, 기존 UI 의미 |
| result.maxCombo | int64 | result | PlayFeedback.maxCombo() |
| result.cleared | bool | result | gResultGaugeCleared 원값. practice 여부는 clearType을 확인 |

제공하지 않는 데이터: 별도의 정확한 PGREAT/GREAT 판정 enum, 판정별 누적 개수, 임의 정확도, FULL COMBO/EX HARD 등 새 클리어 분류, LR2 전용 키. 기존 엔진에 없는 계산을 만들어 채우지 않는다. BPM은 기존 HUD의 채보 BPM이며 새 배속 보정 계산을 추가하지 않았다.

## JSON과 서식

```json
{
  "screen": "result",
  "elements": [
    {"id":"heading", "type":"text", "text":"결과 / RESULT / 結果",
     "x":20, "y":20, "width":600, "height":50, "fontSize":32},
    {"id":"title", "type":"text", "binding":"song.title", "default":"곡 없음",
     "x":20, "y":80, "width":600, "height":50, "fontSize":32},
    {"id":"score", "type":"number", "binding":"result.score", "default":"--",
     "x":20, "y":140, "width":600, "height":60, "font":"Segoe UI", "fontSize":48,
     "align":"right", "verticalAlign":"center", "clipText":true,
     "numberFormat":{"decimals":0,"minDigits":0,"grouping":true},
     "tint":{"r":255,"g":220,"b":100,"a":255}, "opacity":1}
  ]
}
```

- text는 고정 text 또는 binding을 출력한다. 기본값은 default이며 생략/null이면 text다. 고정 number는 default의 숫자를 서식 적용해 출력한다.
- font는 패밀리명, 기본 Malgun Gothic이다. fontSize는 1~256 정수의 가상 좌표 크기다. resource에 선언 폰트 ID를 지정하면 TTF/OTF를 메모리에서 프로세스 전용으로 등록한다. OS에 설치하거나 디스크에 추출하지 않는다. 폰트 핸들·등록은 해제 시 정리한다. 없는 패밀리는 Windows가 대체할 수 있다.
- 수평 left/center/right, 수직 top/center/bottom을 지원한다. 기본 left/top. clipText 기본 true, 기존 rotation/clip/Tint/opacity도 적용한다.
- 정수는 int64 값을 잃지 않는다. 실수는 fixed decimals(0~9), minDigits(0~32)만큼 정수부 앞에 0을 채운 뒤 grouping이면 쉼표를 넣는다. 부호와 소수부는 최소 자리 수에서 제외한다. 882460 → 882,460, 100000 + minDigits 7 → 0,100,000이다. 소수점은 점, 구분자는 쉼표로 고정한다.
- text에 연결한 int64는 그대로, double은 소수점 두 자리, bool은 true/false다. number는 정수/실수 바인딩만 받는다. 숫자 결측 시 문자열 default도 허용한다.
- JSON 숫자는 기존 JSON DOM의 double을 사용한다. 정수 default는 정확히 표현 가능한 ±(2^53−1)까지 int64로 보관하고 그 밖은 double이다. 실제 스냅샷의 int64는 이 JSON 제한과 무관하다.

## 갱신·성능과 실패 처리

프레임마다 JSON을 읽거나 키 이름을 검색하지 않는다. provider의 UTF-16 문자열 캐시는 원문이 바뀔 때만 UTF-8 사본을 만들며, 숫자/불리언 갱신은 인라인 variant를 사용한다. resolver는 재사용 문자열 버퍼와 to_chars를 사용하고 로캘에 의존하지 않는다.

renderer.update는 표시 문자열이 바뀐 레이어만 GDI 글리프 처리와 GPU 업로드를 한다. 동일 크기 비트맵은 CopyFromMemory로 갱신한다. 값이 달라도 최종 서식 문자열이 같으면 갱신하지 않는다. draw는 준비된 이미지와 글리프 비트맵만 공통 레이어 순서로 제출한다. 변경되는 점수는 새 글리프 픽셀 생성과 업로드 비용이 있으므로 무조건 무할당/무비용이라고 주장하지 않는다.

기존 글리프 캐시는 최대 32개 항목이며 한 항목은 4096×512 이하다. 긴 문자열은 측정 너비 4080px까지 UTF-16 경계에서 접두부를 제한한다. 화면 박스 클리핑은 추가로 적용한다. 줄바꿈·스크롤·말줄임·벡터 글리프·숫자 이미지 폰트는 없다. contain 확대에서는 비트맵 글자가 부드러워질 수 있다.

이미지와 현재 글리프 GPU 픽셀 합계에 기존 예산을 적용한다. CPU 캐시·임시 래스터 표면·폰트 메모리는 별도이므로 이 값이 프로세스 전체 메모리 상한은 아니다. 실제 프레임 시간/장시간 메모리 추이를 측정한 것은 아니다.

prepare 실패는 기존 준비 상태를 보존한다. update 실패는 SkinError로 반환하며 호스트가 기본 UI/재준비를 선택한다. 메모리/장치 오류 중 여러 레이어 갱신 전체를 하나의 트랜잭션으로 보장하지는 않는다. 알 수 없는 바인딩은 오류 반환 대신 기본값과 renderer.diagnostics()에 보관한 진단으로 처리한다. 진단 뷰와 displayText의 string_view는 renderer의 다음 변경/해제 전에만 사용한다.

## 호스트 연결과 다음 애니메이션 인터페이스

```cpp
SkinDataProvider provider; // UI 소유, 화면 호스트 수명 동안 재사용
// 화면 진입 때 renderer.prepare(package, screen, target)를 먼저 완료한다.
if (captureSevenManiaSkinData(provider)) {
    if (auto error = renderer.update(&provider.snapshot())) {
        // 오류 표시 후 기존 UI 폴백을 선택한다.
    }
}
// 그 뒤 BeginDraw / renderer.draw / EndDraw / Present.
// 화면 종료 때 provider.clear(); renderer.unload();
```

캡처 함수는 기본 BmsPlayer 및 전용 통합 테스트에서 BMS_ENABLE_SKIN_BINDINGS로 활성화한다. 기존 게임 UI에 호출/레이아웃을 강제로 넣지 않았다. 설치·선택 UI와 실제 화면 호스트 연결은 별도 단계다.

미래 애니메이션은 같은 BindingId와 snapshot.value(id)의 읽기 전용 값 및 revision을 입력으로 사용할 수 있다. LR2 변환기는 같은 SkinElement의 binding/defaultValue/textStyle/numberFormat을 작성하면 된다. 키프레임 실행기, 이벤트 발생/판정 효과, LR2 변환은 이번에 구현하지 않았다.

## 빌드·테스트

```powershell
cmake --build build-rmlui-song-list --config Release --target BmsPlayer SevenManiaSkinTests SevenManiaSkinRenderTests SevenManiaSkinBindingTests
ctest --test-dir build-rmlui-song-list -C Release --output-on-failure -R '^SevenManiaSkin'
& ./build-rmlui-song-list/Release/SevenManiaSkinBindingTests.exe `
  tests/fixtures/skins/data-binding.semaskin `
  build-rmlui-song-list/Validation/SkinBindingHardware --hardware
```

| 검증 | 결과 |
| --- | --- |
| 기본 Release BmsPlayer.exe | 빌드 성공, 기존 기본 경로 파일 갱신 |
| 로더 | SevenManiaSkinTests 97개 검사 통과 |
| 이미지 회귀 | SevenManiaSkinRenderTests 612회 assertion 통과, WARP와 실제 GPU |
| 바인딩 통합 | SevenManiaSkinBindingTests 353회 assertion 통과, WARP와 실제 GPU |
| RmlUi 전체 OFF | 로더/이미지/바인딩 빌드 및 세 테스트 통과 |
| 실제 데이터 | showTimingJudgement와 기존 점수/게이지/콤보 객체 실행 후 캡처. 점수 100000→150000, 바뀐 판정·콤보·BPM·pause의 문자열/픽셀 반영 |
| 형식/결측 | UTF-8·UTF-16, int64 최솟값, 반올림·쉼표·0 패딩, bool, 숫자가 아닌 난이도, unknown/wrong-screen/wrong-type 진단·기본값 |
| 텍스트 | 한글/영문/일본어, left/top·center/center·right/bottom 정렬, 클리핑, Tint/opacity, 시스템 폰트와 메모리 폰트, 손상 폰트 실패 |
| 갱신/수명 | 변경 없는 캡처/update 100회에 글리프/GPU 업로드 증가 없음, 50회 준비/갱신/그리기/해제, GDI 핸들 증가 제한 확인 |
| 화면 전환/스레드 | 화면 불일치 및 null 스냅샷 기본값 복귀, 곡 삭제 후 결측, worker 캡처 거부 |
| 기존 기능 9종 묶음 | 스킨 3종과 BmsCoreTests/BmsDisplayTests/BmsRmlSongListTests/BmsRmlSongListHostTests/BmsSettingsPhase5Tests의 8종 통과. BmsSongSelectionTests 1종 실패 |

기존 BmsSongSelectionTests의 variant selection should wrap forward 실패는 이전부터 남아 있는 C++/Rust 난이도 전환 wrap→clamp 변경과 기대값 차이다. 이번 작업에서 해당 소스나 기대값은 수정하지 않았다. 전체 테스트가 모두 통과했다고 보고하지 않는다.

개발 결과물은 build-rmlui-song-list/Validation/SkinBinding 및 SkinBindingHardware에 저장한다. select.png, play-before.png, play-after.png, result.png, alignment.png, private-font.png와 binding-diagnostics.txt를 확인할 수 있다. 한글/일본어 실제 PNG도 열어 확인했다. 테스트에 사용한 Windows 폰트 파일은 패키지/저장소에 복사하지 않았다.

모든 실게임 화면의 수동 플레이, ASan, 장시간 프레임/메모리 분석, 실제 장치 제거, 모든 OS/폰트 조합은 검증하지 않았다. 기존 화면 디자인·점수·판정·게이지·입력·오디오·타임라인은 이번 변경 범위에서 유지했다.

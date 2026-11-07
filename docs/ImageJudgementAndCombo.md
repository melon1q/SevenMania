# 이미지 기반 판정 · 콤보

## 구현 및 사용

기존 `play.judgementCombo` 텍스트 요소에 선택적인 `hud` 설정을 추가했다. 기존 파일은 수정 없이 폰트 방식으로 동작한다. 판정과 콤보는 각각 `font` 또는 `image`를 선택한다. 두 값이 모두 font면 기존의 결합 텍스트 렌더링 경로를 그대로 사용한다.

에디터에서 play 화면의 판정+콤보 요소를 선택하고 **판정 이미지 설정**을 연다. 두 모드를 독립적으로 선택하고 왼쪽 목록의 판정/숫자를 선택한 뒤 **선택 이미지 가져오기**로 PNG/JPEG를 가져온다. 이미지 미리보기, 영역 X/Y·너비·높이·불투명도, 판정 및 숫자 크기, 자간, 판정/콤보 간격, 표시 시간을 편집한다. 적용하면 같은 게임 렌더러로 미리보기한다. 저장·다른 이름·내보내기·프로젝트 폴더 저장은 기존 경로를 사용하며 가져온 이미지를 패키지에 포함한다. Undo/Redo도 기존 문서 이력으로 처리한다.

게임 **설정 → 스킨**에는 판정 렌더링/콤보 렌더링이 추가됐다. `스킨 설정`은 패키지의 모드를 사용한다. `폰트`/`이미지`는 해당 부분만 강제 지정한다. `[Skin] JudgementMode`, `ComboMode`에 -1/0/1로 저장하며 다른 설정 키는 보존한다. 이미지를 지정하지 않은 기존 스킨에서 이미지 모드를 선택하면 로딩 중 준비한 폰트 이미지로 대체한다.

기본 스킨 디자인과 사용자가 편집한 `.semaskin`은 다시 생성하거나 덮어쓰지 않았다. 검증용 패키지는 `tests/fixtures/skins/hud-images.semaskin`이며 완성된 디자인 스킨이 아니다.

## JSON 형식

화면 요소 예시:

```json
{
  "id": "judgement-combo", "type": "text", "binding": "play.judgementCombo",
  "x": 660, "y": 520, "width": 600, "height": 100,
  "font": "Michroma", "fontSize": 80, "align": "center", "verticalAlign": "center",
  "hud": {
    "judgementMode": "image", "comboMode": "image",
    "judgements": {
      "pgreat": "judge-pgreat", "great": "judge-great", "good": "judge-good",
      "bad": "judge-bad", "poor": "judge-poor", "mine": "judge-mine"
    },
    "digits": ["digit-0", "digit-1", "digit-2", "digit-3", "digit-4",
               "digit-5", "digit-6", "digit-7", "digit-8", "digit-9"],
    "judgementWidth": 320, "judgementHeight": 80,
    "digitWidth": 48, "digitHeight": 80, "spacing": 0, "gap": 16, "displayMs": 850
  }
}
```

참조값은 기존 리소스와 동일한 **리소스 ID**다. manifest.json의 `resourcePaths`에 ID와 패키지 내부 경로를 대응시킨다. 예를 들어 `judge-pgreat` → `assets/images/judgement/pgreat.png`, `digit-0` → `assets/images/combo/0.png`다. 나머지도 같은 방식으로 선언한다. 경로 대신 리소스 ID를 사용하므로 HUD에 절대 경로나 `../`를 직접 넣을 수 없다. 기존 ZIP 경로·파일 형식·크기 검증을 그대로 거친다.

부분 이미지에 폰트 대체를 허용하려면 manifest 항목을 다음과 같이 optional로 선언할 수 있다:

```json
{"judge-pgreat": {"path": "assets/images/judgement/pgreat.png", "required": false}}
```

위 객체는 `resourcePaths` 안의 항목이다. required=true인 파일 누락은 기존 패키지 규칙대로 명확한 로딩 오류다. `hud.digits`를 선언하면 정확히 10개 ID가 필요하다. 빈 ID/미지정 이미지에는 준비 시 폰트 대체를 적용한다. 모드 기본값은 각각 font다. 수치 범위는 크기/시간 양수, 간격 0 이상이며 최대 10000이다. 영역 자체의 좌표·회전·클리핑·레이어 순서는 기존 요소 속성이다. 합친 폭이 영역을 넘으면 비율을 줄여 맞춘다. align은 판정과 숫자를 합친 그룹을 정렬하고, gap과 spacing으로 숫자 위치·간격을 조절한다. 기어 위치와 게임의 Y 오프셋도 따른다. 이미지 모드에서는 에디터 좌표와 크기를 보존한다.

실제 일반 판정은 PGREAT/GREAT/GOOD/BAD/POOR 5종이다. 기존 지뢰 코드의 MINE 표시는 추가 점수 등급을 만들지 않고 별도 표시 슬롯으로 연결했다. 기존 콤보 값과 MISS/BAD 시 초기화 규칙은 그대로 읽는다. 0은 숫자 0 한 개로 표시하며 선행 0은 추가하지 않는다.

## 데이터 · 렌더링 구조

- `SkinHudStyle`은 `SkinElement`의 선택적 표시 데이터다. 향후 변환기도 같은 내부 구조를 만들 수 있다.
- `play.judgementGrade`는 실제 표시 등급을 전달하는 정수(0..4, 기존 MINE 표시는 5), `play.judgementAge`는 마지막 표시 발생 후 경과 초인 실수다. Play 화면 전용 읽기 전용 스냅샷이다. 화면 전환 시 다른 화면에 값이 남지 않는다.
- 폰트 경로의 PGREAT 표시 문자열은 GREAT이므로 이미지 선택에 문자열을 사용하지 않는다. 실제 판정에서 표시용 등급만 복사하며 점수·게이지·판정 계산을 변경하지 않는다.
- `HudImageAtlas`는 로딩/렌더 타깃 준비 단계에 PNG/JPEG를 디코딩하고 6개 표시와 10개 숫자를 하나의 D2D 비트맵에 배치한다. 동일 이미지 바이트는 기존 디코딩 캐시를 공유한다. 투명 테두리로 필터링 번짐을 줄인다. 준비 중에도 디코딩 버퍼를 glyph마다 복사하지 않는다.
- 이벤트/프레임 갱신에서는 등급과 숫자만 읽는다. 콤보가 바뀔 때만 고정 20자리 버퍼를 갱신한다. 이미지 경로는 문자열 조합·폰트 측정·텍스트 래스터화·텍스처 생성/업로드를 하지 않는다. 전체 이미지 HUD만 쓰면 게임 스냅샷도 결합 판정 문자열을 만들지 않는다.
- font/image 혼합 또는 누락 이미지 대체에 필요한 글리프는 준비 시 미리 래스터화해 같은 아틀라스에 넣는다. 정상적인 전체 이미지 HUD는 해당 폰트 준비 작업도 수행하지 않는다.
- 이미 준비한 D2D 비트맵과 source rectangle으로 출력한다. 판정 1개와 N자리 콤보에 DrawBitmap API 명령은 1+N개다. 같은 아틀라스를 연속 사용하지만 GPU 드라이버의 실제 배치 개수나 비용을 1회라고 가정하지 않는다.
- 기존 D3D11/Direct2D 장치와 전경 패스를 재사용한다. 일시정지 메뉴보다 위에 판정이 겹치지 않도록 기존 전경/메뉴 순서를 유지한다. 화면/타깃 변경 시 장치에 맞게 다시 준비하고 기존 ComPtr 자원은 해제한다.
- 설정 전환은 다음 플레이 프레임에서 준비되지 않은 자원을 접근하지 않도록 CPU 준비를 마친 뒤 교체한다. 이미지 HUD는 재생 시작·재시작·일시정지 해제 전 실제 게임 GPU 타깃으로도 미리 준비한다. GPU 장치/타깃 손실 시에는 첫 draw 전에 다시 준비한다. 장치 재생성/스킨 전환의 준비 비용은 정상 프레임 비용과 구별해야 한다.

## 애니메이션 및 실패 처리

기본 이미지 출력은 정적이다. 기존 텍스트 팝 스케일/무지개 색 갱신을 이미지에 적용하지 않는다. 기존 `extensions.animations` 트랙 평가기를 사용해 x/y/scale/rotation/opacity를 선택적으로 적용한다. `displayMs`는 프레임 수가 아닌 실제 경과 시간으로 표시를 끝낸다. 기존 판정 표시와 동일하게 경과 시간은 steady_clock 기준이며 일시정지 중에도 흐른다.

에디터 이미지 설정 창의 애니메이션 JSON과 **페이드 예제**는 같은 트랙 형식/검증/평가기를 사용한다. judgement 트리거를 미리보기와 게임에서 공통으로 실행한다. 별도의 이미지 애니메이션 엔진이나 픽셀 후처리는 추가하지 않았다.

누락 ID, 미지정 이미지, 픽셀 디코딩 실패는 해당 글리프의 준비된 폰트로 대체하고 진단을 보관한다. 디버거 출력과 스킨 적용 시 설정 INI 폴더의 `skin-render.log`에 원인을 기록한다. 로그는 로딩/설정 단계에서만 처리한다. ZIP/manifest/필수 리소스나 이미지 헤더 자체가 손상돼 기존 검증을 통과하지 못하면 패키지 로딩 오류를 반환한다. 이 경우 기존 게임 정책에 따라 빈 화면/재시도를 사용하며 손상 패키지를 억지로 허용하지 않는다.

## 검증

자동화 검증은 다음을 포함한다.

- 6개 실제 표시 종류, PGREAT 연속 갱신, 고정 이미지가 시간에 따라 깜빡이지 않음
- 0/9/10/99/100/999/1000/18자리 콤보, 그룹 정렬·영역 변경, opacity 트랙, 표시 시간·화면 전환
- 10만 번 갱신의 추가 C++ new 할당 0, 디코딩/업로드/텍스트 래스터화 증가 0, private bytes 증가 검사
- 실제 D2D draw 500회, 준비/해제 20회와 프로세스 메모리 허용 범위 검사
- 동일 이미지 중복 참조의 디코딩 공유, 선택적 파일 누락 패키지와 손상 PNG 픽셀 데이터 대체
- 기존 폰트 모드 및 4가지 font/image 조합
- 에디터 실제 가져오기 함수, 설정 창 컨트롤·이미지 미리보기, 애니메이션 입력, .semaskin 저장/재로딩
- 실제 게임 스냅샷의 PGREAT/GREAT 구별 및 이미지 HUD GPU 전경 출력, 게임 플레이 중 CPU readback 없음
- [스킨] 탭 RmlUi/Win32 모드 선택·저장·복원 및 기존 위치 슬라이더와 겹치지 않음
- 기존 로더/이미지·레이어/바인딩/설정/재시작/에디터 회귀 테스트

빌드: 기본 Release `BmsPlayer.exe`, `SevenMania Skin Editor.exe` 및 관련 테스트를 업데이트했다. CTest 10개와 별도의 하드웨어 GPU HUD 테스트가 통과했다. 결과와 합성 측정 수치는 빌드 폴더 `Validation/SkinHudImages/performance.txt`, `Validation/SkinHudImagesHardware/performance.txt`에 저장한다. 이 파일은 실행/환경에 따라 달라지는 테스트 출력으로 Git에 넣지 않는다.

합성 비교는 동일한 256개 콤보 변경에 대해 고정 무지개 PGREAT 폰트 경로와 이미지 경로의 update CPU 시간을 측정한다. 이미지 경로의 텍스처 업로드는 0회이며 기존 폰트는 변경된 문자열을 래스터화/업로드한다. 이는 전체 게임 FPS, Present 대기, 실제 오디오/입력 지연 또는 전체 프레임 타임 비교가 아니다. 성능 비율이나 1100/1200FPS 유지 보장을 주장하지 않는다.

이번 하드웨어 GPU 테스트 실행의 합성 측정 예시(동일 256개 값 변경, update만 측정):

| 항목 | 폰트 PGREAT | 이미지 HUD |
|---|---:|---:|
| update 누적 CPU 시간 | 535.287 ms | 0.1453 ms |
| 변경 중 텍스트 업로드 | 256회 | 0회 |

10만 번 스냅샷/이미지 갱신에서는 추가 C++ new 할당 0회, warm-up 전후 private bytes 70,512,640 → 70,512,640을 기록했다. 단일 실행의 합성 수치이며 드라이버·캐시·해상도와 입력 조건에 따라 달라진다. 전체 프레임 시간이나 게임 FPS 수치로 변환하지 않는다.

실제 곡을 수 시간 연속 플레이하는 검증, 모든 GPU/드라이버 조합, 실제 최저 1% 프레임 타임 비교는 수행하지 않았다. 반복/메모리 테스트는 장시간 실사용 검증을 대체하지 않는다. 에디터 이미지 가져오기 대화상자에서 실제 사용자가 파일을 클릭하는 수동 검증도 별도다. 가져오기 함수와 창의 설정·미리보기·저장 경로는 자동화로 확인한다.

## 변경 파일

- 모델/검증: `src/skin/SkinPackage.h`, `SkinLoader.cpp`, `SkinData.h/.cpp`
- 렌더러: `src/skin/SkinHudImages.h`, `SkinRenderer.h/.cpp`, `SkinApplication.h/.cpp`
- 엔진 연결·설정: `src/Main.cpp`, `SkinApplicationHost.inl`, `SettingsActions.inl`, `SettingsController.h`, `SettingsRmlBindings.inl`, `SettingsRmlPage.inl`, `Assets/UI/settings.rml`
- 에디터: `src/editor/HudImageEditor.h`, `EditorDocument.h`, `SkinEditor.cpp`
- 검증: `tests/SkinHudImageTests.cpp`, `SkinBindingTests.cpp`, `SkinApplicationTests.cpp`, `tests/fixtures/skins/hud-images.semaskin`, `tools/CreateHudImageTestSkin.py`, `CMakeLists.txt`

새 런타임 라이브러리는 추가하지 않았다. 이미 사용하는 WIC, Direct2D, D3D11, ZIP 검증 및 텍스트 캐시를 재사용한다. 테스트 패키지 생성 도구만 기존 개발 환경의 Pillow를 사용한다.

추가 관찰: 반복 실행 중 기존 에디터 폴더 내보내기 smoke에서 한 차례 디렉터리 rename의 Windows `Access is denied`가 발생했고, 같은 테스트를 재실행해 통과했다. 접근 거부의 외부 원인은 확정하지 못했다. 실패 시 원본을 덮어쓰지 않고 pending 폴더를 보존하는 기존 오류 처리를 유지했다.

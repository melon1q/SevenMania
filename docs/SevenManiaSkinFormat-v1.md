# SevenMania `.semaskin` v1 규격

상태: 3차 읽기 전용 데이터 바인딩·텍스트·숫자까지 반영한 v1 규격. 실제 화면 디자인·설치 UI 규격은 아니다.

## 컨테이너

확장자는 `.semaskin`이며 ZIP32의 Stored(0), Deflate(8)를 지원한다. 파일명은 UTF-8이다. ASCII 밖 문자를 쓰는 ZIP 엔트리에는 UTF-8 플래그가 필요하다. 외부 확장자 대소문자는 허용하지만 내부 경로와 JSON 필드명은 대소문자를 구분한다.

```text
Example.semaskin
├─ manifest.json
├─ screens/
│  ├─ select.json     # 선언한 화면만 필수
│  ├─ play.json
│  └─ result.json
├─ assets/images/    # PNG, JPG, JPEG
├─ assets/sprites/   # PNG, JPG, JPEG 스프라이트 시트
├─ assets/fonts/     # TTF, OTF 메타데이터
└─ animations/       # JSON 데이터 보관만 지원
```

`manifest.json`은 ZIP 루트에 위치한다. ZIP 안에 상위 `Example/` 폴더를 한 겹 더 만들면 안 된다. 디렉터리 엔트리는 생략 가능하다. 지정 폴더 외 파일, ZIP64, 암호화, 분할 ZIP, SFX/접두 데이터, 로컬 엔트리 사이 틈/중첩, 대체 Unicode 경로 extra 필드는 지원하지 않는다. 일반적인 ZIP 도구의 Stored/Deflate 패키지와 데이터 디스크립터는 지원한다.

모든 파일은 메모리에서 읽으며 디스크에 압축 해제하지 않는다. 절대 경로, 역슬래시, `.`/`..` 컴포넌트, ADS/드라이브/URL, Windows 예약 장치명, 끝 공백/마침표, 제어 문자, `%` 경로를 거부한다. 심볼릭 링크·재분석 지점과 대소문자 충돌/중복 경로도 거부한다.

## manifest.json

아래 필드 중 `resolution`만 생략 가능하며, 생략하면 1920×1080이다. 나머지 9개 필드는 필수다. `description`은 빈 문자열이어도 된다. 1차 패키지는 그대로 호환된다.

```json
{
  "format": "SevenMania.Skin",
  "formatVersion": 1,
  "id": "example.result-only",
  "name": "Result loader example",
  "author": "Example author",
  "skinVersion": "1.0.0",
  "description": "로더 확인용 부분 스킨",
  "resolution": { "width": 1920, "height": 1080 },
  "supportedScreens": ["result"],
  "resourcePaths": {
    "pixel": "assets/images/pixel.png",
    "optional-font": { "path": "assets/fonts/font.ttf", "required": false }
  }
}
```

| 필드 | 의미 |
| --- | --- |
| format / formatVersion | 포맷 식별자와 호환성 버전. 정확히 `SevenMania.Skin` / 정수 `1`만 지원 |
| id | 등록용 고유 ID. 1~96자 소문자 ASCII·숫자·`.`·`_`·`-`, 첫 문자는 `.` 불가 |
| name / author | 비어 있지 않은 표시 문자열, 각각 최대 256 UTF-16 코드 유닛 |
| skinVersion | 스킨 자체 SemVer 문자열. 최대 64자. `major.minor.patch[-prerelease][+build]` |
| description | 설명, 최대 4096 UTF-16 코드 유닛 |
| resolution | 생략 시 1920×1080. 디자인 좌표계의 양수 정수 width/height, 각각 최대 16384. 실행 창 크기를 변경하지 않음 |
| supportedScreens | 중복 없는 `select`, `play`, `result` 중 1~3개 |
| resourcePaths | 자원 ID → 경로 문자열 또는 `{path, required}`. 문자열/required 생략은 필수 자원 |

선언한 화면 파일이 없으면 오류다. 선언하지 않은 화면 파일이 ZIP에 들어 있어도 오류다. 반면 선언하지 않은 화면 자체가 없는 것은 정상적인 부분 스킨이다. 화면별 선택 결과가 없으면 호출자가 기존 기본 화면을 유지하도록 설계했다.

자원 ID도 스킨 ID와 동일한 문자 규칙을 쓴다. 선택 자원(`required:false`)은 없어도 되지만, 요소가 그 ID를 참조하면 반드시 존재해야 한다. 여러 요소가 같은 자원 ID를 참조할 수 있고, 서로 다른 ID가 동일 파일을 가리켜도 바이트 버퍼를 공유한다.

미지원 포맷 버전을 최신 버전처럼 해석하지 않는다. `skinVersion`은 제작물 버전이며 `formatVersion` 호환성 판단과 별개다.

## 화면 JSON

`screens/result.json`의 최소 이미지 예:

```json
{
  "screen": "result",
  "elements": [
    {
      "id": "pixel",
      "type": "image",
      "resource": "pixel",
      "x": 0, "y": 0, "width": 1, "height": 1,
      "z": 0, "opacity": 1
    }
  ]
}
```

루트의 `screen`, `elements`는 필수다. `screen`은 파일명과 일치해야 한다. `resolution`을 생략하면 manifest 좌표계를 상속한다. `extensions`는 선택적 객체다. 빈 요소 배열도 허용한다.

| 요소 필드 | 규칙 |
| --- | --- |
| id / type | 필수. 화면 안에서 ID 중복 불가. type은 `image`, `text`, `number`, `sprite` |
| x / y / width / height | 필수 유한 수. 위치는 ±1,000,000, 크기는 0 초과~1,000,000 |
| resource | image/sprite는 필수 이미지 ID. text/number에 지정하면 폰트 ID여야 함 |
| z | 선택 정수, ±1,000,000. 낮은 값부터 정렬하며 같은 값은 원래 순서 유지 |
| opacity | 선택 0~1, 기본 1 |
| rotation | 선택 유한 수, ±360,000도. 기본 0. 목적 사각형 중심 기준 시계 방향 |
| tint | 선택 `{r,g,b,a}`. RGB는 필수 0~255 정수, a 생략 시 255. 전체 생략 시 흰색/255 |
| visible | 선택 boolean, 기본 true |
| source | 선택 `{x,y,width,height}`. 이미지 픽셀 좌표, 양수 크기, 이미지 범위 안. image/sprite만 사용 |
| clip | 선택 `{x,y,width,height}`. 화면 가상 좌표계의 축 정렬 영역. 요소 회전의 영향을 받지 않음 |
| text | 선택 문자열, 최대 4096 UTF-16 코드 유닛 |
| binding | 선택 이름, 최대 128 UTF-16 코드 유닛. 3차 레지스트리에서 ID/자료형/화면 범위를 확인. 미등록 이름은 진단과 기본값으로 처리 |
| default | 값이 없을 때 표시할 문자열·정수·실수·불리언 또는 null. number에는 boolean 불가. 생략/null이면 text를 기본 표시로 사용 |
| font / fontSize | 기본 Malgun Gothic / 32. font는 최대 31 UTF-16 코드 유닛의 글꼴 패밀리, fontSize는 가상 좌표 기준 1~256 정수 |
| align / verticalAlign | left/center/right, top/center/bottom. 기본 left/top |
| clipText | boolean, 기본 true. 텍스트를 요소 사각형으로 자름 |
| numberFormat | 선택 객체. decimals 0~9, minDigits 0~32, grouping boolean. 기본 0/0/false |
| sprite | sprite에 필수. 양수 정수 `frameWidth`, `frameHeight`, `columns`, `rows` |
| extensions | 선택 객체. 추후 키프레임·이벤트 등 선언 데이터 보관용이며 실행하지 않음 |

스프라이트 격자는 이미지 범위 안에 있어야 하고 총 프레임 수는 4096 이하다. 다른 타입에 격자를 넣으면 거부한다. 알려지지 않은 정규 필드는 오타/버전 혼동 방지를 위해 거부하므로 확장 데이터는 `extensions` 안에 둔다.

## 리소스와 제한

PNG/JPEG는 고정 포맷 WIC 디코더로 컨테이너·프레임·크기를 확인한다. 과대 크기 헤더는 디코더 생성 전 검사한다. 로더 단독 호출은 픽셀 스트림 끝까지 검사하지 않는다. 2차 `SkinRenderer::prepare()`가 참조되는 표시 이미지의 픽셀을 PBGRA로 디코딩하고 PNG 청크 CRC/경계를 추가 검사한 뒤 GPU 비트맵을 준비한다. 디코딩/업로드 실패 시 이전 렌더 상태를 유지한다. WIC 코덱이 허용하는 모든 손상 패턴을 찾아내는 별도 이미지 포맷 검증기는 아니다.

로더의 TTF/OTF 검사는 sfnt 시그니처와 테이블 범위까지다. 3차 렌더러는 text/number의 resource가 가리키는 폰트를 AddFontMemResourceEx로 프로세스 안에 일시 등록하고 해제한다. OS 영구 설치나 파일 추출은 하지 않는다. 실제 글리프 처리는 Windows GDI에 맡기며 등록 실패 시 이전 준비 상태를 유지한다. DDS, SVG, 영상, 실행 파일과 스크립트는 거부한다. ZIP 안의 미참조 이미지/폰트/애니메이션 JSON도 검사하지만 최종 모델에는 선언한 자원만 보관한다.

| 기본 제한 | 값 |
| --- | --- |
| ZIP 파일 크기 / 엔트리 수 | 64 MiB / 1024개 |
| 엔트리별 / 전체 압축 해제 크기 | 16 MiB / 128 MiB |
| 압축 해제 크기 ÷ 압축 크기 | 최대 200 |
| JSON 파일별 크기 / 깊이 / 구문 노드 예산 | 1 MiB / 64 / 100,000 |
| 화면별 요소 수 | 4096 |
| 이미지 한 변 / 픽셀 수 | 8192 / 16,777,216 |
| 선언 이미지의 RGBA 예상 합계 | 256 MiB |
| 내부 경로 길이 | UTF-8 240 bytes |

JSON은 UTF-8(선택적 BOM)만 지원한다. 중복 키, 잘못된 유니코드, 비유한 숫자, 과도한 중첩을 거부한다. 공통 모델로 직접 넣은 확장 DOM도 깊이·노드·텍스트 예산과 비유한 숫자를 검사한다. CRC와 실제 inflate 결과 크기도 일치해야 한다.

## 사용과 미래 LR2 변환 경로

공개 헤더는 `src/skin/SkinPackage.h`, 네임스페이스는 `sevenmania::skin`이다. `SkinLoader::load()`는 결과 또는 `SkinError{code,path,message}`를 반환한다.

```cpp
sevenmania::skin::SkinManager manager;
if (auto error = manager.registerFile(L"Example.semaskin")) {
    // error->code, path, message를 표시. 기존 선택은 유지된다.
    return;
}
if (auto error = manager.select(sevenmania::skin::ScreenKind::Result,
                                "example.result-only")) return;
auto owner = manager.selected(sevenmania::skin::ScreenKind::Result);
// owner가 살아 있는 동안 내부 화면과 자원도 유효하다.
```

공통 모델은 ZIP 헤더·파일 핸들·GPU 객체·LR2 문법을 포함하지 않는다. LR2 변환기는 `SkinPackage`, `SkinResource`, `SkinScreen`, `SkinElement`를 직접 만들고 `registerModel()`에 전달하면 된다. 외부 원본 경로는 안전한 가상 상대 경로와 자원 ID로 치환하고 바이트를 제공해야 한다. `SkinLoader::prepare()`가 이 공통 검증 경로이며, 가변 바이트의 불변 사본을 만들어 외부 별칭으로 인한 변경도 차단한다.

Manager는 단일 소유 스레드에서 사용한다. 로딩/등록은 화면 진입 전에 수행하고 프레임 경로에서는 불변 결과를 읽는다. 해제는 shared_ptr 수명으로 처리한다. 선택된 스킨 등록을 해제하면 그 화면 선택은 비워지며, 이미 반환한 owner는 호출자가 놓을 때까지 유효하다.

## 2차 이미지 렌더링

별도 `sevenmania_skin_render` 라이브러리의 `SkinRenderer`가 image 요소와 sprite의 첫 프레임을 그린다. text/number는 3차에서 기존 GDI TextRasterCache를 사용해 출력한다. 프레임 변경, 이벤트 바인딩은 실행하지 않는다. `source`를 명시하면 sprite 첫 프레임보다 우선한다.

가상 해상도는 manifest 값을 상속하며, 1차 규격의 화면별 명시적 resolution 재정의도 유지한다. 비율을 유지하는 contain 모드로 중앙 배치하고 가상 화면 밖을 자른다. 실행 창이나 모니터 해상도는 바꾸지 않는다. 전체 이미지 보간은 linear, 명시한 source와 sprite는 인접 프레임의 색 번짐 방지를 위해 nearest-neighbor다. 별도 필터 선택 옵션은 아직 없다.

동일 준비 작업 안에서 같은 이미지 바이트는 한 번 디코딩한다. 동일 이미지/RGB Tint는 같은 비트맵을 공유한다. Tint alpha와 opacity는 draw 시 곱하며 추가 비트맵을 만들지 않는다. RGB Tint는 준비 시 픽셀에 적용하므로 다른 RGB Tint는 추가 GPU 비트맵을 사용한다. 디코딩 합계와 Tint 변형 GPU 픽셀 합계에 각각 기본 256 MiB 예산을 적용한다. 예산은 드라이버의 실제 메모리 할당량을 뜻하지 않는다.

이미지 속성 예:

```json
{
  "id": "overlay", "type": "image", "resource": "pixel",
  "x": 200, "y": 100, "width": 256, "height": 128,
  "z": 3, "opacity": 0.75, "rotation": 15,
  "tint": {"r": 255, "g": 128, "b": 64, "a": 200},
  "visible": true,
  "source": {"x": 0, "y": 0, "width": 1, "height": 1},
  "clip": {"x": 0, "y": 0, "width": 1920, "height": 1080}
}
```

준비/그리기/해제와 호스트 연결 계약은 `SevenManiaSkin-Phase2-Implementation.md`를 참고한다.

## 3차 바인딩·텍스트·숫자

현재 키, 실제 엔진 값의 의미와 자료형, 스냅샷 수명은 `SevenManiaSkin-Phase3-Implementation.md`의 표를 기준으로 한다. 바인딩 이름은 대소문자를 구분한다. 알려지지 않은 이름/잘못된 화면/number에 문자열 바인딩을 연결한 경우는 패키지 전체를 거부하지 않고 진단과 기본값으로 처리한다.

```json
{
  "screen": "result",
  "elements": [
    {"id":"heading", "type":"text", "text":"결과 / RESULT / 結果",
     "x":20, "y":20, "width":600, "height":50, "fontSize":32},
    {"id":"score", "type":"number", "binding":"result.score", "default":"--",
     "x":20, "y":80, "width":600, "height":60, "font":"Segoe UI", "fontSize":48,
     "align":"right", "verticalAlign":"center", "clipText":true,
     "numberFormat":{"decimals":0,"grouping":true,"minDigits":7},
     "tint":{"r":255,"g":220,"b":100,"a":255}, "opacity":1}
  ]
}
```

폰트 resource를 생략하면 시스템 폰트 패밀리를 사용한다. resource를 지정하면 manifest에 선언한 TTF/OTF ID여야 하며 font에는 그 파일의 내부 패밀리명을 지정한다. 폰트 자체를 찾지 못하면 Windows의 글꼴 대체가 적용될 수 있다. 고정 문자열은 text, 동적 값은 binding을 사용한다. 기본값은 키 미등록·화면 불일치·값 미제공에 적용되며, 제공된 빈 문자열이나 0은 정상 값이다.

number는 정수 64비트 값을 정확히 표시하고, 실수는 지정한 소수점 자리로 반올림한다. minDigits는 부호·소수점 이하를 제외한 정수부의 최소 자리 수이며 앞에 0을 채운 뒤 세 자리마다 쉼표를 넣는다. 예: 100000 + minDigits 7 + grouping → 0,100,000. 고정 number는 default에 숫자를 넣어 표시한다. text로 숫자를 연결하면 정수는 그대로, 실수는 소수점 두 자리, 불리언은 true/false로 표시한다. 사용자 정의 printf 서식이나 코드는 받지 않는다.

텍스트는 단일 행 UTF-8이며 내부에서 UTF-16으로 변환한다. 위치·영역·정렬·색상·alpha·rotation·clip은 준비된 글리프 비트맵에 적용한다. 요소 크기에 맞춰 글자 크기를 자동 변경하지 않는다. 기존 글리프 캐시의 한계에 맞춰 래스터 너비는 최대 4096px(실제 문자열 측정 4080px + 여백), 높이는 최대 512px이다. 더 긴 문자열은 안전한 UTF-16 경계에서 접두부를 제한한다. 자동 줄바꿈·말줄임·스크롤·숫자 이미지 폰트는 아직 없다.

`SkinRenderer::update(snapshot)`는 BeginDraw 전에 호출한다. 변경되지 않은 표시 문자열에는 글리프 처리/GPU 업로드를 하지 않는다. `draw()`에서는 준비된 이미지와 텍스트를 공통 z 순서로 출력한다. 잘못된 바인딩 정보는 renderer.diagnostics()에서 확인하며, 화면 전환 시 이전 화면 스냅샷을 넘기면 동적 값은 기본값으로 돌아간다.


## 내장 글꼴과 폰트 라이선스

내장 글꼴은 `Michroma`, `Oxanium`, `Oxanium Bold`입니다. Michroma는 공식 Regular만 포함하며, Oxanium은 Regular와 실제 Bold 파일을 포함합니다. 시스템 설치 없이 프로세스 안에서 등록합니다. 기존 BankGothic 계열 이름은 Michroma로 해석하고 기존 폰트 바이트를 GDI에 등록하지 않습니다. 에디터에서 열고 다시 저장하면 이전 폰트 자원도 제거됩니다.

폰트를 포함한 스킨은 해당 라이선스 전문을 `licenses/*.txt`에 동봉할 수 있습니다. 이 경로의 텍스트는 크기 제한 64KiB, UTF-8 검증을 거치며 실행하거나 렌더링하지 않습니다. 기본 스킨의 Michroma OFL 문서도 이 경로에 들어갑니다. 프로그램 배포용 OFL 전문은 `Assets/Fonts/Licenses/`에 있습니다.

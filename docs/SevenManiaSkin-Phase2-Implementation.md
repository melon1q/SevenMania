# SevenMania 스킨 2차 구현: 이미지 렌더링

작성일: 2026-10-28

## 조사 결과와 구현 경계

- 1차 `SkinPackage.h`에는 컨테이너 독립 모델, 자원 바이트 공유, 화면별 요소, sprite 메타데이터가 있다. `SkinLoader::prepare()`가 변환기 모델도 검증하고 요소를 안정 정렬한다. 이 경로를 그대로 사용했다.
- `CompositionPresenter.h`의 플레이 GPU 출력은 D3D11 DXGI 표면에 만든 Direct2D render target과 premultiplied BGRA bitmap을 사용한다. 기존 `sprite()`는 픽셀 업로드와 캐시를 갖지만 회전·Tint·원본 영역을 받지 않고, 프레젠터의 화면/수명과 결합되어 있다.
- 곡 선택은 `Main.cpp`의 GDI/GDI+ 화면과 `RmlSongListRenderer`의 D3D11 RmlUi/읽기 복사 경로가 혼합되어 있다. 플레이는 CompositionPresenter/GDI 경로, 리절트는 기존 Main.cpp 경로를 유지한다. 새 렌더러 호출은 이들 화면에 넣지 않았다.
- 기존 WIC 이미지 메타데이터 검사와 FreeType에 포함된 zlib CRC, Windows Direct2D/D3D11을 재사용한다. 새 외부 라이브러리·영상 디코더는 추가하지 않았다.
- 공통 연결점은 `ID2D1RenderTarget*`이다. 현재 프레젠터와 같은 DirectX 출력 방식을 사용하지만 프레젠터 내부 캐시/입력/Present 정책에는 의존하지 않는다. 기존 렌더링 백엔드를 교체하지 않는다.

## 추가 파일

| 파일 | 역할 |
| --- | --- |
| `src/skin/SkinRenderer.h` | `SkinRenderer`, `SkinRenderStats` 공개 인터페이스 |
| `src/skin/SkinRenderer.cpp` | WIC 전체 픽셀 디코딩, PNG 청크 검사, Direct2D 비트맵 준비·공유·그리기·해제 |
| `tests/SkinRenderTests.cpp` | D3D11 WARP/실제 GPU의 오프스크린 출력, 픽셀 검증, 개발용 PNG 미리보기 |
| `tests/fixtures/skins/render-layers.semaskin` | 단순 색상·투명도·회전·자르기 검증용 부분 스킨 |
| `tests/fixtures/skins/generate-render-fixture.py` | 동일 샘플 재생성 도구. Python 표준 라이브러리만 사용하며 게임 실행 의존성이 아님 |
| `docs/SevenManiaSkin-Phase2-Implementation.md` | 이 보고서 |

## 기존 파일 변경

| 파일 | 변경 |
| --- | --- |
| `src/skin/SkinPackage.h` | `SkinRect`, `SkinTint`, rotation/tint/visible/source/clip. manifest 기본 해상도 1920×1080 |
| `src/skin/SkinLoader.cpp` | 새 JSON 속성 파싱 및 공통 모델 검증. source 이미지 범위, 유한 수·타입·Tint 범위 검사 |
| `tests/SkinTests.cpp` | 잘못된 렌더 속성 10종과 기본 해상도 상속 검사 추가 |
| `cmake/SkinSystem.cmake` | 독립 `sevenmania_skin_render` 라이브러리 및 자동화 테스트 추가 |
| `CMakeLists.txt` | 기본 BmsPlayer의 스킨 의존성을 렌더 라이브러리까지 연결 |
| `docs/SevenManiaSkinFormat-v1.md` | 2차 속성, 기본값, 렌더링 규칙 반영 |

기존 게임·곡 선택·리절트·설정 소스, BMS 파서·판정·오디오·입력은 이번 작업에서 수정하지 않았다. 작업 전에 존재했던 다른 변경과 staged 변경도 보존했다.

## 데이터와 렌더링 규칙

- `resource`는 manifest의 자원 ID이며 실제 이미지 경로는 resourcePaths에서 해결한다. PNG와 기존 JPEG 자원을 지원한다.
- x/y/width/height는 가상 좌표, source는 원본 이미지 픽셀이다. source를 이미지 밖으로 지정하면 로딩이 실패한다.
- opacity 기본값은 1, rotation은 0도, Tint는 RGBA 255, visible은 true다.
- 회전은 목적 사각형 중심 기준 시계 방향이다. Tint RGB와 원본 RGB를 곱하고, Tint alpha와 opacity는 DrawBitmap의 alpha 배율로 곱한다. 원본 이미지 alpha도 유지한다.
- 낮은 z부터 출력하며 같은 z에서는 JSON 선언 순서를 유지한다. 불투명도 때문에 순서를 재배치하지 않는다.
- clip은 요소 회전의 영향을 받지 않는 화면 가상 좌표 영역이다. 전체 가상 화면 경계도 클리핑한다.
- manifest resolution 기본값은 1920×1080이다. 지정하면 그 값을 상속하고, 1차 규격의 화면별 명시적 resolution 재정의도 유지한다. contain으로 비율을 유지해 중앙 배치한다. 창 크기/디스플레이 모드는 바꾸지 않는다.
- 전체 이미지는 linear 보간, source 지정 이미지와 sprite는 인접 아틀라스 영역 색 번짐을 막는 nearest-neighbor를 사용한다.
- sprite는 첫 프레임만 표시한다. source가 있으면 그 영역이 우선한다. 애니메이션 실행은 없다. text/number는 모델을 보존하지만 출력에서 건너뛴다.

## 자원 수명과 성능

`prepare()`가 공통 모델을 재검증하고, 새 상태를 지역 객체로 완성한 뒤 성공했을 때만 교체한다. 누락·디코딩·GPU 업로드·예산 초과로 실패하면 기존 준비 상태를 유지한다. COM 자원과 비트맵은 ComPtr, 준비 상태는 unique_ptr로 소유한다. `unload()`와 소멸자가 비트맵과 render target 참조를 해제한다.

준비 작업 안에서 동일 바이트의 이미지는 자원 ID/경로가 달라도 한 번 디코딩한다. 동일 이미지와 RGB Tint 조합은 비트맵을 공유한다. opacity, 회전, source, clip, Tint alpha가 달라도 비트맵을 추가하지 않는다. 샘플은 표시 이미지 10개를 3회 디코딩, 7개 비트맵으로 출력한다.

RGB Tint는 준비 단계에 적용하므로 같은 이미지라도 RGB Tint가 다르면 별도 비트맵이다. 이는 기존 Direct2D bitmap 경로를 유지하기 위한 선택이다. 디코딩 합계와 Tint 변형 비트맵 합계에 각각 기본 256 MiB 제한을 적용한다. 준비 중 인코딩 원본·디코딩 버퍼·Tint 임시 버퍼·이전 GPU 상태가 함께 존재할 수 있으므로 전체 프로세스 메모리를 256 MiB로 제한한다는 의미는 아니다. 준비가 끝나면 디코딩 버퍼는 버린다.

`draw()`는 렌더러 자체의 파일 I/O, JSON 파싱, 이미지 디코딩, 텍스처 생성, 컨테이너 할당을 하지 않는다. 준비된 레이어를 순회하며 DrawBitmap을 한 번씩 호출한다. D2D 내부의 명령 제출을 이용하며 별도의 범용 배칭 엔진을 추가하지 않았다. 실제 GPU 드로우콜 수나 프레임 시간 향상을 측정한 것은 아니다. 화면 전환 때 준비해야 하며 인게임 프레임에서 prepare를 호출하지 않아야 한다.

캐시는 준비한 화면 단위이며, 스킨/장치가 바뀌면 다시 준비한다. 전역 영구 캐시는 없다. 준비한 renderer는 render target을 강하게 참조하므로 호스트가 target을 교체/해제하기 전에 unload해야 한다.

## 다음 단계에서 연결할 호스트 인터페이스

```cpp
using namespace sevenmania::skin;
SkinRenderer renderer;
auto package = manager.selected(ScreenKind::Result);
if (package) {
    if (auto error = renderer.prepare(package, ScreenKind::Result, target)) {
        // 오류 표시. 이전 준비 상태 또는 기존 기본 화면을 유지한다.
    }
}

// target의 소유 스레드, 화면 진입 때 준비 완료한 뒤 프레임마다:
target->BeginDraw();
const auto size = target->GetSize(); // DIP; 96 DPI에서는 픽셀과 동일
if (!renderer.draw(size.width, size.height)) {
    // 아직 준비되지 않았다면 기존 기본 화면을 그린다.
}
HRESULT status = target->EndDraw();
// D2DERR_RECREATE_TARGET/장치 손실은 호스트가 처리한다.
// renderer.unload(), target 재생성, 화면 진입 경로에서 prepare 재호출.
```

호스트가 BeginDraw/EndDraw/Present, 배경 지우기, DPI, 출력 창을 소유한다. renderer는 호출 전 transform을 복원하고 자체 push clip을 pop한다. 기존 호스트 clip 안에 출력된다. width/height는 양수 유한 DIP 값이고 target 크기를 넘지 않아야 한다. 단일 소유 스레드에서 사용한다. draw의 true는 명령 제출을 뜻하며 실제 GPU 성공 판정은 EndDraw 결과로 한다.

앞으로 CompositionPresenter의 그리기 구간 또는 별도 화면 호스트에서 같은 target을 전달하면 된다. 곡 선택의 GDI/RmlUi와 합성할 때에는 별도의 호스트 연결 설계가 필요하다. 이번에는 실제 화면 훅을 추가하지 않았다.

LR2 변환기는 동일 SkinPackage/SkinElement와 resource 바이트를 만들고 SkinLoader::prepare 또는 SkinManager::registerModel로 검증한 결과를 이 렌더러에 전달할 수 있다. ZIP이나 LR2 문법을 renderer에 넣지 않는다.

## 개발용 검증 경로

```powershell
cmake --build build-rmlui-song-list --config Release --target SevenManiaSkinRenderTests
ctest --test-dir build-rmlui-song-list -C Release --output-on-failure -R '^SevenManiaSkin'

# 실제 GPU로 같은 픽셀 검증 실행
& ./build-rmlui-song-list/Release/SevenManiaSkinRenderTests.exe `
  tests/fixtures/skins/render-layers.semaskin `
  build-rmlui-song-list/Validation/SkinRenderHardware --hardware

# 임의의 유효한 패키지에서 선언한 각 화면을 1920×1080 PNG로 미리보기
& ./build-rmlui-song-list/Release/SevenManiaSkinRenderTests.exe `
  tests/fixtures/skins/render-layers.semaskin `
  build-rmlui-song-list/Validation/SkinPreview --preview --hardware
```

--hardware를 생략하면 D3D11 WARP를 사용한다. 미리보기는 별도 창 대신 DirectX 오프스크린 표면을 PNG로 저장하는 개발용 경로다. 실게임 UI에는 버튼이나 메뉴를 추가하지 않는다. 샘플은 검증용 도형이며 디자인 스킨이 아니다.

## 검증 결과

| 항목 | 결과 |
| --- | --- |
| 기본 Release BmsPlayer 빌드 | 성공. build-rmlui-song-list/Release/BmsPlayer.exe 갱신 |
| SevenManiaSkinTests | 87개 검사 통과. 기존 100회 로드/해제와 변형 ZIP 검사 포함 |
| SevenManiaSkinRenderTests / WARP | 612회 assertion 통과, 100회 준비/출력/해제 |
| 같은 테스트 / 실제 GPU | 612회 assertion 통과 |
| RmlUi 전체 OFF 별도 빌드 | 로더/렌더러 빌드 및 두 테스트 통과 |
| 해상도 | 320×180, 640×480, 1920×1080, 2560×1440, 800×180에서 픽셀 검증 |
| 이미지 출력 | PNG/JPEG 준비·출력, 위치/크기, z 정렬·동률 선언 순서, opacity, PNG alpha, Tint RGBA, 90도 회전, source/clip/visible 검사 |
| 실패/수명 | 누락 이미지, 손상 PNG, null/미지원 화면, 잘못된 속성, 예산 초과, 이전 상태 보존, target 재준비, 반복 해제 검사 |
| 개발 미리보기 | --preview --hardware 성공, result.png 생성 |
| 기존 핵심/곡 목록/목록 호스트/설정 Phase5 | BmsCoreTests, BmsRmlSongListTests, BmsRmlSongListHostTests, BmsSettingsPhase5Tests 통과 |
| BmsDisplayTests | 묶음 실행에서 section start arrow control failed 1회. 단독 재실행 통과. 간헐 실패 원인 미확정 |
| BmsSongSelectionTests | 기존 variant selection should wrap forward 실패가 계속 남음 |

곡 선택 테스트는 작업 전부터 존재한 C++/Rust의 wrap→clamp 변경과 기대값이 다르다. 이 테스트 타깃은 스킨 라이브러리를 링크하지 않는다. 해당 변경을 되돌리거나 관련 없는 동작을 수정하지 않았다. 전체 자동화가 모두 통과했다고 보고하지 않는다.

640×480 PNG를 열어 contain 여백, 색상 겹침, 회전, 자르기 결과도 확인했다. 실게임의 모든 화면/모든 기능 수동 점검, 장시간 플레이·메모리 추이 측정, 실제 GPU 장치 제거, ASan 검증은 수행하지 않았다. 반복 수명 테스트 통과를 메모리 누수의 완전한 증명으로 해석하지 않는다.

## 미구현 및 다음 단계

기존 곡 선택·플레이·리절트 디자인 교체, Core/일반 디자인, LR2 변환기, 에디터, 영상, 키프레임, 판정 이벤트는 구현하지 않았다. 다음 단계는 호스트의 화면 진입/종료와 장치 재생성에 prepare/draw/unload를 연결하고 기존 화면 폴백을 유지하는 것이다. 이후 텍스트/숫자 렌더링과 데이터 바인딩을 같은 공통 모델 위에 추가할 수 있다.

# 곡 플레이 병목 측정

이 보고서 이후 해결책을 CPU/GPU 하이브리드 실험에 적용했다.
변경 내용과 새 측정은 [Hybrid-Renderer.md](Hybrid-Renderer.md)를 참고한다. 아래는 변경 전 결과다.

## 결론

mimalloc·oneTBB ON / Tracy OFF인 Release 구성에서 실제 플레이 루프를 측정했다.
이번 두 곡에서는 **전체 화면 복사·출력이 가장 큰 지속 비용**이며,
**BGA 프레임을 새로 합성하는 순간이 긴 프레임의 주원인**이었다.
건반·회전 스크래치와 콤보·판정 텍스트도 노트 자체보다 비용이 컸다.
oneTBB는 곡 목록 분석에만 쓰이므로 플레이 FPS 문제의 직접 해결책이 아니다.

## 조건과 한계

- 실험 브랜치의 `BMS_USE_MIMALLOC=ON`, `BMS_USE_ONETBB=ON`, `BMS_USE_TRACY=OFF`, Release.
- 기존 플레이어의 Main.cpp, AudioEngine.cpp, 실제 WASAPI 출력, 비동기 BGA, WM_PAINT, 화면 출력을 사용했다.
  offscreen 렌더링 결과가 아니다. 별도 실행파일 `BmsGameplayProfile`에서 계측만 활성화했다.
- 기존 설정을 별도 파일에 복사했다. 원래 설정/플레이 기록은 변경하지 않았다.
  1920×1080 전체화면, FPS 무제한, 배속 6.00x, 노트 높이 16px, BGA 밝기 20%,
  버퍼 32, 자원 표시 ON, CPU/메모리/GPU 상한 100%. 기존 GPU 옵션은 OFF였다.
- Creamy Sugary OVERDRIVE!!! Another(1,426노트), energy trixxx Another(1,252노트).
  30초로 이동해 자동으로 입력·콤보·키음을 발생시키고 8~10초 플레이했다.
  초기화/시크 영향을 피하기 위해 CSV의 31~37초 구간을 비교했다.
- 타 BMS 실행기를 동일 조건에서 직접 계측하지 않았다. 아래 결과만으로 타 구동기와의 차이를 전부 설명하거나
  모든 곡에서 1,000FPS를 보장할 수는 없다. 장시간 발열, 더 높은 노트 밀도, 다른 영상 포맷은 추가 검증 대상이다.
- FPS는 렌더 루프 처리율(1,000 / 평균 interval_ms)이다. 모니터에 실제 표시된 프레임 수는 다르다.
  GPU 큐가 차면 코드가 화면 제출을 건너뛰어도 현재 앱의 FPS 카운터는 증가한다.
  GPU 작업 자체의 실행 시간은 이 CSV의 upload/draw 시간과 같지 않다. 이들은 CPU 호출 시간을 측정한다.

## 실제 결과

| 곡 / 조건 | 렌더 루프 FPS | 평균 프레임 처리 | 프레임 처리 p99 |
| --- | ---: | ---: | ---: |
| Creamy / CPU, 첫 세부 측정 | 864 | 1.137 ms | 2.480 ms |
| Creamy / CPU, 반복 | 861 | 1.143 ms | 2.398 ms |
| Creamy / GPU, 반복 | 989 | 0.989 ms | 2.175 ms |
| energy trixxx / CPU | 795 | 1.225 ms | 2.485 ms |
| energy trixxx / GPU | 978 | 0.990 ms | 2.185 ms |
| Creamy / CPU, 버퍼 256 참고 측정 | 879 | 1.113 ms | 2.267 ms |
| Creamy / CPU, BGA 밝기 0% 참고 측정 | 1,017 | 0.968 ms | 1.411 ms |
| Creamy / CPU, 1280×720 창모드 참고 측정 | 1,418 | 0.692 ms | 1.256 ms |

다른 GPU 반복에서는 912~1,037FPS로 변동했다. 버퍼 변경은 한 번의 참고 측정으로 유의한 개선을 입증하지 않는다.
720p는 해상도와 창모드를 함께 바꿨으므로 순수 해상도 효과로 해석하면 안 된다.
프레임 시간은 interval보다 CSV 기록/자동 입력 등의 간격 비용만큼 짧다.

## 병목별 증거와 해결 순서

### 1. 전체 화면 복사와 최종 출력 — 최우선

Creamy CPU 세부 측정에서 최종 출력 `present_ms`는 평균 **0.492ms**,
BGA 배경 복사 `bga_copy_ms`는 **0.149ms**였다. 전체 프레임 1.137ms의 절반 이상을 차지한다.
반복에서도 두 항목 합은 약 0.667ms로 비슷했다. 1,000FPS의 프레임 예산은 1ms다.

`drawBgaBackplate`는 캐시된 배경도 매 프레임 전체 화면 BitBlt로 복사한다.
WM_PAINT의 CPU 경로는 결과를 다시 화면에 BitBlt한다.
GPU 경로도 CPU에서 만든 전체 화면 DIB를 매 프레임 UpdateSubresource로 업로드한다.
GPU 반복 측정에서 이 업로드만 **0.313ms**, D2D 그리기 호출은 **0.069ms**였다.
즉 GPU 옵션을 켜도 CPU 이미지 전송 비용이 계속 남는다.

해결책은 BGA와 정적 기어를 GPU 텍스처로 보관하고, 바뀐 영상 프레임/영역만 갱신하는 것이다.
노트·텍스트·이펙트를 그 위에 GPU에서 직접 합성하면 전체 CPU DIB 복사/업로드를 줄일 수 있다.
현 구조를 유지하는 단기안은 동적 영역 갱신, 다중 업로드 리소스/스테이징 정책을 비교 측정하는 것이다.
UpdateSubresource는 리소스 경합 시 복사가 더 생길 수 있다.
([Microsoft 문서](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource))

추가로 현재 `CompositionPresenter::present`는 **화면 큐가 준비됐는지 확인하기 전에** 업로드와 D2D 그리기를 한다.
한 GPU 세부 측정에서는 프레임 약 75%가 제출 지연 상태였다(다른 실행에서는 달랐다).
큐가 찬 경우 비싼 렌더 작업을 시작하지 않고 입력·판정·오디오 갱신만 계속하는 구조가 낭비를 줄인다.
이 변경은 화면에 보이는 프레임과 처리 루프 FPS를 별도로 표시하며 검증해야 한다.
([DXGI의 대기 없는 제출 동작](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present))

### 2. BGA가 바뀌는 프레임 — 끊김 개선의 최우선

CPU 세부 측정 5,184프레임 중 BGA 재합성은 180프레임이었다.
일반 프레임은 평균 **1.092ms**, 재합성 프레임은 **2.396ms**였다.
재합성 작업 자체가 약 **1.235ms**이며 이미지 합성 0.356ms, 어둡게 처리하는 전체 화면 루프 0.443ms 등이 포함된다.
해당 구간의 이미지 디코드 평균은 거의 0에 가까웠다. 이미 디코드된 BGA를 확대·합성·명암 처리하며 복사하는 비용이 핵심이다.
energy trixxx에서도 일반 1.178ms / 재합성 2.455ms로 같은 경향이었다.

영상/이미지는 원래 크기로 GPU에 올리고, 확대·검은 덮개·레인 투명도 합성을 GPU로 이동하는 것이 우선이다.
전환할 다음 이미지의 최초 디코드는 미리 준비하되, 매 프레임 영상 전체를 CPU에서 재가공하는 흐름을 피한다.
BGA가 꺼졌을 때도 현재 코드는 영상 갱신 요청을 하므로, 밝기 0%에서는 디코드/리사이즈 요청을 쉬고
다시 켤 때 현재 오디오 시각으로 이동시키는 개선도 가능하다.

### 3. 건반·회전 스크래치와 텍스트 — 다음 순서

CPU 세부 측정에서 건반(`deck_ms`)은 평균 **0.124ms**, 텍스트는 **0.118ms**,
energy trixxx의 텍스트는 **0.191ms**였다. 키 빔은 곡에 따라 0.063~0.102ms였다.
GPU 경로에서도 건반은 CPU에서 약 0.121ms를 쓰고 그 결과를 GPU에 복사한다.

건반의 누름/기본 상태를 GPU 스프라이트로 보관하고, 스크래치는 GPU에서 회전시키는 것이 적절하다.
현재도 키 이미지 확대 결과는 캐시하므로 단순히 동일한 캐시를 또 추가하는 것은 해결책이 아니다.
텍스트는 기존 문자열 캐시가 있지만 콤보 숫자가 계속 변해 새 문자열을 만들고,
CPU 경로는 크기 애니메이션마다 픽셀 보간·합성을 한다. 숫자 0~9와 COMBO 문자를 재사용하는
글리프/스프라이트 아틀라스로 바꾸고 GPU에서 배율만 적용하면 윤곽선·애니메이션을 유지할 수 있다.
([리소스 재사용과 아틀라스에 대한 Microsoft 가이드](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance))

### 4. 이번 구간에서 우선순위가 낮은 항목

- 노트 그리기: Creamy CPU **0.0088ms**, GPU **0.0004ms**. 노트를 병렬화하는 작업부터 할 근거가 약하다.
- 오디오 시각 조회: 보통 **0.001ms 미만**. 오디오 스레드 전체 실행 시간을 의미하지는 않는다.
  버퍼 32→256 비교도 이번 FPS 저하의 주원인이라는 증거가 없었다.
- 시스템 사용량 텍스트: 약 **0.01ms**. 700→1,000FPS 차이를 주로 설명하지 못한다.
- oneTBB: 플레이 시작 후의 렌더링에는 사용하지 않는다. 곡 분석 속도 개선과 플레이 FPS를 구분해야 한다.
- 주기적인 강제 메모리 비우기는 복사·명암 처리 비용을 해결하지 못하고 재생성 비용을 만들 수 있다.

계측 항목 중 bga_ms/재합성 세부 항목, judgement_ms/deck/text 등은 부모-자식 관계다. 전부 합산하면 중복된다.
GDI 작업은 배치 처리되므로 일부 비용은 뒤의 GdiFlush/출력 구간에서 관측될 수 있다.

## 재현

```powershell
cmake --build build-performance-optimized --config Release --target BmsGameplayProfile
# 원본 설정 대신 복사본의 절대 경로를 지정한다.
$env:BMS_PLAYER_CONFIG_PATH = 'C:/작업폴더/복사한설정.ini'
./build-performance-optimized/Release/BmsGameplayProfile.exe '곡 파일.bme' 'cpu.csv' cpu 30 8
./build-performance-optimized/Release/BmsGameplayProfile.exe '곡 파일.bme' 'gpu.csv' gpu 30 8
python ./tools/Summarize-GameplayProfile.py cpu.csv gpu.csv --start 31 --end 37
```

동시에 두 측정 앱을 실행하지 않는다. 생성된 CSV/요약 JSON은 `build-gameplay-results`에 보관했고 Git에는 넣지 않았다.
이 측정 당시에는 계측과 진단만 수행했다. 이후 하이브리드 실험은 별도 옵션/실행파일로 추가했으며 기본 빌드는 유지한다.

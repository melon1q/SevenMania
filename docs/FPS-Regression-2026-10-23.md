# BGA 영상 합성에 의한 평균 FPS 저하 수정

## 원인

호환성 추가 커밋 `2cc46b8`의 `Main.cpp::drawBgaBackplate`에서 재현했다. BGA 영상의 매 픽셀마다 `assetId()` 문자열 처리와 `bitmapColors.find()` 해시 조회, 키 색·알파 결정, RGB 나눗셈을 수행했다. 불투명한 BASE 영상도 같은 경로를 사용했다.

1920×1080에서는 영상 갱신 한 번에 최대 2,073,600회의 동일 자산 조회가 반복된다. 이 비용은 CPU/GPU 모드가 함께 사용하는 CPU BGA 합성 단계에 있어 GPU를 켜도 사라지지 않았다. 영상이 계속 바뀌면서 배경 캐시도 매번 무효화되어, 지속적인 평균 FPS 저하로 이어졌다.

실제 Creamy 측정에서 GPU 모드의 평균 프레임 처리는 41.427ms, 그중 영상/이미지 합성이 38.662ms였다. 노트 렌더링은 0.011ms였으므로 이번 심각한 저하의 주원인은 노트 수나 오디오 판정 계산이 아니다.

## 변경

- 이미지 ID·색 키·실효 알파는 레이어마다 한 번만 계산한다.
- 새 `VideoComposite.h::blend`에서 불투명하고 색 키가 없는 영상은 행 단위 `memcpy`를 사용한다.
- 색 키가 있는 불투명 영상은 키 검사 후 복사하고, 실제 반투명 영상만 RGB 블렌딩을 수행한다.
- 밝기가 0인 BGA, 비활성화된 POOR 영상에는 레이어 프레임 요청을 보내지 않는다.
- 불투명 POOR가 전체 화면을 대체할 때 뒤쪽 영상 레이어를 다시 합성하지 않는다.
- `BgaTests.cpp`에 독립적인 scalar 기준 구현과의 픽셀 비교를 추가했다. 알파 5종 × 화면 밖을 포함한 원점 6종 × 색 키 3종, 총 90조합을 검증한다.

BASE62·ARGB·EXBMP·레이어 색 키·화면 밖 클리핑은 유지했다. FPS 제한/1500FPS 상한, 판정 시계, 오디오 시계, 영상의 작성 시각은 바꾸지 않았다.

## 측정 방법

Windows, Release, `build-performance-hybrid`, mimalloc/oneTBB/hybrid ON. 수정 전 계측기는 `BmsGameplayProfile-before.exe`로 보존했다. 실제 Main.cpp의 플레이·WM_PAINT·BGA·출력 경로를 실행하는 기존 `BmsGameplayProfile`을 사용했다.

Creamy Sugary OVERDRIVE!!! 7another, 1,426노트. 1920×1080, FPS 무제한 또는 240, Reflex OFF, DirectComposition OFF, 버퍼 256, 자원 상한 100%. 임시 INI 파일을 사용했다. 실제 적용된 전체 설정은 `build-fps-regression/profile.ini`에 있다. 개인 설정·곡·기록 파일은 수정하지 않았다.

매 실행은 30초로 이동해 6초간 자동 입력하며, 초기 시크 영향을 줄이기 위해 CSV의 **31–35초**만 집계했다. 아래 수정 후 대표값은 다른 테스트와 겹치지 않은 단독 재측정이다. 초기 CPU 참고 실행은 회귀 검사와 일부 겹쳐 대표값에서 제외했다.

| 조건 | 수정 전 렌더 루프 FPS | 수정 후 렌더 루프 FPS | 평균 처리 ms 전→후 | 처리 p99 ms 전→후 |
|---|---:|---:|---:|---:|
| GPU + CPU, 무제한 | 24.1 | 1,307.3 | 41.427 → 0.665 | 42.769 → 2.211 |
| CPU, 무제한 | 21.4 | 1,008.3 | 41.281 → 0.973 | 46.316 → 2.264 |
| GPU + CPU, 240 제한 | 별도 미측정 | 239.8 | 수정 후 1.275 | 수정 후 3.285 |

GPU 무제한 반복은 1,305.0FPS 및 1,307.3FPS였다. 영상 갱신 프레임에서 합성 단계 평균은 GPU **38.662 → 0.139ms**, CPU **38.443 → 0.135ms**였다. 수정 후 4초 구간에서 영상 갱신이 120회 관측되어 정상적인 약 30fps 영상 갱신을 유지했다.

240 제한에서 프레임 간격 평균은 약 4.17ms, 간격 p99는 4.729ms였다. 평균 목표는 회복했지만 모든 프레임 간격이 완전히 일정하다는 주장은 아니다.

Random도 비교했다. 해당 실행의 선택 구간은 **영상 갱신이 0회**였으므로 영상 합성의 개선 증거로 사용하지 않았다. 일반 경로 GPU 처리율은 전후 1,336.6 → 1,332.3FPS로 비슷했다. 이 실행만으로 Random의 영상 코덱이나 싱크가 정상이라는 결론을 내리지 않는다.

FPS는 `1000 / 평균 interval_ms`인 렌더 루프 처리율이다. 모니터에서 실제로 표시한 고유 프레임 수·입력 지연과는 다르다. GPU 복귀 실패는 없었다. 오디오 empty-padding/render-failure 증가도 없었지만, 일부 실행의 오디오 CPU 계측값이 0이므로 이를 오디오 부하가 없다는 근거로 사용하지 않았다.

## 남은 비용

수정 후 GPU 단독 재측정에서는 배경 CPU 영역 복사 약 0.224ms, 최종 제출 호출 약 0.126ms가 남았다. CPU 경로에서는 최종 화면 출력 약 0.387ms가 가장 큰 지속 비용 중 하나다. 노트는 두 경로 모두 약 0.01ms였다.

이번에는 측정으로 드러난 영상 합성 회귀를 수정했다. 복사·출력을 없애는 렌더러 전면 교체는 하지 않았다. 다른 해상도·GPU·코덱·장시간 발열 상태 및 모든 실곡에서 동일 FPS를 보장하지 않는다.

## 검증과 재현

Release 빌드 성공. `BmsBgaTests`, `BmsVideoTests`, `BmsDisplayTests`, `BmsCompatibilityTests`, `BmsCompatibilityPlaybackTests` **5/5 통과**. 새 픽셀 비교와 기존 CPU/GPU 표시·영상 재시작·다중 레이어·실제 입력/판정/믹서 검사를 포함한다.

```powershell
cmake --build build-performance-hybrid --config Release --target BmsPlayer BmsGameplayProfile BmsBgaTests BmsDisplayTests BmsCompatibilityPlaybackTests --parallel 4
ctest --test-dir build-performance-hybrid -C Release -R '^(BmsBgaTests|BmsVideoTests|BmsDisplayTests|BmsCompatibilityTests|BmsCompatibilityPlaybackTests)$' --output-on-failure

$env:BMS_PLAYER_CONFIG_PATH = '별도 설정.ini의 절대 경로'
./build-performance-hybrid/Release/BmsGameplayProfile.exe '채보의 절대 경로' '결과.csv' gpu 30 6 0 off
python tools/Summarize-GameplayProfile.py '결과.csv' --start 31 --end 35
```

원본 CSV·실행 로그·집계 JSON은 무시된 `build-fps-regression` 폴더에 보관한다. 합성 helper와 테스트 및 이 보고서만 관련 소스 변경과 함께 커밋하며, 개인 곡이나 측정용 실행파일은 커밋하지 않는다.

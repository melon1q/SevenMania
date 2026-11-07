# 저지연 연동 준비: 1·2단계

2026-10-21, 로컬 `main`에 `codex/hybrid-renderer`를 fast-forward 병합했다.
병합 지점은 `0d1299d`이며, 원래 스테이징/작업 중이던 변경은 그대로 보존했다.
이번 범위는 기준 측정과 SDK 독립 연결 구조다. SDK 설치·활성화, 지연 마커, SDK sleep, Boost,
드라이버 설정 변경은 하지 않았다. 저지연 기능 기본값/실제 상태는 OFF다.

## 그래픽 설정

- DirectComposition 체크박스는 기존 GPU 사용 옵션의 명칭을 복원한 것이다. 별도의 중복 렌더러를 추가하지 않았다.
  Hybrid 빌드에서는 CPU 기어/노트와 GPU 합성을 사용한다. 체크 해제 시 기존 CPU 경로다.
- 노트 서브픽셀 렌더링과 노트 안티앨리어싱을 각각 저장한다. 둘 다 기본 ON으로 기존 화질을 유지한다.
  서브픽셀 OFF에서는 노트 경계를 정수 픽셀로 맞추고, AA OFF에서는 CPU 경계 혼합과 GPU 도형 AA를 끈다.
  이 옵션은 텍스트 ClearType 설정이나 전체 화면 MSAA 설정이 아니다.
- 저지연 드롭다운에는 끔/NVIDIA Reflex/AMD Anti-Lag 2가 표시된다. 현재는 SDK 미연결로 비활성 상태다.
  공급사 이름만으로 지원을 추정하거나 지원된다고 표시하지 않는다.
- VSync 선택 시에만 동기화한다. 무제한의 1500FPS 상한과 입력·판정/오디오 시간 기준은 유지한다.

## 1단계: 기준 측정

현재 PC, 240Hz 모니터, 1920×1080, 배속 6.00, BGA 20%, WASAPI 요청 버퍼 32,
Creamy Sugary OVERDRIVE!!! ANOTHER 1426노트의 30~38초를 자동 입력으로 재생했다.
CPU/GPU(Hybrid) × 무제한/240/VSync를 각각 2회, 총 12회 순차 실행했다.
CPU 프레임·간격의 분석 구간은 시크 직후와 종료를 제외한 31~37초다.
오디오 오류·GPU 제출·GPU 타임스탬프 카운터는 전체 8초 구간이다.

기준 소스는 `0d1299d`와 기존 로컬 작업 변경을 포함한 상태다. 진단 빌드에는 FPS 선택 인자와
오디오 카운터만 먼저 추가했다. 이후 새 그래픽 옵션/연결 구조를 구현하기 전에 측정했다.
mimalloc/oneTBB ON, Tracy OFF, Hybrid ON이다. 실제 배포 실행파일은 CSV와 오디오 진단 카운터를 기록하지 않는다.

| 모드 | 제한 | 반복 | CPU 처리 평균 ms | CPU 처리 p99 ms | CPU 루프 간격 p99 ms | GPU 제출/초 | 제출 간격 추정 p99 ms | GPU 표본 평균 ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| CPU | 무제한 | 1 | 1.450 | 2.998 | 3.087 | — | — | — |
| CPU | 무제한 | 2 | 1.708 | 3.123 | 3.233 | — | — | — |
| CPU | 240 | 1 | 1.210 | 2.336 | 4.670 | — | — | — |
| CPU | 240 | 2 | 1.256 | 2.455 | 4.658 | — | — | — |
| CPU | VSync | 1 | 3.493 | 4.187 | 7.622 | — | — | — |
| CPU | VSync | 2 | 3.494 | 4.173 | 7.623 | — | — | — |
| GPU | 무제한 | 1 | 0.479 | 1.990 | 2.022 | 612.4 | 4.136 | 0.402 |
| GPU | 무제한 | 2 | 0.480 | 2.045 | 2.084 | 607.8 | 4.658 | 0.405 |
| GPU | 240 | 1 | 0.849 | 2.092 | 4.894 | 241.2 | 5.063 | 0.489 |
| GPU | 240 | 2 | 0.833 | 2.075 | 4.646 | 242.4 | 4.657 | 0.477 |
| GPU | VSync | 1 | 0.194 | 1.876 | 2.514 | 228.4 | 8.465 | 0.493 |
| GPU | VSync | 2 | 0.191 | 1.835 | 2.509 | 226.1 | 8.610 | 0.497 |

12회 모두 WASAPI 렌더 오류 0회, 재생 중 빈 패딩 감지 0회였다. 빈 패딩은 끊김 위험 지표이며
그 자체로 들리는 끊김을 증명하지 않는다. 청음/루프백 녹음 검증을 대신하지 않는다.
GPU 제출 간격은 성공한 프레임의 CPU 루프 시작 간격으로 추정하며 실제 스캔아웃 간격이 아니다.
GPU 타임스탬프 표본은 렌더 명령 구간으로, 전체 GPU 사용률이나 입력 지연과 같지 않다.
CPU VSync 처리 시간에는 DwmFlush 대기가 포함된다. GPU VSync는 제출 전 그리기를 생략하므로
CPU 프레임 평균이 작아도 출력이 빠르다는 뜻은 아니다. 이번 VSync GPU 측정은 약 226~228회/초로
240Hz를 충족하지 못했다. 이후 단계에서 이 손실과 긴 프레임도 비교해야 한다.
자동 키 입력/OS 페인트로 표시 FPS와 업데이트 제한/제출 횟수가 다를 수 있으므로 별도로 해석한다.
실제 입력→화면 지연은 아직 측정하지 않았다. 3단계 마커 또는 외부 장비가 필요하다.

재현 명령:

```powershell
cmake --build build-performance-hybrid --config Release --target BmsGameplayProfile
./tools/Run-LatencyBaseline.ps1 -Profiler build-performance-hybrid/Release/BmsGameplayProfile.exe `
  -Chart '곡의 절대 경로.bme' -Settings '복사할 설정.ini' -Repeats 2
python tools/Summarize-LatencyBaseline.py build-latency-baseline > build-latency-baseline/summary.json
```

실제 곡/개인 설정은 커밋하지 않는다. 원본 CSV/로그/설정 복사본과 바이너리·곡·설정 SHA256은
Git 제외 폴더 `build-latency-baseline`의 manifest에 보관한다. 이전 진단 실행파일은
`build-stage12-snapshot/BmsGameplayProfile-baseline.exe`에 보관한다.

## 2단계: SDK 독립 공통 구조

`src/LowLatency.h`가 Off/Reflex/Anti-Lag 2를 구분하고 공급사별 Backend의 capability probe/release 경계를 제공한다.
렌더러가 실제 D3D11 장치와 공급사 ID를 넘기지만, 현재 SDK backend는 등록하지 않는다.
SDK가 없어도 추가 DLL 로드·링크·프레임 대기가 발생하지 않는다.
구조에는 다음 계약을 두었다.

- 실제 active 상태는 항상 Off, 프레임 대기 소유자는 기존 FramePacer 하나다.
- SDK 존재 여부와 장치 지원 여부는 별도 capability다. 향후 실제 SDK probe 결과로 확인한다.
- 장치 해제/재생성 시 backend 자원을 정리하며, 한 backend의 실패는 다른 backend 상태에 전파하지 않는다.
- `failSession` 후 해당 backend는 같은 세션에서 다시 probe하지 않고 상태로 이유를 남긴다.
- capability가 Ready인 테스트 backend도 이번 단계에서는 활성화하지 못한다. 마커·SDK 제어 구현은 별도 단계다.

단위 테스트는 모의 backend로 SDK 미설치, 미지원 장치, 공급사별 상태, 세션 오류 차단, 장치 해제,
기존 프레임 대기 유지와 기본 Off를 검증한다. GPU/CPU 픽셀 테스트는 새 렌더링 토글을 검증한다.
설정 UI 테스트는 옵션 표시·비활성 상태·저장·재시작 복원을 검증한다.

다음 단계로 넘어가기 전: 입력/오디오 시간 기준을 바꾸지 않고 프레임 ID·마커 위치를 먼저 정한다.
SDK 하나씩 검증하고 입력 지연 개선이 반복 오차를 넘는지 확인한다. 잠정 성능 허용치는 평균뿐 아니라
p99 처리/제출 간격의 악화가 5% 또는 기준선 반복 편차 중 큰 값 이내인 것으로 잡되,
측정 환경을 고정한 후 SDK 실험 전에 확정한다. 판정 오류·오디오 출력 오류가 새로 생기면 채택하지 않는다.

2단계 추가 후에도 같은 6개 모드를 1회씩 재생했다. 오디오 렌더 오류/빈 패딩은 모두 0이었다.
GPU 무제한의 표시 FPS는 종료 직전 1437이었지만 제출은 약 240.5회/초로 기준선의 607.8~612.4와 달랐다.
VSync 제출은 약 239.5회/초였다. CPU/GPU 처리 시간과 실제 제출 가능 시점은 서로 다른 지표이며,
Windows 합성/표시 경로 상태가 고정됐다고 단정하지 않는다. 이를 SDK 효과나 성능 향상으로 주장하지 않는다.
후속 원본과 manifest는 `build-latency-after`에 보관했다. 다음 단계에서는 PresentMon 등 실제 표시 추적을
추가해 실행 환경 차이를 통제한 뒤 비교해야 한다.

공식 문서: [NVIDIA Reflex 연동 가이드](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideReflex.md),
[AMD Anti-Lag 2 SDK](https://github.com/GPUOpen-LibrariesAndSDKs/AntiLag2-SDK).
두 SDK의 sleep/제어 순서를 동일하다고 가정하지 않는다. 일반 드라이버 Anti-Lag 옵션을 변경하지 않는다.
실제 Streamline 연동 후에는 OFF 상태에서도 필요한 마커/sleep 호출을 공식 가이드대로 유지해야 한다.
현재 단계는 해당 SDK 자체를 연결하지 않았으므로 그런 호출이 없다.

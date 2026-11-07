# 출력 프레임 간격 안정화 (2026-10-22)

## 확인한 원인

기존 출력 경로는 큐가 가득 차 `Present`를 미룬 뒤 다음 게임 프레임까지 기다렸다. 숫자 FPS 제한의 타이머와 Windows 출력 큐의 준비 시점이 어긋나면, 렌더링 FPS가 충분해도 제출 간격이 한 프레임 길이만큼 늘어날 수 있었다. 비동기 `Present`가 `DXGI_ERROR_WAS_STILL_DRAWING`을 반환하면 이미 소비한 준비 신호도 잃었다.

## 변경

- 게임 프레임 타이머와 DXGI 출력 준비 이벤트를 함께 기다린다. 무제한 모드의 짧은 폴링 대기에서도 준비 이벤트를 확인한다. 메시지 입력 처리는 기존대로 우선 처리한다.
- 준비 이벤트가 오면 가장 최근에 그린 대기 프레임을 제출한다. CPU 렌더링·오디오·판정·게임 시계를 다시 실행하지 않는다. 대기 프레임을 누적하지 않으며 새 프레임이 이전 것을 대체한다.
- 자동 리셋 이벤트를 메시지 대기 함수가 소비한 경우, 같은 신호를 `present()`에서 다시 기다리지 않도록 예약을 전달한다.
- `WAS_STILL_DRAWING`에서는 예약과 대기 프레임을 유지한다. 성공한 제출 또는 장치 초기화 때 해제한다.
- 일반 GPU 경로도 출력 준비 확인을 업로드·GPU 그리기보다 먼저 수행한다.
- 일시정지 화면도 출력 준비 이벤트에 반응한다. Reflex가 현재 프레임의 대기를 담당하면 일반 프레임 경로가 예약을 사용하며, SDK Sleep을 추가 호출하지 않는다.
- OUTPUT 집계에서 `DXGI_STATUS_OCCLUDED`를 정상 제출로 계산하지 않는다.

VSync 선택, 숫자 FPS 제한, 무제한 1500FPS 상한, 자원 제한 설정은 유지한다. 강제 VSync, 프레임 보간, 표시 숫자 평활화는 추가하지 않았다.

## 측정

같은 장비에서 640×360 하이브리드 프레임, 프레임당 0.5ms의 합성 CPU 그리기 작업, 각 4초를 비교했다. 아래 수치는 **Present가 받아들인 제출 시각**이다. 모니터의 실제 스캔아웃이나 입력 지연 측정은 아니다.

| 조건 | 제출 FPS: 이전 → 수정 | 제출 간격 p99: 이전 → 수정 |
|---|---:|---:|
| 240FPS / DirectComposition | 233.74 → 239.47 | 8.14 → 4.45ms |
| 240FPS / 일반 출력 | 235.74 → 239.22 | 8.15 → 4.43ms |
| 무제한 / DirectComposition | 252.48 → 261.24 | 4.75 → 4.70ms |
| 무제한 / 일반 출력 | 266.22 → 262.72 | 4.74 → 4.71ms |

240FPS 조건에서 놓친 제출 기회가 줄었다. 무제한 조건에서는 준비 이벤트를 이미 자주 확인하므로 개선 폭이 작고, 평균 제출 FPS 차이에는 실행 간 편차가 있다. 모든 환경에서 고정된 OUTPUT 값을 보장하지 않는다. Reflex 활성 상태의 백그라운드 비교에서는 긴 드라이버 대기가 계속 관찰되어, 그 조건의 간격 개선은 확인하지 못했다. SDK 기능을 자동으로 끄거나 설정을 변경하지 않았다. Windows 합성기·드라이버·다른 프로그램의 부하, 렌더링 시간 및 사용자가 설정한 자원 상한은 계속 영향을 준다.

반복 측정용 도구는 `BmsOutputCadenceProfile` 대상이다. 예:

```powershell
cmake --build build-performance-hybrid --config Release --target BmsOutputCadenceProfile
./build-performance-hybrid/Release/BmsOutputCadenceProfile.exe 240 dc
./build-performance-hybrid/Release/BmsOutputCadenceProfile.exe 0 native
```

작은 테스트 창은 활성화하지 않으며 4초 후 닫힌다. 별도 곡·사용자 설정·드라이버 설정을 변경하지 않는다.

검증에는 두 핸들(타이머/출력 이벤트)의 구분, 메시지가 먼저 온 경우, 신호 중복 소비 방지, CPU 렌더/게임 시계 재실행 방지, 큐 포화 재시도, CPU fallback, CPU/GPU 표시·일시정지·QUIT, 실제 NVIDIA Reflex ON/OFF와 미설치 SDK fallback, 1500FPS 상한 테스트를 포함했다.

참고: [Microsoft의 DXGI 대기 객체 안내](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject), [Present 동작 및 반환 상태](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).

# NVIDIA Reflex 5·6단계 검증

## 범위와 배포 조건

사용자 요청에 따라 AMD Anti-Lag 2 SDK·백엔드·옵션은 구현하지 않는다.
현재 저지연 설정은 끔 / NVIDIA Reflex 두 가지이며 기본값은 계속 OFF다.
기존 `Mode::AntiLag2` 열거값은 비활성 예약값일 뿐 실행 가능한 기능이 아니다.
Boost와 드라이버 설정 변경도 추가하지 않는다.

이번 변경은 설정 조합 반복 측정과 복구 검증을 추가한다. 판정 시각과 오디오 시계,
실제 프레임 루프는 변경하지 않는다. 7단계 배포 승인을 자동으로 내리지 않는다.

## 재현

```powershell
./tools/Run-ReflexValidation.ps1 `
  -Profiler build-performance-hybrid/Release/BmsGameplayProfile.exe `
  -Chart '곡의 절대 경로.bme' -Settings '원본 설정.ini' `
  -OutputDirectory build-reflex-validation -Repeats 3
python tools/Summarize-ReflexValidation.py build-reflex-validation > build-reflex-validation/summary.json
```

새 출력 폴더가 필요하다. 각 실행마다 원본 설정을 새로 복사하고 종료/실패 시 환경변수를
복원한다. 곡·실행파일·설정 해시, 실행 순서, 종료 코드, 원본 CSV·로그를 보존한다.
원본 자료와 개인 곡/설정은 Git에 포함하지 않는다.

GPU 렌더링에서 무제한(기존 1500 FPS 상한), 240 FPS, VSync 각각 OFF/ON을 3회씩
측정한다. 매 반복마다 OFF/ON 순서를 뒤집는다. 시작/종료 1초씩을 제외한 같은 곡 구간을
분석하며, 로그의 Reflex 실제 활성 상태·드라이버 확인·GPU 폴백·오디오 오류를 필수로 확인한다.
누락된 카운터나 불완전한 측정 구간은 통과 처리하지 않는다.

실행 전 정한 성능 허용치는 OFF 반복의 p99 평균 대비 5% 또는 OFF 반복 사이 p99 범위 중
큰 값이다. CPU 처리 시간·루프 간격·성공 제출 간격 추정치의 ON p99 평균을 각각 비교한다.
오디오 렌더 오류와 빈 패딩은 모두 0이어야 한다. 분석 도구는 실패 시 종료 코드 1을 반환한다.

제출 간격은 CPU CSV 기반 추정치이며 실제 화면 표시 간격이나 물리적 입력 지연이 아니다.
빈 패딩이 0이어도 청음상 무결성을 증명하지 않는다. 자동 입력 결과는 수동 판정 검증을
대신하지 않는다. 비교 결과가 좋아도 `release_approved`는 항상 false다.

## 복구 자동 검증

- `BmsLowLatencyTests`: FPS 제한 전환, 프레임당 SDK Sleep 1회, OFF 시 기존 대기 복귀,
  일시정지에 해당하는 OFF/재개 ON, 장치 해제·재연결, 단조 증가 프레임 ID,
  미지원 교체 장치, configure/Sleep/marker 오류 후 세션 차단과 재시도 방지.
- `BmsGpuTests`: 실제 NVIDIA SDK로 DirectComposition/네이티브 출력 ON/OFF 및 Present,
  활성 상태에서 렌더러 해제, 크기를 바꾼 장치 재생성, Sleep/마커/드라이버 활성 상태 복구.
- SDK 비활성 빌드: 같은 컨트롤러·GPU 테스트로 SDK 없이 기존 렌더링이 동작하는지 검사.
- `tests/ReflexValidationTests.py`: 정상 비교, 누락 카운터, 성능 악화, 불완전 실행 목록,
  잘못된 수치를 검증한다. 저장소의 쓰기 가능한 임시 폴더를 TEMP/TMP로 지정할 수 있다.

실제 Alt+Tab, 모니터 변경, OS/드라이버 장치 제거, 곡 전환·재시작의 전체 UI 흐름은 별도
수동 검증이 필요하다. 모의 SDK 실패를 실제 드라이버 장애 재현이라고 보고하지 않는다.

## 2026-10-23 실행 결과

컨트롤러·실제 NVIDIA GPU 복구 테스트, SDK OFF 빌드의 동일 2개 테스트,
Python 분석기 6개 테스트가 모두 통과했다. 실제 GPU 테스트에서는 지원 경로로 진입해
드라이버 활성 상태까지 확인했다. 미지원 경로의 skip만으로 통과한 결과가 아니다.
기존 진단기의 `quit-check`도 OFF/ON 각각 실행해 일시정지 후 곡 종료 시
GPU 자식 창 제거, 곡 목록 렌더링, Reflex OFF 복귀를 모두 확인했다.

기존 4단계의 진단 바이너리로 Creamy Sugary OVERDRIVE!!! ANOTHER의 30~38초를
설정 조합별 3회씩 총 18회 재생했다. 다른 대화에서 진행 중인 BMS 호환성 변경을
측정 바이너리에 섞지 않았다. 따라서 이 측정은 현재 미커밋 호환성 변경의 성능 보증이 아니다.
원본과 해시는 `build-reflex-stage56-measurements/manifest.json`, 분석 결과는
같은 폴더의 `summary.json`에 보관한다.

| 설정 | CPU 루프 p99 OFF→ON ms | 제출 간격 추정 p99 OFF→ON ms |
|---|---:|---:|
| 무제한 | 1.604 → 1.851 | 4.769 → 4.615 |
| 240 FPS | 4.650 → 4.266 | 8.640 → 8.360 |
| VSync | 2.699 → 1.846 | 7.968 → 8.193 |

수치는 각 실행 p99의 3회 평균이다. 18회 모두 GPU 폴백·오디오 렌더 오류·빈 패딩 0회,
ON은 Reflex와 드라이버 활성 상태 1, OFF는 둘 다 0이었다. 사전 정의한 성능 기준은
통과했으나, 특히 240 FPS 제출 간격의 OFF 반복 범위가 약 7.846 ms로 크다.
무제한 CPU 처리 p99 평균도 OFF 1.554 ms에서 ON 1.824 ms로 늘었다.
반복 편차 안의 통과를 성능 개선이나 물리적 입력 지연 감소로 해석하면 안 된다.

7단계 선택 배포는 아직 승인하지 않았다. 수동 판정·청음, Alt+Tab·모니터/드라이버 장애,
현재 최종 소스의 반복 측정, 반복 오차를 넘는 물리적 입력 지연 개선 확인이 남아 있다.

# 성능 라이브러리 실험

브랜치: `codex/performance-libraries`. 기본 CMake 설정은 세 옵션 모두 OFF다.
기존 `BmsPlayer.exe`와 실험 실행파일은 별개이며, main 병합은 사용자 테스트 후 결정한다.

## 실행파일 만들기

PowerShell에서 저장소 루트를 기준으로 실행한다. Visual Studio C++ 도구와 CMake가 필요하며,
처음 설정할 때 공식 GitHub 저장소에서 고정 버전 소스를 내려받는다(SHA256 검증).

```powershell
./tools/Build-PerformanceExperiment.ps1
```

`build/Release`에 아래 파일이 생성된다. 세 실행파일이 같은 Songs, Assets, 설정을 사용한다.
한 번에 하나만 실행한다. 기존 `BmsPlayer.exe`는 이 스크립트가 교체하지 않는다.

| 실행파일 | 목적 |
| --- | --- |
| BmsPlayer-Baseline.exe | 라이브러리 OFF, 비교 기준 |
| BmsPlayer-Optimized.exe | mimalloc + oneTBB 실험 |
| BmsPlayer-Tracy.exe | 동일 실험 + Tracy 계측 |
| BmsPlayer-Hybrid.exe | mimalloc + oneTBB + CPU/GPU 하이브리드 실험 |

`-Variant Optimized`처럼 개별 빌드도 가능하다. TBB의 `tbb12.dll`을 실행파일 옆에 둬야 한다.
하이브리드만 빌드하려면 `-Variant Hybrid`를 사용한다. 이 빌드에서 그래픽의
`CPU + GPU 하이브리드 사용`을 체크하면 새 경로가 켜진다. 해제하면 기존 CPU 경로다.
자세한 변경 내용과 측정은 [Hybrid-Renderer.md](Hybrid-Renderer.md)에 있다.
배포할 때는 함께 생성되는 `Performance-Licenses`도 포함한다.

## 적용 범위

- Tracy 0.12.2: 프레임, BGA, 노트, HUD, 화면 제출의 CPU 소요 시간과 음원 디코딩/믹싱,
  곡 목록 분석을 계측한다. GPU 자체 실행 시간이나 모든 메모리 할당 추적은 추가하지 않았다.
  연결 전에는 on-demand 방식이며, localhost에서만 접속할 수 있다.
- mimalloc 2.2.4: C++ `new/delete`를 교체한다. CRT `malloc/free`, Windows/COM 할당은 유지한다.
- oneTBB 2022.2.0: 곡 목록 분석 작업의 스케줄러만 교체한다. 기존 최대 4개 작업 제한과
  폴더별 파서 재사용, 취소, 진행률 처리는 유지한다. 오디오/판정은 기존 스레드를 사용한다.

개별 비교는 CMake에서 `BMS_USE_TRACY`, `BMS_USE_MIMALLOC`, `BMS_USE_ONETBB`를 각각 ON/OFF 한다.
라이브러리 사용만으로 FPS 상승을 보장하지 않는다. oneTBB 변경은 플레이 FPS보다 곡 목록 로딩에 영향을 준다.

## 비교 방법

1. Baseline과 Optimized에서 같은 해상도, GPU 설정, FPS 제한, BGA 밝기, 사운드 버퍼를 사용한다.
2. 같은 곡·난이도·구간을 번갈아 최소 3번 실행한다. 첫 실행의 디스크 캐시 효과는 따로 본다.
3. 평균 FPS와 함께 끊김, 음 깨짐, 입력 감각, 메모리, 곡 목록 로딩 시간을 비교한다.
4. 병목은 [Tracy 0.12.2](https://github.com/wolfpld/tracy/releases/tag/v0.12.2)의 Windows profiler를 실행하고
   BmsPlayer-Tracy.exe의 localhost 클라이언트에 연결해 확인한다. 계측 중 수치는 일반 실행과 구분한다.

각 빌드 폴더의 `Release/BmsPerformanceExperiment.exe "곡 폴더"`는 임시 객체 할당 시간,
목록 분석 시간, 분석 결과 fingerprint를 출력한다. 비교 빌드의 fingerprint가 같아야 한다.
임시 객체 테스트는 합성 부하이며 FPS 결과로 해석하지 않는다.

## 이번 PC에서의 사전 측정

기본/Optimized를 번갈아 3회 측정한 중앙값이다. Tracy는 연결하지 않았다.

| 항목 | 기본 | Optimized |
| --- | ---: | ---: |
| 실제 채보 179개 분석 | 433.1 ms | 346.9 ms |
| 임시 객체 할당 합성 부하 | 89.8 ms | 5.8 ms |
| 실제 곡 렌더링 CPU 프레임 | 1.896 ms | 1.901 ms |

분석 결과 fingerprint는 모든 실행에서 일치했다. 렌더링은 Creamy Sugary OVERDRIVE!!! Another,
1920×1061, 배속 6, 이펙트 활성화, BGA/노트/HUD를 포함한 offscreen CPU 렌더 1,000프레임이다.
화면 출력 대기·실시간 오디오·GPU 실행은 이 수치에 포함되지 않으므로 게임 FPS로 해석하지 않는다.
목록 분석은 약 20% 빨라졌지만 이 렌더링 부하에서는 뚜렷한 개선이 없었다.
Tracy ON 빌드의 파서·목록·오디오·화면 테스트 4개와 Optimized의 목록·오디오 테스트 2개가 통과했다.

참고: [Tracy](https://github.com/wolfpld/tracy), [mimalloc](https://github.com/microsoft/mimalloc),
[oneTBB](https://github.com/uxlfoundation/oneTBB). 외부 소스나 빌드 산출물은 Git에 포함하지 않는다.

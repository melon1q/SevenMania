# uploadHybrid 병목 확인 및 영상 프레임 재사용 — 2026-11-02

## 결과

`a63aff3`과 [이전 CPU 계측 보고서](ReflexCpuAttribution-2026-11-02.md)를 확인했다. 이전 버퍼의 소유권 교체 비용은 단순한 shared_ptr 참조 카운트 변경만이 아니었다. **실제 영상 Frame 소멸자와 큰 픽셀 vector 해제가 렌더 스레드에서 실행되는 경로**를 직접 계측해 확인했다.

이를 줄이기 위해 `AsyncVideoBga`의 decoder worker에 최대 3개 프레임을 재사용하는 작은 풀을 추가했다. renderer나 BGA 갱신 규칙은 교체하지 않았다. Reflex, FramePacer, 입력 처리, 오디오, 판정 코드는 변경하지 않았다. 새로운 라이브러리도 없다.

동일 진단 바이너리에서 기존 할당 경로와 재사용 경로를 비교한 ON 20초×2회 결과:

- uploadHybrid 호출 P99: 0.8973→0.6662ms, 1.0487→0.6637ms.
- 전체 CPU P99: 1.4517→1.2605ms, 1.5522→1.2699ms.
- 1% Low: 620.1→698.2FPS, 572.8→694.2FPS.
- capture 전체 픽셀 버퍼 할당: 522→3회, 525→3회.
- 중앙 측정 구간의 렌더 스레드 Frame 해제: 490→0회, 493→0회.

모든 최악 프레임이 제거된 것은 아니다. D2D 이미지 복사·메모리 비교는 여전히 남는다. 여기서 확인한 개선을 드라이버/GPU 내부 병목의 완전 해결로 해석하지 않는다.

## 원인과 호출 구조

`CompositionPresenter::present`는 출력 큐가 준비됐을 때 `drawFrame → uploadHybrid`를 실행한다. CPU 게임 갱신 횟수와 실제 업로드 횟수는 같지 않다. 이번 r1의 중앙 17초에는 업로드 약 240회/초, 배경 복사 약 29회/초였다. 1500FPS 상한이라고 BGA를 초당 1500번 새로 생성하거나 업로드하는 구조가 아니다.

배경은 1920×1080 BGRA8(uint32_t, D3D/D2D B8G8R8A8_UNORM)이며 CPU 픽셀 버퍼 한 개는 8,294,400바이트(7.91MiB)다. D2D 배경/기어 영역은 ALPHA_MODE_IGNORE, 별도 투명 레이어는 premultiplied alpha다.

기존 배경 갱신은 이미 다음을 수행하고 있었다.

1. 같은 generation은 배경 업로드를 생략한다.
2. generation이 달라지면 이전 프레임과 위/아래 행을 memcmp로 비교한다.
3. 변경된 세로 범위만, 전체 가로 폭을 가진 직사각형으로 CopyFromMemory한다.
4. `uploadedPixels_ = hybridPixelOwner_`로 마지막 업로드의 비교용 owner를 교체한다.
5. 별도로 기어·좌하단·우상단 CPU 영역을 업로드한다.

따라서 전체 복사 제거/동일 이미지 업로드 생략을 이번에 새로 구현했다고 주장하지 않는다. r1 전/후의 배경 복사당 평균은 각각 약 4,856,356 / 4,857,592바이트였다. 평균 비교 행 수는 약 450행이고, 복사당 전체 1080행을 전송한 것은 아니다. CPU 영역별 전송량은 업로드 호출당 각각 2,401,920 / 161,280 / 360,000바이트였다. 이번 수정은 이 전송량을 줄이지 않았다.

`Main::drawBgaBackplate`는 `AsyncVideoBga::Frame::pixels`를 aliasing shared_ptr로 presenter에 넘긴다. 이전 renderer owner는 마지막 업로드와 비교하기 위해 계속 살아 있고, decoder는 최신 Frame을 교체한다. 이후 presenter가 owner를 교체하면 마지막 참조가 렌더 스레드에서 사라질 수 있었다.

직접 확인을 위해 진단 빌드에서 `Frame::~Frame`의 vector 해제를 별도 scope로 감쌌다. UI 스레드에서만 프레임 recorder를 사용하고 다른 스레드는 atomic 누계만 갱신한다. 기존 경로 r1의 owner 교체 평균 0.24077ms와 실제 vector 해제 평균 0.23974ms가 일치했다. r2도 0.32491 / 0.32359ms였다. r1의 측정 구간에서 UI 해제된 픽셀 용량은 4,064,256,000바이트(490회)였다. **큰 해제를 호출 경로 밖으로 숨겨 놓고 비용이 없어졌다고 판단한 것이 아니라, 같은 프레임을 재사용해 반복 할당·해제 자체를 줄였다.**

## 최소 수정과 안전성

실제 동작 변경은 `src/AsyncVideoBga.h`의 worker 안에 한정했다.

- worker가 보유하는 fixed array 3슬롯에 strong reference를 유지한다.
- 비어 있거나 `use_count()==1`인 슬롯만 선택한다. 이 1은 해당 worker가 가진 풀 참조다. 다른 strong owner가 있는 프레임의 픽셀/메타데이터를 덮어쓰지 않는다.
- 최종 검토에서 MSVC 14.51의 count 읽기와 `_Decref` 구현도 확인했다. 마지막 consumer의 참조 감소를 관측한 뒤 쓰기를 시작하기 전에 acquire fence를 추가했다. count 읽기만으로 메모리 acquire가 보장된다고 가정하지 않는다. 최종 바이너리의 추가 전후 캡처는 아래에 별도로 제시한다.
- 현재 코드에는 Frame의 weak_ptr 경로가 없다. 이 안전성 전제는 풀 바깥에서 다시 소유권을 얻을 수 있는 weak alias를 추가하지 않는 것이다. 향후 그런 API를 추가한다면 다시 검토해야 한다.
- producer만 풀을 만지므로 풀용 mutex를 추가하지 않았다. 기존 request/latest 게시와 읽기의 mutex/condition_variable은 유지했다. 완성된 프레임만 기존 mutex 아래에 게시하고 const snapshot을 제공한다.
- 슬롯이 모두 사용 중이면 기존 `make_shared<Frame>` 경로를 사용한다. 프레임을 기다리거나 버리거나 오래된 프레임을 일부러 재게시하지 않는다.
- 슬롯별 8MiB, decoder별 최대 24MiB의 픽셀 용량만 풀에 보관한다. 크기 변경 시 독점 소유인 이전 크기 슬롯은 worker에서 해제한다. vector가 예상보다 많이 reserve한 경우에도 풀에서 제외한다.
- 8MiB 초과 프레임은 기존 경로다. **따라서 4K 등 큰 출력은 이번 풀의 성능 개선 대상이 아니다.** 기존 scaler의 256MiB 검증도 유지했다. 여러 decoder 및 외부 보유 프레임의 총 메모리까지 24MiB로 제한한다는 의미는 아니다.
- reset은 기존대로 중단 신호→worker join→최신 프레임 해제를 수행한다. 외부 owner가 보유한 프레임은 reset/open 이후에도 유효하다. pool 종료나 해상도 전환 등에서는 해제가 남을 수 있다. 모든 상황의 UI 해제 0회를 보장하지 않는다.

CopyFromMemory/D3D 리소스·텍스처 생성·DirectComposition·GPU 큐 대기는 수정하지 않았다. CPU 픽셀을 GPU가 직접 참조하는 무복사 구조로 바꾸지 않았다. 이전 코드보다 소스 버퍼 수명이 짧아지지 않으며 GPU 업로드가 사용 중인 프레임은 strong owner 때문에 재사용되지 않는다.

| 검토 후보 | 판단 |
|---|---|
| 미변경 업로드 생략 | 이미 generation 검사 및 행 비교가 있으므로 유지 |
| 전체 이미지 복사 제거 | 부분 직사각형 업로드가 이미 있음. CPU→D2D 복사 자체 삭제 근거 부족 |
| 이중/삼중 버퍼 재사용 | 실제 해제 원인을 확인해 3슬롯 풀만 적용 |
| 안전한 소유권 이전 | 외부 참조가 없는 슬롯만 재사용. 원시 포인터 인수의 owner 보유 유지 |
| 비교 비용 축소 | owner 비교/해시/dirty rect의 추가 변경은 정확성·추가 계산 비용 검증 필요. 이번에는 미적용 |
| 락 경합 변경 | 새 락 없음. 기존 mutex의 경합/드라이버 내부 대기를 정밀 분리하지 못했으므로 임의 변경 없음 |

## 같은 조건의 전후 측정

GTX 1660 Ti, Windows/D3D11/DirectComposition/Hybrid, 1920×1080 보더리스, VSync OFF, Reflex ON, FPS=0(기존 1500 상한), 같은 DefaultReference 이미지 HUD 및 Creamy Sugary OVERDRIVE!!! 채보 30초부터 20초 실행했다. 입력 메시지 리플레이, 오디오 시계는 유지하되 볼륨 0, 컨트롤러/자원 표시/정확도 재시작 OFF다. 각 실행은 격리 INI를 쓴다. 같은 예정 입력이라도 메시지 도착 시점·판정·배경 갱신 수는 조금 달라질 수 있다.

기존 경로는 **진단 타깃에만** 있는 `BMS_PROBE_LEGACY_VIDEO_FRAMES`로 선택한다. 배포 빌드는 이 환경변수를 읽지 않는다. 전→후→후→전 순서, 같은 바이너리·동일 계측을 사용했다. 이전 보고서의 1.051ms 표본을 이번 전후 수치와 섞지 않는다. 모든 시간은 CPU 호출 경과 시간이며 GPU 실행 시간이 아니다.

중앙 2–19초를 집계했다. percentile=floor((N−1)×q), 1% Low=느린 interval 1% 평균의 역수다. uploadHybrid는 **실제 실행된 양수 scope 표본**의 분포이며, 실행하지 않은 프레임의 0을 섞지 않는다. 100ns 수준인 owner 교체는 0으로 기록된 실행도 있어 양수 표본 수가 실제 갱신 수보다 작다. 분석 JSON에는 프레임당 0 포함 분포도 따로 있다. 중첩 scope 시간은 더하지 않는다.

| 실행 | upload 평균 | P95 | P99 | 최악 upload | CPU 평균 | CPU P99 | 최악 CPU | interval P99 | 1% Low |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| r1 전 | 0.1533 | 0.7956 | 0.8973 | 1.1150 | 0.5281 | 1.4517 | 2.7571 | 1.4572 | 620.1 |
| r1 후 | 0.1259 | 0.5452 | 0.6662 | 0.9301 | 0.5192 | 1.2605 | 2.3207 | 1.2697 | 698.2 |
| r2 전 | 0.1685 | 0.8747 | 1.0487 | 1.6678 | 0.5301 | 1.5522 | 3.1943 | 1.5571 | 572.8 |
| r2 후 | 0.1298 | 0.5809 | 0.6637 | 0.8263 | 0.5233 | 1.2699 | 2.8509 | 1.2844 | 694.2 |

단위 시간=ms, 1% Low=FPS. ≥2ms interval은 중앙 17초에 r1 7→2회, r2 18→3회였다. ≥5ms는 모두 0회였다. 짧은 시험에서 tail이 줄었지만 최악값의 장기 확률을 보장하지는 않는다.

| 구간(실행된 양수 scope) | r1 전 평균/P95/P99 | r1 후 평균/P95/P99 | r2 전 평균/P95/P99 | r2 후 평균/P95/P99 |
|---|---|---|---|---|
| 배경 CopyFromMemory | .2926/.4170/.4520 | .3029/.4558/.5554 | .3028/.4360/.5585 | .3138/.4384/.4848 |
| 배경 memcmp | .1247/.1578/.2013 | .1322/.1766/.1996 | .1343/.1794/.2026 | .1510/.1864/.2066 |
| owner 교체 | .24077/.2993/.3275 | .000103/.0001/.0002 | .32491/.4761/.5712 | .000102/.0001/.0002 |
| 실제 UI vector 해제 | .23974/.2975/.3266 | 0회 | .32359/.4752/.5698 | 0회 |

복사·비교 각각이 일관되게 빨라졌다는 증거는 없다. 확인한 개선은 반복적인 큰 해제/할당 감소다. 바뀐 버퍼 위치와 캐시 동작이 영향을 줄 수 있지만 CPU cache miss 카운터를 수집하지 않았으므로 캐시 효율 개선 수치는 주장하지 않는다.

acquire fence까지 포함한 최종 바이너리에서도 전후 각 20초를 추가 실행했다(`hybrid-final-before/after`). 다만 양쪽 모두 unfocused 플래그가 있었으므로 위 foreground 주 비교와 합치지 않는다. 이 별도 배경 실행은 upload 평균 .19421→.17068ms, P95 .9470→.6692ms, P99 1.1727→.8524ms, CPU P99 1.6879→1.4214ms, 1% Low 519.3→614.0FPS였다. 실제 UI vector 해제는 496→0회, capture 전체 픽셀 할당은 528→2회였다. 최종 빌드의 배경 실행·생명주기 동작은 재확인했지만 이 두 실행을 최종 foreground 성능 보장으로 제시하지 않는다. 최종 source/바이너리 hash는 해당 manifest에 별도로 있다.

## BGA 조건·메모리·출력 검증

진단용 정지 이미지/전환은 게임 코드를 바꾸지 않고 **로딩 완료 후 진단 러너의 채보 사본**에만 적용했다. 정지는 기존 startup-splash.png, 전환은 정지→영상→정지→영상(캡처 4/8/12초)이다. 최초 탐색 `hybrid-bga-still/switch`는 로딩 중 재파싱으로 설정이 덮어써졌고, `hybrid-v2-*`는 BMS 소문자 리소스 ID 정규화와 맞지 않아 실제 리소스가 연결되지 않았다. 이 두 탐색 세트는 성능 근거에서 제외했다. 검증 가드와 `bga_fixture_valid` 로그를 추가한 **v3**만 정지/전환 결과로 사용한다.

| 조건 | 실행 길이 / 집계 길이 | upload 평균 전→후 | upload P99 전→후 | CPU P99 전→후 | 1% Low 전→후 |
|---|---|---|---|---|---|
| 정지 이미지(v3) | 12 / 9초 | .05936→.06085 | .0834→.0928 | .7507→.7776 | 983.1→967.0 |
| 정지/영상 전환(v3) | 20 / 17초 | .11918→.09624 | .8868→.5809 | 1.2285→.9994 | 708.5→823.9 |

정지 이미지에서 개선은 없었고 소폭 변동이 있었다. 영상 프레임 할당은 전/후 모두 0회다. 전환에서는 361→2회 할당, 359회 재사용이었다. 이는 모든 BGA 조건에서 동일한 성능 향상을 주장할 수 없다는 대조 근거다.

추가 대조/40초 메모리 실행 수치와 상태는 아래 최종 집계에 기록한다.

| 추가 조건(v4) | 실행 / 집계 길이 | upload 평균 전→후 | P95 전→후 | P99 전→후 | CPU P99 전→후 | 1% Low 전→후 |
|---|---|---|---|---|---|---|
| Reflex OFF·영상 | 12 / 9초 | .16942→.14060 | .8396→.6118 | .9990→.7319 | .8978→.8361 | 573.3→625.3 |
| Reflex ON·BGA OFF | 12 / 9초 | .06942→.06628 | .0935→.0822 | .1265→.0976 | .7135→.6447 | 1000.3→1017.7 |
| Reflex ON·영상 연속 | 40 / 37초 | .17398→.12062 | .8284→.5407 | .9187→.6218 | 1.3618→1.1134 | 661.9→775.9 |

BGA OFF에서는 전후 영상 버퍼 할당이 모두 0회다. 이때의 작은 변동까지 풀의 효과라고 단정하지 않는다. OFF 영상 대조는 P99가 줄었지만 최악 CPU는 2.1209→2.9038ms로 나빠졌다. 드문 모든 지연을 해결했다고 주장하지 않는 이유다.

40초 실행의 Frame/vector 할당은 1127→2회, 재사용은 1128회, overflow는 0회였다. 개선 후 10초 이후 관측한 픽셀 용량은 16,588,800바이트로 일정했고, 이후 추가 픽셀 할당은 없었다. 같은 구간 process private bytes는 개선 전 659.89–673.52MiB, 후 663.09–669.68MiB였다. 개선 후 첫/마지막 private bytes는 652.06/665.78MiB로 초기 증가도 관측됐다. 전체 프로세스 메모리가 전혀 증가하지 않았다고 보고하지 않는다. 프레임 풀에서 매 프레임 증가하는 현상은 이 짧은 실행에서는 없었으며 수시간 누수 검증을 대체하지 못한다.

풀은 메모리를 유지하는 대신 반복 할당을 줄인다. 주 비교 r1/r2 종료 시 기존 경로의 live 픽셀은 7.91MiB, 풀은 23.73MiB였다(약 15.82MiB 추가 보유). 같은 r1 peak working set은 약 584.52→601.47MiB였다. 40초 실행은 실제 2슬롯만 필요해 15.82MiB를 유지했다. 슬롯 3개를 반드시 선할당하는 구조가 아니다.

초기 `hybrid-off-control`, `hybrid-bga-off`, `hybrid-long` 캡처는 unfocused 플래그가 있어 정상 foreground 성능 표에서 제외하고 **v4**로 다시 수집했다. 위 주 비교·v3·v4의 최종 14캡처는 모두 paused/unfocused/auto-resume=0, frame/span dropped=0, GPU fallback=0이었다. 정지/전환 fixture는 별도 리소스 검증 로그도 확인했다. 실제 재현된 모든 탐색 실행이 이 14개 조건과 같았다고 주장하지 않는다.

Release 진단 빌드, 기본 `build-rmlui-song-list/Release/BmsPlayer.exe` 빌드 성공. 기존 배포 스킨과 소스 스킨의 SHA256은 모두 `4d1e54ab2f65d1da5c984338d2457c0fee02a7c0b5cccfe8e4dd31220419d54a`로 유지했다. 다른 이름의 배포 실행파일은 만들지 않았다.

최종 CTest 6/6 통과: BmsLowLatencyTests, BmsFrameTests, BmsVideoFrameScalerTests, BmsVideoTests, BmsCompatibilityTests, BmsCompatibilityPlaybackTests. 추가 검증은 다음과 같다.

- 3슬롯보다 많은 외부 프레임을 보관해 fallback을 강제하고 기존 픽셀/크기를 확인했다.
- 90회 재사용·되감기 후 참조 중인 프레임이 바뀌지 않고 객체 주소가 3개 이내에서 재사용되는지 확인했다.
- 크기 변경, 8MiB 초과 프레임, reset/reopen 이후 외부 owner 유효성을 검사했다.
- 실제 decoder의 재사용 프레임을 uploadHybrid→D2D에 넣고 GPU readback으로 빨강/초록/파랑·되감기를 12회 확인했다. ALPHA_MODE_IGNORE 표면이므로 정의된 RGB를 비교했다. 처음 테스트에서 미정의 alpha까지 비교한 조건은 이를 확인한 뒤 수정했다.
- 기존 CPU/GPU 노트 배치·색상, 실제 판정/오디오 통합 테스트도 통과했다. 해당 게임 시스템의 구현은 수정하지 않았다.

수시간 실행·모든 코덱·다중 레이어에서의 최대 메모리·모든 해상도·수동 플레이 전체를 검증한 것은 아니다. 현재 진단 실행은 고정 span 버퍼 때문에 기본 배포보다 메모리를 많이 사용한다. 측정한 할당 횟수는 **AsyncVideoBga Frame/vector의 용량 증가**이며 전체 엔진/Media Foundation/드라이버 힙 할당 횟수가 아니다.

## ETW와 남은 문제

WPR 수집은 성능 프로파일링 권한 오류 `0xc5585011`로 시작하지 못했다. CPU/heap/driver 스택과 GPU 대기 원인을 ETW로 분석 완료했다고 주장하지 않는다. [관리자 환경에서의 수집 절차](HybridUpload-ETW-Collection.md)를 별도로 작성했다. 직접 destructor 계측으로 UI vector 해제는 확인했지만 allocator 내부에서 virtual memory decommit/락/캐시 무효화가 각각 차지한 비용은 분리하지 못했다.

다음 우선순위는 남은 CopyFromMemory의 CPU 복사/드라이버 대기 구분, 비교 범위의 비용 분석, 4K/여러 동영상 decoder에서 메모리 예산과 큰 프레임의 재사용 정책 검토다. 현재 풀 한도를 무작정 늘리거나 동일 이미지 비교를 없애는 변경은 하지 않았다.

## 변경 파일과 재현

- `src/AsyncVideoBga.h`: 실제 3슬롯 재사용과 진단용 생명주기/할당 누계.
- `src/CompositionPresenter.h`: 배경 업로드 바이트 계측만 추가. 렌더링 알고리즘 변경 없음.
- `src/HudFrameProbe.h`: UI vector 해제 시간·횟수·용량과 업로드 바이트 열 추가.
- `tests/HudFrameProfile.cpp`: 실제/기존 경로 대조, 검증된 BGA fixture, 1초 간격 사전 할당 메모리 표본 및 종료 후 저장.
- `tests/VideoBgaTests.cpp`, `tests/CompatibilityPlaybackTests.cpp`: 수명·fallback·재사용·실제 GPU RGB 출력 회귀 테스트.
- `tools/Run-HudFrameStudy.py`, `tools/AnalyzeHybridUpload.py`: 재현 옵션, SHA256 manifest, 평균/P95/P99/1% Low·메모리·상태 분석.
- 이 보고서와 ETW 수집 문서.

```powershell
cmake --build build-rmlui-song-list --config Release --target BmsHudFrameProfile
python tools/Run-HudFrameStudy.py --label hybrid-new-before --asset original --cases 11 --duration 20 --repeats 1 --fps 0 --reflex --legacy-video-frames
python tools/Run-HudFrameStudy.py --label hybrid-new-after --asset original --cases 11 --duration 20 --repeats 1 --fps 0 --reflex
python tools/AnalyzeHybridUpload.py
```

`--bga off/still/switch`는 별도 label로 실행한다. `foreground_steady`, `bga_workload_verified`, dropped/GPU fallback을 확인한 뒤 성능 표본을 선택한다. CSV는 사전 할당 버퍼에 기록 후 종료 시 저장하며 핫패스 파일 로그는 없다. 원시 결과/바이너리는 Git에 넣지 않았다. 기록 위치는 `build-rmlui-song-list/Validation/HudFrameStudy/`, 대표 prefix는 `hybrid-r1-before/after`, `hybrid-r2-before/after`, `hybrid-v3-still-before/after`, `hybrid-v3-switch-before/after`, `hybrid-v4-off-control-before/after`, `hybrid-v4-bga-off-before/after`, `hybrid-v4-long-before/after`이며 `-original-11-r1`이 이어진다. 모든 source/skin/chart/INI/exe 해시는 manifest에 있다. 기존 사용자 변경이 포함된 작업 트리를 측정했으며 별개 변경은 커밋에서 제외했다.

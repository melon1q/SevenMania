# uploadHybrid ETW 추가 수집 절차

이번 환경에는 WPR/WPA/xperf가 있지만 `wpr -start CPU -start VirtualAllocation -filemode`가 `0xc5585011: Failed to enable the policy to profile system performance`로 실패했다. `wpr -status`는 기록 중이 아니었다. ETL 수집이나 WPA 스택 분석을 완료한 것이 아니다. 레지스트리/시스템 권한을 임의로 변경하지 않았다.

## CPU·대기·가상 메모리

관리자 PowerShell에서 기존 다른 추적이 실행 중이지 않은지 `wpr -status`로 먼저 확인한다. 다른 작업의 추적을 중단하지 않는다. 최적화된 심볼 포함 진단 빌드는 기존 Visual Studio 구성에서 다음처럼 만들 수 있다.

```powershell
cmake --build build-rmlui-song-list --config RelWithDebInfo --target BmsHudFrameProfile
wpr -start CPU -start VirtualAllocation -filemode
python tools/Run-HudFrameStudy.py --label hybrid-etw-before --asset original --cases 11 --duration 20 --repeats 1 --fps 0 --reflex --legacy-video-frames --exe build-rmlui-song-list/HudProbe/RelWithDebInfo/BmsHudFrameProfile.exe
wpr -stop build-rmlui-song-list/Validation/hybrid-etw-before.etl
```

개선 후는 다른 label/ETL 이름으로 `--legacy-video-frames`만 뺀다. 같은 실행파일·심볼을 보존한다. GPU 큐/드라이버 조사가 필요하면 **별도의** 짧은 기록에 WPR GPU 및 DesktopComposition 프로필을 추가한다. 상세 추적은 실행 자체의 프레임 시간을 바꾸므로 평상시 CSV와 성능 수치를 직접 혼합하지 않는다.

WPA에서 해당 프로세스와 UI/decoder 스레드로 필터링한다. CPU Usage(Sampled)의 stack에서 `uploadHybrid → shared_ptr → Frame::~Frame → vector → free/RtlFreeHeap` 경로와 `CopyFromMemory`의 D2D/드라이버 스택을 찾는다. CPU Usage(Precise)/wait 정보가 있는 프로필에서는 context switch·ready time과 드라이버 호출 구간을 비교한다. 기본 CPU 프로필에 필요한 스위치 이벤트가 없으면 WPRUI의 CPU usage/First level triage로 별도 수집한다. 모듈의 실제 PDB와 Microsoft 심볼을 로드하고, 최적화에 의해 인라인된 함수는 상위 주소/소스 행으로 추적한다.

가상 메모리 표에서 1920×1080 BGRA 프레임 크기(8,294,400바이트)의 할당/해제를 확인하되 heap block 크기는 allocator metadata와 다를 수 있다. 현재 구현의 원인 확인은 직접 destructor 계측으로 수행했으며, 그것으로 커널/드라이버 내부까지 분석했다고 판단하지 않는다.

## 개별 힙 할당·해제

실행파일 **시작 전**, 관리자 셸에서 대상 진단 실행파일에만 설정한다. 종료 후 반드시 해당 설정을 해제한다. [Microsoft의 힙 추적 절차](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-for-heap-analysis)에 따른 명령이다.

```powershell
wpr -HeapTracingConfig BmsHudFrameProfile.exe enable
wpr -start Heap -filemode
# 위 진단 명령 중 before 또는 after 한 가지를 실행한다.
wpr -stop build-rmlui-song-list/Validation/hybrid-heap.etl
wpr -HeapTracingConfig BmsHudFrameProfile.exe disable
```

실패한 경우에도 본인이 켠 HeapTracingConfig를 해제한다. `HeapTracingConfig`는 시스템 IFEO 설정을 변경한다. 이번 자동 작업에서는 실행하지 않았다. 힙 추적은 오버헤드가 크므로 10–20초의 원인 확인용 실행으로 제한한다. 세션 시작/종료/설정은 [WPR 공식 명령 설명](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options)을 따른다.

확인할 항목은 UI 스레드의 큰 vector 해제 수, decoder 쪽 frame/vector 할당 수, pool 용량과 재사용, pool 부족/8MiB 초과 fallback, 스레드 ready/wait 및 D2D 업로드 내부다. 개선 후에도 남는 CopyFromMemory/비교 비용을 변경하려면 이 증거와 BGA 출력 검증을 먼저 확보한다.

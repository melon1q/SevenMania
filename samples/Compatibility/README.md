# 호환성 샘플

직접 합성한 음원·이미지·비압축 AVI와 테스트 생성 채보입니다. 실곡, 개인 설정, 플레이 기록은 포함하지 않습니다.

게임에서 **곡 폴더 열기**로 이 `Compatibility` 폴더를 선택하세요. 기능마다 별도 하위 폴더로 나누어 곡 목록에서 서로 묶이지 않게 했습니다. `23-showcase`는 2초 뒤부터 노트가 나오는 종합 시연용입니다. 다른 파일은 경계 조건 검증용이므로 시작 즉시 노트가 나올 수 있습니다.

| 번호 | 확인할 기능 |
|---|---|
| 01 | 정수/소수 마디 길이 |
| 02 | 중첩 RANDOM/SETRANDOM, ELSEIF, SWITCH/CASE/SKIP |
| 03 | 동일 위치 STOP과 STP |
| 04 | 역방향/정지 SCROLL, SPEED 보간 |
| 05 | RANK/EXRANK/TOTAL, Invisible, Mine, TEXT |
| 06–07 | MGQ LNTYPE 2, LN/CN/HCN, 끝점 키음 |
| 08–09 | BASE62, WAVCMD/VOLWAV, 메타데이터 |
| 10 | BMP00 POOR, 블랙 키, 다중 영상, 같은 영상 재시작, EXBMP/ARGB/SWBGA/@BGA |
| 11 | bmson의 BPM/STOP, 음원 슬라이싱, 겹친 키음 |
| 12–16 | 실제 입력 이벤트 및 5/10/14키/PMS |
| 17 | BASEBPM, EXBPM |
| 18–19 | 누락 STOP·잘못된 BPM 객체 복구 진단 — F1으로 확인 |
| 20–22 | PATH_WAV, 강제 OPTION, 여러 LNOBJ |
| 23 | 종합 시연 |
| 24–25 | 구형 시각 옵션 및 알 수 없는 옵션의 F1 진단 |
| 26–27, 29 | 공유 MATERIALSWAV/MATERIALSBMP, 인라인 소재 경로 |
| 28 | EUC-KR/CP949 한국어 제목 |
| 30–31 | BASE16, 불투명도 FF→00 |
| 32 | GTP/GLV 구형 게이지, HSP-MANUAL |

26·27·29번을 실행하려면 **이 폴더의 `Materials` 폴더를 실행파일 옆에 복사**하세요. 공유 소재 명령은 BMS 파일 옆이 아니라 실행파일 옆 `Materials`를 사용합니다. 테스트한 `build-performance-hybrid/Release`에는 이미 테스트 소재가 생성되어 있습니다.

5키는 스크래치+1–5키, 10/14키는 1P 설정 키와 2P `오른쪽 Shift, Q W E R T Y U`, PMS는 `Z S X D C F V G B`를 사용합니다. 10키의 사용하지 않는 레인 키는 표시하지 않습니다.

재생성 방법:

```powershell
cmake --build build-performance-hybrid --config Release --target BmsCompatibilityTests BmsCompatibilityPlaybackTests
ctest --test-dir build-performance-hybrid -C Release -R '^BmsCompatibility' --output-on-failure
python tools/export_compatibility_samples.py build-performance-hybrid/CompatibilitySamples samples/Compatibility
```

자동 검증 범위와 실행기별 차이는 `docs/BMS-Compatibility-Implementation-2026-10-23.md`에 정리했습니다. 모든 상용/실곡 파일의 코덱이나 물리 A/V 지연을 보장하는 샘플 모음은 아닙니다.

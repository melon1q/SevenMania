# 효과음 · 음악(BGM) 폴더 지정 (2026-11-18)

설정 → 소리 → **시스템 사운드**에서 "효과음 폴더"와 "음악(BGM) 폴더"를 고를 수 있다. 값은 `%APPDATA%\BmsPlayer\keybindings.ini`의 `[Sound]`에 `SystemSoundFolder`, `SystemBgmFolder`로 바로 저장되고, 고르는 즉시 다시 불러온다(플레이 · 불러오기 중에는 바꿀 수 없다). "기본 … 사용" 버튼은 경로를 비워 기존 소리로 되돌린다.

## 파일 이름 (beatoraja 기준)

beatoraja `SystemSoundManager.SoundType`과 같은 이름을 쓴다. 출처: [SystemSoundManager.java](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/SystemSoundManager.java), [AudioDriver.java](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/audio/AudioDriver.java).

| 폴더 | 파일 | SevenMania 재생 지점 |
|---|---|---|
| 음악 | `select` | 선곡 화면 배경음, 반복 |
| 음악 | `decide` | 곡 결정 후 진입 |
| 효과음 | `scratch` | 곡 목록 이동 (기존 `songlistscroll` 대체) |
| 효과음 | `f-open` / `f-close` | 폴더 들어가기 / 나오기 |
| 효과음 | `o-open` / `o-close` | LR2 선곡 옵션 패널 열기 / 닫기 |
| 효과음 | `o-change` | LR2 선곡 옵션 값 변경, 키 모드 필터, 정렬 |
| 효과음 | `playready` | 플레이 시작 |
| 효과음 | `playstop` | 일시정지 QUIT으로 플레이 중단 |
| 효과음 | `clear` / `fail` | 결과 화면 진입 (클리어 / 실패) |
| 효과음 | `resultclose` | 결과 화면에서 선곡으로 복귀 |
| 효과음 | `click`, `levelchange` | SevenMania 전용: 버튼 클릭, 난이도 변경 |

- 확장자는 beatoraja `AudioDriver.getPaths`와 같은 순서로 `.wav` → `.flac` → `.ogg` → `.mp3`를 찾는다. 네 형식 모두 `AudioEngine`이 디코딩한다(Media Foundation, ogg는 stb_vorbis).
- 사운드 세트: beatoraja처럼 표식 파일(음악은 `select.*`, 효과음은 `clear.*`)이 있는 폴더를 세트로 본다. 고른 폴더에 표식이 있으면 그 폴더, 없으면 아래 폴더(3단계까지)에서 이름순으로 처음 찾은 세트를 쓴다. beatoraja는 세트를 무작위로 고르지만 SevenMania는 매번 같은 세트를 쓴다. 표식이 없는 폴더도 들어 있는 파일은 사용한다.
- 우선순위: 사용자 폴더(세트) → 기본 스킨 `sounds` 폴더 → `Assets`. 기본 파일이 없는 이벤트(f-open, clear 등)는 사용자 파일이 없으면 무음이다. 즉 폴더를 지정하지 않으면 동작은 이전과 같다.
- 구현: `src/SystemSoundSet.h`(이름표 · 해석 · 설정 저장, 헤더 전용), `reloadSystemSounds()` / `triggerSystemSound()`(`Main.cpp`). 다시 불러올 때는 UI 효과음 출력을 멈춘 뒤 효과음 뱅크를 바꾼다(렌더 스레드가 잠금 없이 읽기 때문).

## 음량 · 미리듣기

- 효과음은 마스터 × 효과음 음량, `decide`도 마스터 × 효과음 음량을 따른다.
- `select`는 마스터 × 선곡 BGM 음량을 따르고 반복한다. 곡 미리듣기가 실제로 나오는 동안에는 선곡 BGM을 5% 이하로 줄였다가(부드럽게 페이드), 미리듣기가 멈추면 원래 음량으로 되돌린다. 재생 위치는 유지한다. 플레이에 들어가면 정지한다. (기존 [선곡 오디오](SelectionAudio-2026-11-07.md) 정책 유지)

## LR2

LR2는 `LR2files/Sound` 아래 사운드 세트 정의 파일로 이벤트별 파일을 지정하는 방식이라 파일 이름이 고정돼 있지 않다. 정의 파일 형식은 공개 자료에서 확인하지 못해 이번에는 읽지 않는다. LR2 세트를 쓰려면 파일 이름을 위 표에 맞춰 바꿔야 한다.

## 아직 연결하지 않은 것

- `course_clear` / `course_fail` / `course_close`: 이름표에는 있지만 코스(단위 인정) 플레이가 엔진에 없어 재생 지점이 없다.
- `guide-pg` … `guide-ms`(판정 가이드 SE): 플레이 중 판정마다 울리는 기능이라 범위가 커서 넣지 않았다.
- 설정 화면의 네이티브 대체 화면(RmlUi 실패 시)에는 폴더 선택 버튼이 없다. ini를 직접 고치면 적용된다.

## 검증

`BmsSystemSoundTests`: 이름표, 확장자 순서, 세트 탐색, 대체 순서, ini 저장/불러오기(한글 · 일본어 경로, 비우기). `BmsAudioTests --selection-policy`: 파일이 없는 폴더를 지정해도 선곡 BGM · decide가 무음으로 남는지. 실제 소리 청취 검증은 하지 않았다.

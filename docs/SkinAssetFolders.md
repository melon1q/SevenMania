# 스킨 에셋 폴더 규격

`.semaskin` 파일과 같은 폴더에 `Notes/`, `Gear/`, `HitEffects/`를 두면 해당 에셋이 스킨 패키지보다 우선합니다.
폴더 에셋은 `.semaskin` 안에 들어가지 않으므로, 배포할 때는 폴더째로 함께 전달해야 합니다.
로드 중 무시된 파일의 이유는 설정 폴더의 `skin-render.log`에 기록됩니다(노트 시트와 히트 이펙트).

기본 스킨은 소스의 `Assets/Skins/DefaultSkin/Notes/NoteGroup.png`(324×360 ModernChic 시트)를 함께 배포합니다. 빌드할 때 `Assets/Skins/DefaultSkin/` 폴더 전체가 실행 파일 옆 `Assets/Skins/DefaultSkin/`으로 복사되므로, 기본 스킨의 노트·기어·히트 이펙트 에셋은 이 소스 폴더에 넣어 두어야 새 빌드 폴더에도 들어갑니다.

## Notes/

노트 하나는 아래 순서로 찾으며, 먼저 있는 것이 이깁니다.

1. 개별 파일 (`normal-white.png` 등)
2. `NoteGroup.png` 시트의 해당 칸
3. 없으면 게임 기본 노트 그림

### 개별 파일 이름

| 용도 | 파일 이름 |
| --- | --- |
| 공통 (색 구분 없음) | `normal.png`, `long-start.png`, `long-body.png`, `long-end.png`, `mine.png` |
| 스크래치 공통 | `scratch.png`, `scratch-long-start.png`, `scratch-long-body.png`, `scratch-long-end.png` |
| 색별 | `{normal\|scratch}{-long-start\|-long-body\|-long-end}-{색}.png` |

색은 키 노트가 `white`/`blue`, 스크래치가 `green`/`red`입니다.
예: `normal-white.png`, `normal-long-body-blue.png`, `scratch-long-start-green.png`.
PNG 한 변은 최대 1024px이며, 깨진 파일은 무시되고 다음 순위로 넘어갑니다.

### NoteGroup.png

`Notes/NoteGroup.ini`가 **없으면** 324×360 ModernChic 배치로 읽습니다. 크기가 다르면 시트를 무시하고 로그에 남깁니다.

기본 배치: 열은 `scratch-green`(x0, 폭108), `scratch-red`(x108, 폭108), `normal-white`(x216, 폭60), `normal-blue`(x276, 폭48).
부품 행은 `Tap`=0, `LongEnd`=36, `LongStart`=72, `LongBody`=144이고 칸 높이는 36입니다.

`NoteGroup.ini`가 **있으면** 그 선언만 따릅니다.

```ini
[Sheet]
Width=100        ; 생략하거나 0이면 시트 크기를 검사하지 않음
Height=72
CellHeight=36
[Parts]          ; 각 부품의 y 좌표 (생략 시 기본값)
Tap=0
LongStart=36
LongBody=0
LongEnd=36
LongBodyFrames=3 ; 몸통 프레임 수 (생략하면 1 = 정지)
LongBodyFps=12   ; 초당 프레임 수 (1~120, 생략하면 12)
[normal-white]   ; 선언하지 않은 열은 꺼짐
X=10
Width=20
[scratch-green]
X=0
Width=10
```

열은 `scratch-green`, `scratch-red`, `normal-white`, `normal-blue` 네 개로 고정이고, 칸이 시트 밖이면 그 칸만 건너뛰며 로그에 남깁니다.

### 노트 크기

노트 높이는 36px로 고정이며(설정 없음) 시트 칸 높이와 같습니다. 기본 레인 폭도 시트 칸 폭과 같아서 노트가 원본 비율로 그려집니다: 스크래치 108, 흰 건반 60, 파란 건반 48(2·4·6번). 그에 맞춰 기본 스킨의 7K 기어를 다시 배치했고(`tools/retarget-7k-gear.py`), 다른 키 모드는 `tools/generate-gear-modes.py`로 다시 만듭니다.

### 이름 붙은 시트 (`000-NoteGroup.png`)

`Notes/` 안에 `<이름>-NoteGroup.png`를 여러 개 둘 수 있습니다(예: `000-NoteGroup.png`, `001-NoteGroup.png`). 이름은 파일 이름에서 `-NoteGroup`을 뺀 부분이고, 런처의 **노트 이미지** 목록에 그 이름만 표시됩니다. 선택한 이름은 설정 파일의 `[Skin] NoteGroup`에 저장됩니다.

- 레이아웃 파일도 같은 이름으로 짝을 짓습니다: `000-NoteGroup.ini`. 시트마다 따로 선언합니다.
- "자동 / 기본"이면 `NoteGroup.png`를 쓰고, 그것이 없으면 이름순 첫 번째 시트를 씁니다.
- 선택한 이름의 파일이 없으면 로그에 남기고 자동 선택으로 돌아갑니다.
- 이름은 대소문자를 구분하지 않고 이름순(`000`이 `001`보다 앞)으로 정렬됩니다. 이름에 하이픈이 더 있으면 그대로 표시됩니다(`Neon-Blue-NoteGroup.png` → `Neon-Blue`).
- 개별 파일(`normal-white.png` 등)은 어느 시트를 고르든 시트보다 우선합니다.

### 롱노트 몸통 애니메이션

몸통 칸(`LongBody`)에서 **아래쪽으로 칸 높이만큼씩** 이어진 칸들이 애니메이션 프레임입니다(최대 64). 재생 위치는 곡 재생 시간으로 정해지므로 일시정지하면 멈춥니다.

- `NoteGroup.ini`가 없으면 기본 ModernChic 시트의 평소 몸통은 y108 한 칸(비활성)이고, 누르고 있는 동안만 y144·y180이 번갈아 나옵니다(아래 "누르고 있는 롱노트").
- `NoteGroup.ini`가 있으면 `LongBodyFrames`를 적은 경우에만 애니메이션합니다(생략 시 1프레임).
- 프레임이 시트 밖으로 나가면 거기까지만 쓰고 로그에 남깁니다.
- 개별 파일(`*-long-body-*.png`)은 항상 한 장입니다.

### 누르고 있는 롱노트 (held)

롱노트를 누르고 있는 동안에는 머리·몸통·꼬리가 눌린 상태의 이미지로 바뀌고, 몸통은 채워진 칸 둘이 반복해서 바뀝니다. 눌린 이미지가 없으면 일반 롱노트 이미지를 그대로 씁니다.

- 기본 ModernChic 시트: 평소 몸통은 비어 있는 맨 위 칸(y108, 움직이지 않음)이고, 누르고 있는 동안은 주황색으로 채워진 y144·y180 두 칸이 12fps로 번갈아 나옵니다. 머리·꼬리는 평소와 같은 장식을 씁니다. 시트의 y216 아래쪽 블록은 읽지 않습니다.
- `NoteGroup.ini`의 `[Parts]`에 `HeldLongStart`, `HeldLongBody`, `HeldLongEnd`(y 좌표), `HeldLongBodyFrames`(기본 1), `HeldLongBodyFps`(기본 12)를 적습니다. 적지 않은 눌린 칸은 꺼지고 일반 이미지를 씁니다.
- 개별 파일: `held-long-start.png`, `held-long-body.png`, `held-long-end.png`, 스크래치는 `scratch-held-long-*.png`, 색별은 `normal-held-long-body-white.png`처럼 색을 끝에 붙입니다. 개별 파일은 몸통도 한 장입니다.

### 아직 쓰지 않는 부분

시트의 지뢰 칸은 읽지 않습니다(지뢰는 `mine.png` 개별 파일만).

## Gear/

`Gear/Buttons-Black/`, `Gear/Buttons-White/`, `Gear/Scratch/` 폴더가 각각 검은 건반, 흰 건반, 턴테이블 이미지를 대체합니다.

- 폴더 안의 유효한 이미지 중 **파일 이름 순(대소문자 무시) 첫 번째**를 씁니다. 이전에는 이미지가 둘 이상이면 그 부품 전체가 무시됐습니다. 아직 여러 장 중에서 고르는 UI는 없습니다.
- 확장자와 상관없이 내용으로 PNG/JPG를 판별하며, 16MB 초과나 읽을 수 없는 파일은 건너뜁니다.
- 스킨 패키지의 play 화면에 `nativeGearPart` 요소가 있고 리소스가 `black-key`/`white-key`/`disc`일 때만 교체됩니다. 그런 요소가 없는 스킨에서는 폴더가 있어도 반영되지 않으며, 이때 `skin-render.log`에 어느 폴더가 무시됐는지 기록합니다.

## HitEffects/

`1.png`, `2.png`, … 처럼 **앞에 0이 없는 숫자 이름**의 PNG만 인식합니다(`01.png`, `a.png`는 무시). 번호 순으로 정렬되며(`2`가 `10`보다 앞), 선택한 번호는 설정 파일의 `[Skin] HitEffect`에 저장됩니다.

- 프레임은 가로로 나란히 놓으며 기본은 **6프레임**입니다. 다른 프레임 수는 같은 번호의 `.ini`로 선언합니다(1~32). 이미지 폭이 프레임 수의 배수가 아니면 무시됩니다.

  ```ini
  ; HitEffects/3.ini  (3.png가 8프레임일 때)
  [HitEffect]
  Frames=8
  ```
- 프레임 수와 상관없이 재생 시간은 같습니다(0.18초 동안 모든 프레임을 지나감).
- 무시된 파일(이름 규칙 위반, 번호 중복, 디코딩 실패, 프레임 수 오류, 폭이 배수가 아님)은 이유와 함께 `skin-render.log`에 기록됩니다.
- 저장된 번호가 없으면 가장 작은 번호를 씁니다. 폴더에 유효한 이펙트가 없으면 패키지의 이펙트, 그것도 없으면 내장 기본값을 씁니다. 패키지·내장 기본 이펙트는 항상 6프레임입니다.
- 재생 시간 조절, 판정별·레인별 이펙트는 아직 없습니다.

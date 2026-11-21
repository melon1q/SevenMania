# LR2 7KEY 스킨 파서: Phase 1

`sevenmania::skin::lr2::parse()`는 `.lr2skin` 헤더와 활성 INCLUDE 파일을 읽고
`Result`와 `Document`를 반환한다. 이 문서는 파서 규칙만 다룬다. 게임 화면 연결과
화면별 지원 범위는 [Lr2SkinCompatibility.md](Lr2SkinCompatibility.md)에 있다.
파일을 열거나 파싱하지 못하면 문서 대신 경로·줄 번호가 포함된 진단을 반환한다.

- 본문·INCLUDE는 Windows 코드 페이지 932로 엄격하게 디코딩한다. 명령 이름은
  대소문자를 구분하지 않으며, 각 명령의 필드에는 원래의 명령 이름과 마지막
  빈 필드를 포함한다. 문자열 필드는 UTF-8이다.
- 헤더는 `INFORMATION`(7KEY type 0), `RESOLUTION`, `CUSTOMOPTION`,
  `CUSTOMFILE`, `CUSTOMOFFSET`을 구조화한다. 해상도가 생략되면 SD다.
- `Document.commands`에는 원래 선언 순서, 파일, 줄, 필드와 분기 활성 상태가
  담긴다. 활성 INCLUDE는 그 위치에 확장하고 비활성 INCLUDE는 열지 않는다.
  `IF` 등 제어 명령도 보존한다. 읽지 않은 분기의 외부 파일은 수집하지 않는다.
- 옵션 그룹은 기본으로 첫 선택지를 사용한다. `Options.initialOptions`의
  명시된 값이 우선하고, 그룹 안에 참으로 지정된 선택지가 있으면 첫 선택지를
  추가하지 않는다. 선택되지 않은 ID의 거짓을 임의로 가정하지 않는다.
  `Options.choices`는 사용자가 이름으로 고른 값이다. 옵션 선택지는 그 묶음의
  모든 ID를 확정하며 `initialOptions`보다 우선하고, 파일 선택은 와일드카드
  해석에, 오프셋 값은 `Document.offsetValues`에 반영된다. 스킨에 없는 이름은
  무시한다.
  `Options.condition`은 게임 상태 조건을 읽기 전용으로 제공할 수 있다.
  알 수 없는 조건은 거짓으로 처리하고 경고한다.
- `IF`, `ELSEIF`, `ELSE`, `ENDIF`, `SETOPTION`, `INCLUDE`를 처리한다.
  조건 상태는 LR2·참조 구현처럼 파일마다 하나뿐이다. IF 안의 IF는 중첩되지
  않고 열려 있던 IF를 대체하며, 첫 ENDIF가 전체를 닫는다. 짝이 맞지 않는
  ELSEIF/ELSE/ENDIF, 닫지 않은 IF, 읽을 수 없는 조건 ID(거짓으로 처리),
  잘못된 SETOPTION은 경고만 남기고 계속 읽는다. INCLUDE된 파일은 자기 조건
  상태를 따로 가지며 바깥 파일의 IF에 영향을 주지 않는다.
- INCLUDE와 IMAGE 경로는 파일명에 `*` 하나를 쓸 수 있다. 같은 패턴의
  CUSTOMFILE이 있으면 사용자가 고른 파일명, 그다음 기본 파일명을 쓰고, 없거나 그 파일이 없으면 대소문자
  구분 없는 이름순 첫 매치를 쓴다(LR2는 무작위). 폴더 부분의 `*`와 `*` 두 개는
  오류다. 누락 INCLUDE는 `previewIncludes`가 꺼져 있을 때만 오류다.
- 경로는 입력의 `LR2files` 상위 폴더 또는 명시적으로 지정한 `Options.root`
  안에서만 해석한다. `LR2files` 상위가 없으면(테마 폴더만 따로 복사한 경우)
  스킨이 적은 `LR2files\Theme\<이름>\...` 경로 중 하나가 실제로 존재하는 가장
  가까운 상위 폴더(최대 4단계)를 테마 폴더로 보고, 그 폴더가 루트이자
  `LR2files\Theme\<이름>`의 자리가 된다. `LR2files\WallPaper` 같은 테마 밖
  경로는 이 경우 찾지 못한다. Windows 절대·드라이브 경로를 차단하고 정규화된 실제
  경로를 확인해 이탈을 막는다. 순환 및 깊이·파일 크기·총 크기·명령 수,
  행 길이·필드 수 제한이 있다. 파서가 여는 파일은 INCLUDE뿐이다.
- 이미지, 노트, 판정, BGA, 숫자 등 렌더링 명령은 파서가 실행하지 않는다. 활성 미지원 명령은 원본 필드를 보존하고 종류별로 경고한다.
  나중 단계에서 사용할 수 있도록 경고를 오류로 바꾸지 않는다.

참조한 행 분리·조건과 헤더 규칙은 각각 beatoraja의
[`LR2SkinLoader`](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/skin/lr2/LR2SkinLoader.java),
[`LR2SkinHeaderLoader`](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/skin/lr2/LR2SkinHeaderLoader.java),
[`LR2SkinCSVLoader`](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/skin/lr2/LR2SkinCSVLoader.java)를
기준으로 한다. 안전을 위해 INCLUDE 순환·경로 이탈·크기 제한 초과는 명확한
오류로 처리하고, 조건문 문법 오류는 참조 구현처럼 경고로 넘긴다. 모든 LR2 스킨과의 완전 호환을 의미하지 않는다.

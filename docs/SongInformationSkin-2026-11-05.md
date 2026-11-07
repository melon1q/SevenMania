# 곡 상세 정보 독립 스킨 화면 (2026-11-05)

곡 상세 정보 패널을 `select`와 독립된 `songinformation` 화면으로 추가했다. 스킨 에디터의 화면 선택 목록에서 `songinformation`을 선택하면 기존 요소 편집 기능으로 박스, 이미지, 텍스트, 숫자의 위치·크기·색·폰트·레이어를 수정할 수 있다. 저장 및 내보내기 시 `screens/songinformation.json`으로 저장된다.

## 데이터와 동작

- 공통 바인딩: `song.title`, `song.artist`, `song.genre`, `song.bpm`, `chart.level`, `chart.levelText` (실제 상세 정보 공급은 `chart.levelText` 사용).
- 새 바인딩: `songinformation.normalNotes` (정수), `songinformation.longNotes` (정수), `songinformation.duration` (초, 실수).
- 곡 정보 버튼을 누를 때 기존 채보 읽기 경로에서 표시용 스냅샷을 만든다. 매 프레임 채보를 다시 분석하지 않는다.
- 게임은 기존 select 화면과 곡 목록을 유지한 채 정보 화면을 합성한다. 기존 Esc/클릭 닫기를 유지한다.
- 기본 스킨에 독립 패널과 제목·작곡가·장르·BPM·노트 수·길이 요소를 추가했다. 배포 폴더의 기본 스킨에도 적용했다.
- 기존 스킨에 이 화면이 없으면 게임에서는 기존 정보 박스를 사용한다. 에디터로 열면 최소 정보 템플릿을 추가하며, 저장한 뒤부터 독립 화면을 사용한다.
- 새 빈 스킨에는 편집용 빈 songinformation 화면이 포함된다. 비어 있는 화면은 제작자가 요소를 추가해야 한다.

## 변경 파일

`src/skin/SkinPackage.h`, `SkinLoader.cpp`: 선택적 네 번째 화면 인식 및 검증.
`src/skin/SkinData.h`, `SkinData.cpp`: 정보 바인딩과 화면 범위.
`src/skin/SkinApplication.cpp`: 네 번째 화면 렌더러 저장 공간.
`src/editor/EditorDocument.h`, `SkinEditor.cpp`: 화면 선택, 저장/재열기, 구형 문서 확장, 미리보기.
`src/Main.cpp`: 정보 스냅샷 및 합성 연결 (이번 변경 +7줄).
`tests/SkinBindingTests.cpp`: 데이터 범위, 실제 정보 개수 및 합성 렌더링 확인.
`Assets/Skins/DefaultReference.semaskin`: 기본 정보 화면.

## 검증

Release의 BmsPlayer.exe, SevenMania Skin Editor.exe 및 관련 테스트를 빌드했다.
최종 자동 테스트: SevenManiaSkinTests, SevenManiaSkinBindingTests, SevenManiaSkinEditorSmoke 모두 통과 (3/3, 2.66초).
바인딩 테스트에서 실제 정보 열기 경로의 일반 노트 2개/롱노트 1개를 확인하고, 정보 패널을 렌더링해 배경 픽셀을 검증했다. 에디터 테스트에서 정보 화면 미리보기, 저장 및 재열기 후 JSON 보존을 확인했다.
추가 SevenManiaSkinApplicationTests는 `HUD RML settings stale`에서 실패했다. 해당 검사는 앞선 작업에서 제거된 skin-judgement-mode / skin-combo-mode 설정을 여전히 요구한다. 이번 기능 검증과 구분하며 해당 테스트를 완화하지 않았다.
수동 게임 조작 및 여러 해상도에서의 시각 검증은 수행하지 않았다. 정적 select와 정보 패널은 기존 공통 합성 표면을 번갈아 사용하므로, 정보 패널 표시 중 고FPS 성능은 별도 측정하지 않았다.

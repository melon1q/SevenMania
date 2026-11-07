# SevenMania 스킨 에디터 MVP — 초기 구현

실행: build-rmlui-song-list/Release/SevenMania Skin Editor.exe

별도 Win32 실행 파일이다. 본체 게임 로직은 변경하지 않았다. 공통 SkinLoader/SkinRenderer/SkinDataProvider/BindingRegistry를 사용한다. EditorDocument는 기존 JSON DOM을 그대로 보유하고 직렬화하므로 extensions, source, sprite 등 속성 패널에 없는 기존 필드도 유지한다. 로더가 모르는 정규 필드는 가져오기 단계에서 오류로 거부한다.

## 현재 기능
- 새 스킨, 패키지 가져오기/저장/다른 이름 저장/내보내기
- 압축되지 않은 프로젝트 폴더 열기/저장
- PNG/JPEG 추가, 고정 텍스트 및 숫자 추가
- select/play/result 전환, 실제 레지스트리의 바인딩 선택과 미리보기 전용 Mock Data
- 레이어 선택, 숨김, 잠금, 복제, 삭제, 이름(ID), 순서 변경
- 위치·크기·회전·불투명도·RGB/alpha Tint·z·resource·text·binding·font·정렬·숫자 서식 속성 입력
- 속성을 수정한 뒤 ‘속성 적용’을 누르면 검증 후 반영
- 드래그 이동, 오른쪽 아래 핸들 크기 조절, 휠 확대/축소, 50/100/200% 버튼, 중간 버튼 Pan, 중앙 맞춤
- 회전 역변환을 이용한 요소 선택, 선택 사각형, 격자 표시
- 최대 60개 문서 스냅샷 Undo/Redo. 이미지 바이트는 불변 공유. 드래그 한 번을 이력 한 번으로 기록
- 수정 표시와 종료/다른 문서 열기 전 버리기 확인

anchor 값은 top-left / center / bottom-right를 지원한다. 속성 X/Y를 해당 기준점 좌표로 표시하고 적용 시 엔진의 절대 좌상단 좌표로 환산한다. 기준점 정보는 extensions.editorAnchor, 잠금 정보는 extensions.editorLocked에 저장한다. 게임 렌더러의 회전 중심 규칙은 바꾸지 않는다.

## 저장
패키지는 Stored ZIP32로 임시 .pending.semaskin에 작성하고 실제 SkinLoader 검증 후 교체한다. 실패 시 원본을 교체하지 않는다. 프로젝트 폴더는 형제 pending 폴더에 완성한 다음 기존 폴더를 고유 editor-backup 이름으로 보존하고 교체한다. 실패한 pending 폴더는 점검용으로 남을 수 있다. 백업은 자동 삭제하지 않는다. 폴더 가져오기는 링크/재분석 경로와 과대 파일을 거부한다.

## 미리보기
공통 SkinRenderer를 Direct2D DC target에 연결하고 캔버스 배율로 출력한다. 드래그 중에는 editGeometry로 좌표만 바꾸므로 이미지 재디코딩/패키지 I/O를 하지 않는다. 다른 속성 변경은 명시적 적용 때 다시 검증·준비한다. 이는 아직 변경 종류별 최적화가 완성된 구현은 아니다.

## 검증 결과와 제한
에디터와 기본 BmsPlayer 빌드 성공. SevenManiaSkinEditorSmoke에서 새 문서/텍스트 추가/렌더러 준비/좌표 갱신/패키지 저장·재로드/폴더 저장·재로드 확인. 기존 스킨 테스트 4종도 통과했다.
마우스 이동·크기 조절·회전 선택·색상 변경·Undo/Redo 전 조합, 실제 게임 픽셀과의 비교는 자동 검증하지 않았다. 이 기능들이 모두 수동 테스트 완료되었다고 주장하지 않는다.

현재 창 배치는 고정 크기이며 고해상도/DPI별 반응형 배치와 속성의 종류별 숨김은 미완성이다. 앵커는 세 종류만 지원한다. 스프라이트 시트/이미지 source/clip의 전용 편집 UI, 메타데이터 편집 UI, 고급 앵커, 속성 입력 즉시 자동 적용은 없다. 기존 필드는 보존한다. 전체 요구사항을 모두 완료한 정식 3.5차 버전은 아니다.
다음 단계는 편집 모델을 GUI에서 분리하고 좌표/이력/저장 실패 회귀 테스트를 확장한 뒤 UI 레이아웃과 편집 최적화를 마무리하는 것이다. 4차 애니메이션 데이터는 공통 모델 extensions에 보존할 수 있으나 타임라인 실행기는 구현하지 않았다.

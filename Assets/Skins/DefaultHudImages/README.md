# 기본 판정·콤보 이미지

`judgement/`의 판정 6종과 `combo/`의 숫자 0–9를 기본
`DefaultReference.semaskin` 안에 포함한다. PGREAT 파일은 기존 표기처럼
무지개색 `GREAT`이며, 시간에 따른 반짝임/색상 변경은 없다.

Oxanium Regular로 만든 투명 PNG다. 글꼴 라이선스는
`Assets/Fonts/Licenses/Oxanium-OFL.txt`에 있으며 스킨에도 동봉한다.

재생성: `python tools/UpdateDefaultHudImages.py`

이 명령은 현재 기본 패키지의 판정·콤보 설정과 해당 PNG만 갱신한다.
곡 선택·기어·리절트 배치는 재생성하지 않는다. 제작 도구에만 Pillow가
필요하며, 빌드와 게임 실행에는 Python/Pillow가 필요하지 않다.

빌드 시 게임과 에디터의 `Assets/Skins/DefaultSkin/DefaultReference.semaskin`을 동기화한다.
PNG는 패키지 안에서 읽으므로 배포 폴더에 이 원본 디렉터리를 별도로 넣을 필요는 없다.

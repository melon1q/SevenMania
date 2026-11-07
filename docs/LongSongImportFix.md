# 긴 곡 폴더 가져오기 수정

2026-10-26

## 원인과 수정

BOFU2017 ha@ppy s@mmer v@c@tion!! 원본 폴더의 세 BMS는 직접 분석하면 모두 정상이다. 그러나 같은 긴 이름의 폴더가 두 겹이며, Songs 저장 경로에 가져오기 해시와 `.importing-…`, `.copying-…` 임시 이름이 추가되면 Windows의 기존 MAX_PATH 제한을 넘는다. 수정 전 실제 폴더 가져오기에서 중첩 폴더 생성 실패를 재현했다.

`src/long-paths.manifest`의 `longPathAware=true`를 모든 실행 파일에 병합한다. 플레이어와 테스트가 동일하게 긴 경로를 사용한다. 원본 폴더 이름이나 곡 데이터는 변경하지 않는다. 이 방식은 Windows의 LongPathsEnabled 정책이 활성화된 환경에서 동작하며, 확인한 사용자 PC의 값은 1이다. 시스템 설정은 변경하지 않았다.

## 검증

- 수정 전 실패했던 실제 곡 폴더 가져오기 성공. Another 1,863개, Normal 638개, Hyper 1,100개 노트와 각각 962/962/961개 음원 참조를 읽고 세 채보 모두 playable 확인.
- `BmsLibraryTests --verify-import <source> <destination>`으로 실제 복사 후 검색을 검증할 수 있다. 테스트 산출물은 빌드 폴더 아래에만 저장했다.
- 자동 회귀 테스트는 MAX_PATH보다 긴 한글·일본어 중첩 폴더에서 복사, 채보/음원 경로 해석, 변경 없는 재가져오기를 확인한다.
- Release 플레이어 빌드 및 BmsCoreTests/BmsLibraryTests 통과.

이 검증에서 실제 음원을 재생하거나 곡 전체를 플레이하지는 않았다.

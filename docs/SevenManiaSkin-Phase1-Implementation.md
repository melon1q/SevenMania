# SevenMania 스킨 시스템 1차 구현 보고

## 결과와 범위

`.semaskin` 규격·메모리 ZIP 로더·검증·공통 모델·등록/화면별 선택 API를 구현했다. 실제 스킨 디자인, GPU 적용, 설치/선택 UI는 구현하지 않았다. 기존 엔진의 화면 처리에 로더 호출을 삽입하지 않았으며 기존 렌더링·BMS 파서·오디오·판정·입력 코드를 수정하지 않았다.

규격과 사용 예는 [v1 규격](SevenManiaSkinFormat-v1.md), 사전 조사와 경계는 [설계 기록](SevenManiaSkin-Design.md)에 정리했다. 프로젝트 공식 명칭은 SevenMania로 사용하되 기본 실행 파일 경로는 기존 작업 규칙에 따라 `build-rmlui-song-list/Release/BmsPlayer.exe`를 유지한다.

## 파일

| 추가 파일 | 책임 |
| --- | --- |
| `src/skin/SkinPackage.h` | 공개 모델, 제한, 오류, Loader/Manager API |
| `src/skin/SkinInternal.h` | 비공개 계층 사이의 함수와 오류 전달 |
| `src/skin/SkinZip.cpp` | ZIP32 목록·헤더·범위·CRC·압축률·경로 검사와 메모리 해제 |
| `src/skin/SkinInflate.c` | 기존 FreeType 번들 zlib의 독립 번역 단위 어댑터 |
| `src/skin/SkinJson.cpp` | 기존 JSON 파서 앞뒤의 엄격한 UTF-8/키/예산 검사 |
| `src/skin/SkinImage.cpp` | PNG/JPEG 크기·컨테이너 및 폰트 테이블 메타데이터 검사 |
| `src/skin/SkinLoader.cpp` | manifest/화면 파싱과 컨테이너 독립 검증·불변 스냅샷 |
| `src/skin/SkinManager.cpp` | 등록, 화면별 선택, 실패 시 상태 유지, 등록 해제 |
| `cmake/SkinSystem.cmake` | UI/게임 모듈과 분리한 라이브러리 및 테스트 타깃 |
| `tests/SkinTests.cpp` | 정상·오류·제한·수명·변환 모델 검증 |
| `tests/fixtures/skins/loader-result.semaskin` | 결정적 ZIP Deflate로 만든 1픽셀 결과 화면 샘플 |
| `tests/fixtures/skins/pixel.jpg` | 2×2 JPEG 메타데이터 테스트 자료 |
| `docs/licenses/Zlib.txt` | 재사용한 압축 코드의 라이선스 |
| 설계·v1 규격·이 문서 | 구현 계약과 결과 기록 |

수정한 기존 파일은 `CMakeLists.txt`(타깃 연결·라이선스 복사), `tools/Package.ps1`(라이선스 포함)이다. 작업 시작 전부터 있던 Main.cpp·곡 선택 코드·UI·Rust 등의 다른 수정은 이번 커밋에서 제외한다.

## 지원 기능과 확장점

- 부분/전체 화면 패키지, Stored/Deflate와 데이터 디스크립터, 오류의 코드·파일 경로·메시지 반환
- 포맷 버전과 제작물 SemVer 분리, 필수/선택 자원 및 화면/요소 참조 검증
- 이미지/텍스트/숫자/스프라이트 요소, 레이어 순서, 바인딩 이름과 확장 선언 데이터 보관
- 자원 바이트 공유, 불변 로딩 결과, 실패한 로드/선택의 기존 상태 유지, 보유 owner의 안전한 수명
- `prepare()`/`registerModel()`을 통한 같은 내부 모델의 직접 검증. 미래 LR2 변환기가 ZIP을 만들 필요 없이 이 경로를 사용 가능

새로 다운로드한 라이브러리는 없다. 기존 FreeType 서브모듈에 번들된 zlib 소스를 수정 없이 재사용하고 심볼 접두사를 분리했다. Windows WIC를 사용하며 영상 디코더를 추가하지 않았다. 독립 모듈은 RmlUi를 끈 구성에서도 빌드·검증했다.

## 테스트와 한계

스킨 테스트는 76개 검증 항목을 통과했다. 이 안에는 정상 Stored/Deflate/디스크립터, 누락 manifest/화면/이미지, 잘못된 JSON·중복 키·유니코드·버전, JPEG 메타데이터, 폰트 테이블 범위, 부분/전체 화면, 중복/위험 경로·심볼릭 링크·암호화·ZIP64·CRC·잘못된 ZIP, 압축 폭탄·허위 출력 크기·크기/깊이/노드/요소/이미지 예산 제한, 가변 바이트 별칭 차단, 실패 시 선택 보존 등이 있다. 100회 로딩/해제의 소유권 해제와 300개 결정적 변형 ZIP 입력도 검사했다.

Release의 `BmsPlayer` 기본 빌드와 `SevenManiaSkinTests` 빌드가 성공했다. 기본 실행 파일도 갱신했다. RmlUi 전체 OFF 구성에서도 독립 스킨 라이브러리와 전용 테스트를 빌드해 76개 검사를 통과했다.

| 새로 빌드한 검사 대상 | 결과 |
| --- | --- |
| SevenManiaSkinTests | 통과 |
| BmsCoreTests | 통과 |
| BmsDisplayTests | 통과 |
| BmsRmlSongListTests / BmsRmlSongListHostTests | 통과 |
| BmsSettingsPhase5Tests | 통과 |
| BmsSongSelectionTests | 아래의 기존 동작/테스트 기대 불일치로 실패 |

PNG/JPEG의 전체 픽셀 디코딩·폰트 렌더링·GPU 업로드를 검증한 것은 아니다. 임의 스킨에 대한 완전한 보안 증명, sanitizer 검사, 장시간 실제 플레이·GPU 캐시 수명 검증도 수행하지 않았다. 300개 변형 입력 검사는 제한적인 손상 입력 검사다.

기존 회귀 검사에서는 파서, 화면 표시, 곡 목록 렌더러/호스트, 설정/플레이 미리보기 검사가 통과했다. `BmsSongSelectionTests`는 `variant selection should wrap forward`에서 실패했다. 작업 시작 전부터 있던 C++/Rust 곡 선택 변경이 끝에서 순환하는 대신 멈추도록 바뀌었고, 테스트는 순환을 기대한다. 해당 테스트 타깃은 새 스킨 라이브러리를 링크하지 않으며 이번 작업에서는 그 코드·기대를 수정하지 않았다. 따라서 ‘모든 기존 테스트 통과’로 보고하지 않는다.

## 다음 단계

1. 기존 기본 화면을 유지하는 상태에서 스킨 설치 경로·목록·선택 저장·교체 정책 연결
2. 이미지 실제 디코딩 오류 처리, 제한된 CPU/GPU 캐시, 기존 렌더러에 작은 요소부터 연결
3. 공통 바인딩 이름과 화면별 fallback 규칙 확정 후 부분 스킨 적용
4. 같은 IR을 출력하는 LR2 변환기의 별도 설계

Core/일반 디자인, 완전한 애니메이션 엔진, 스킨 에디터, LR2 변환기, 기존 화면 교체, 영상 재생은 이번 단계에 포함하지 않았다.

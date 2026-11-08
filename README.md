# SevenMania

Windows 전용 BMS 리듬 게임 플레이어입니다. C++20, Win32, Direct3D 11, WASAPI로 만들었습니다.

## 주요 기능

- BMS / BME / BML / PMS / bmson 채보 재생 (5·7·9·10·14키)
- 곡 폴더 자동 검색과 선곡 화면 (그룹·정렬·난이도 목록)
- 스킨(`.semaskin`) 적용과 스킨 에디터
- 연습 모드, 컨트롤러(IIDX 형태) 입력
- 소리 보정: 오디오 레이턴시, 노트 표시 타이밍, 판정 타이밍(자동 보정 포함)
- WASAPI 공유/독점 출력, 선곡 BGM·미리듣기·키음·효과음 볼륨 설정

## 실행

[Releases](../../releases)에서 ZIP을 받아 압축을 풀고 `SevenManiaLauncher.exe`를 실행합니다.
`Songs` 폴더에 곡을 넣거나 게임 안에서 곡 폴더를 지정하면 됩니다. 설정은 `%APPDATA%\BmsPlayer\keybindings.ini`에 저장됩니다.

## 빌드

필요한 것: Visual Studio 2026 (Desktop development with C++), Windows 10/11 SDK, CMake 3.24 이상, Git.
기본 빌드는 선곡 로직에 Rust 정적 라이브러리를 쓰므로 [rustup](https://rustup.rs/)도 필요합니다 (버전은 `rust-toolchain.toml`에 고정).

```powershell
git clone --recurse-submodules https://github.com/melon1q/SevenMania.git
cd SevenMania
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target BmsPlayer --parallel 4
```

결과물은 `build/Release/`에 생성됩니다 (`BmsPlayer.exe`가 `SevenMania.exe`로 복사됨). 런처는 `--target SevenManiaLauncher`로 따로 빌드합니다.
Debug 빌드는 Rust 라이브러리 대신 C++ 선곡 구현을 사용합니다.

## 테스트

테스트는 개별 실행 파일이라 먼저 빌드해야 합니다.

```powershell
cmake --build build --config Release --target BmsJudgementTests
ctest --test-dir build -C Release -R BmsJudgementTests --output-on-failure
```

## 구조

- `src/` 게임 본체. `Main.cpp`가 창과 상태 흐름을 맡고, 대부분의 하위 시스템은 헤더 전용입니다.
- `rust/song_selection/` 선곡 그룹·정렬 모델 (C ABI로 연결)
- `Assets/` UI(RmlUi), 폰트, 기본 스킨
- `docs/` 설계와 측정 문서 (스킨 포맷: `docs/SevenManiaSkinFormat-v1.md`)

## 라이선스

[MIT](LICENSE). 서드파티(RmlUi, FreeType 등) 라이선스는 릴리스 ZIP의 `Assets/Licenses/`에 포함되어 있습니다.

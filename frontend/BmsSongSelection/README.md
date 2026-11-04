# BMS C# 곡 선택 화면

이 WPF 프런트엔드는 C++ 플레이어를 한 번 실행해 UTF-8 JSON 카탈로그를 받고, 곡 묶기·정렬·난이도 선택·검색·숨김 목록 표시를 C#에서 처리합니다. 채보 파싱과 곡 데이터의 의미는 C++이 소유합니다.

## 실행

1. 저장소 루트에서 `cmake --build build --config Release --target BmsPlayer`로 C++ 플레이어를 빌드합니다.
2. 이 프로젝트를 .NET 8 SDK가 설치된 Windows에서 빌드합니다.
3. 앱에서 `BmsPlayer.exe`와 곡 폴더를 선택합니다. 명령행으로 `--player <exe> --library <folder>`를 지정해 시작할 수도 있습니다.

`BmsPlayer.exe --catalog <folder> --seed <정수>`는 UI·오디오 초기화 없이 스캔한 곡 요약을 표준 출력에 JSON 한 개로 씁니다. 이 경로는 지정한 곡 폴더를 변경하거나 Songs 폴더로 복사하지 않습니다.

현재 C# 화면은 곡 선택과 카탈로그 표시 단계입니다. 곡 시작 버튼은 비활성 상태이며, 다음 통합 단계에서 같은 RANDOM 시드를 사용해 선택 채보를 기존 C++ 재생 엔진에 전달해야 합니다. 설정과 플레이 기록도 아직 C++ 소유입니다.

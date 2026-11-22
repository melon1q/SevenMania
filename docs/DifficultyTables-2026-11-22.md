# 난이도표 (2026-11-22)

BMS 난이도표(beatoraja·LR2 도구가 읽는 형식)를 등록해 곡 선택 화면에서 폴더로 고른다.

## 쓰는 법

1. 게임 설정 → "곡 라이브러리" 페이지 → **난이도표**.
2. **표 주소**에 표 페이지 주소(`<meta name="bmstable" content="header.json">`가 있는 HTML) 또는 `header.json` 주소를 넣고 **추가**. 붙여넣기(Ctrl+V)가 된다.
3. **모두 새로고침**을 누르면 그때 인터넷에서 받는다. 그 밖에는 인터넷에 접속하지 않는다(시작할 때도 자동으로 받지 않는다).
4. 곡 선택 화면의 맨 위(ROOT)에 `ALL SONGS` 옆으로 표 이름의 폴더가 생긴다. 표 → 레벨(예: `sl3  (8/30)`, 가진 채보/전체) → 그 레벨에서 라이브러리에 있는 채보 순으로 들어간다. Esc·Backspace·UP으로 한 단계씩 나온다.

## 동작

- **맞추기:** 표의 `md5`(없으면 `sha256`)와 채보 파일의 해시를 비교한다. 경로·폴더 이름은 보지 않는다. 해시는 파서가 계산하고 메타데이터 캐시에 저장한다(`Chart::md5`/`sha256`).
- **없는 채보:** 목록에는 가진 채보만 나온다. 레벨 이름의 `(가진 수/전체)`로만 알 수 있다.
- **레벨 순서:** 헤더의 `level_order`가 먼저, 그 밖의 레벨은 숫자 순, 그다음 글자 순.
- **필터:** 키 모드·난이도 필터가 표 안에서도 적용된다. 한 곡의 여러 채보 중 그 레벨에 있는 채보가 선택된다.
- **저장:** 주소는 설정 파일 `[DifficultyTables]`(`Count`, `Url0`…)에, 내려받은 헤더·데이터는 설정 폴더의 `tables\<주소 해시>.header.json`·`.data.json`·`.source.txt`에 받은 그대로 둔다. 다음 실행에는 이 사본을 읽는다. 제거하면 사본도 지운다.
- **글자 인코딩:** UTF-8(BOM 포함)이 아니면 Shift_JIS로 읽는다.
- **다운로드:** WinHTTP(Windows 내장)로 받으며 http·https, 리다이렉트, 시스템 프록시를 따른다. 한 파일 64MB까지, 응답 대기 30초. 받는 중에는 설정의 제거·새로고침 버튼이 잠긴다. 게임을 끄면 받던 작업을 멈춘다(최대 응답 대기 시간만큼 걸릴 수 있다).

## 코드

| 파일 | 역할 |
|---|---|
| `src/DifficultyTable.h` | 메타 태그·URL 해석, 헤더·데이터 JSON 파싱(`JsonValue.h`), 레벨 순서, 캐시, 라이브러리 대조 |
| `src/DifficultyTableFetch.h` | WinHTTP 다운로드와 페이지→헤더→데이터 순서 |
| `src/DifficultyTableHost.inl` | 등록 목록, 백그라운드 새로고침(`WM_TABLES_UPDATED`), 곡 목록 행(`tableRowBase`/`levelRowBase`), 필터 |
| `src/DifficultyTableSettings.inl` | 설정 페이지 항목(RmlUi) |
| `tests/DifficultyTableTests.cpp` | 파싱·URL·레벨·대조·캐시 테스트(네트워크 없음). `--fetch <주소>`로 실제 표를 받아 요약하는 수동 점검 |

수동 점검 예: `BmsDifficultyTableTests.exe --fetch https://stellabms.xyz/sl/table.html` → Satellite, 2568개, 레벨 13개(2026-11-22 확인).

## 한계

- RmlUi 설정 화면에만 항목이 있다(네이티브 설정 창 없음).
- 표의 코스(`course`) 정보와 표 자체의 URL 갱신 안내는 읽지 않는다.
- 레벨 폴더 안에서 표가 정한 곡 순서가 아니라 라이브러리 정렬 순서를 쓴다.
- 선곡 화면 위치를 저장할 때 표 안의 위치는 저장하지 않는다(다음 실행은 ROOT에서 시작).

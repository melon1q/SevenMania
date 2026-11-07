# 이전 기록 표시 수정

저장: finishPlayback → savePlayRecord. 조회: rebuildChartRecordKeys → playRecordForChart.
저장/조회 모두 normalizedRecordPath를 사용하며 동일 키를 사용하는 회귀 테스트가 통과했다.
원인은 drawBrowseScreen의 BMS_ENABLE_SKIN_BINDINGS 조기 반환으로 기존 drawBrowsePlayRecord를 호출하지 않은 것이다.
스킨의 기존 출력 뒤에 최소 GDI 기록 요약을 연결했다. BEST SCORE, 해당 랭크, 누적 CLEAR/FAILED, 최대 콤보를 표시하며 기록이 없으면 NO RECORD다.
기존 저장 형식과 최고 점수/클리어/콤보의 독립 갱신 정책, 연습 모드 저장 제외를 유지했다. 새로운 바인딩은 추가하지 않았다.

Release BmsPlayer 및 SevenManiaSkinBindingTests 빌드 성공. CTest 1/1 통과.
테스트는 격리 INI에서 기록 없음, 저장 키/조회 키 일치, 다른 채보 분리, 신기록 갱신, 클리어/콤보 보존, 메모리 기록 초기화 후 디스크 재로딩을 확인한다.
기존 스킨의 로딩·바인딩·렌더링 회귀 테스트도 같은 실행에서 통과했다.
실제 별도 프로세스 재시작과 사용자 화면의 수동 시각 검증은 하지 않았다. 재시작 검증은 동일 로드 함수를 사용한 디스크 재로딩 수준이다.
사용자 기존 변경을 제외하고 관련 변경만 커밋했다. 커스텀 스킨의 특수 배치에서는 요약 영역과 겹칠 수 있으며 기본 표시 영역을 사용한다.

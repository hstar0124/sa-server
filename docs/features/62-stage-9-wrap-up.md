# 62 (9단계) 조회 모델의 거래 수와 9단계 마무리: 설계 노트 13, README 거래 절

## 배경과 목표
58~61로 거래가 끝까지 돈다. Backend는 확정마다 참여자별 `TradeCompleted` 이벤트를 Outbox에 쓰고, EventWorker가 그것을 Kafka로 내보낸다. 지금까지 통계 소비자는 이 이벤트를 모르는 종류로 기록만 하고 넘겼다. 이 PR은 다음을 한다.
- 캐릭터 통계에 확정한 거래 수를 센다.
- e2e로 그 수가 이벤트 수와 같은지 확인한다.
- 9단계를 문서로 마무리한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 거래로 받은 금화 | "번 골드"에 더하기 / 더하지 않기 | 더하지 않는다. 주고받은 금화는 번 것이 아니다. 거래 수만 센다 |
| e2e의 비교 대상 | 봇이 본 확정 수 / Backend 기록 | Backend 기록. 실행이 끝나는 순간의 확정은 봇이 마지막 `TradeUpdate`를 받기 전에 떠날 수 있다(한 실행에서 48 대 50, 마지막 확정이 세션을 닫기 시작한 지 14 ms 뒤). 통계는 그 캐릭터들의 `TradeCompleted` 수와 같아야 한다 |
| 릴리스 | 9단계만 / 8c와 함께 | 함께. 0.8.0을 건너뛰고 0.9.0으로 낸다 |

## 구현
- EventWorker `CharacterStatsProjection`: `TradeCompleted` → `TradesCompleted` 1. 페이로드는 디코딩해 검증한다. `ProjectionStore`가 `trades_completed`를 더한다(열은 마이그레이션 0009)
- Backend `GET .../characters/{c}/stats`: `tradesCompleted`
- `scripts/lib/e2e.sh`: 이 실행에서 거래한 캐릭터들의 통계 거래 합 = 그 캐릭터들의 `TradeCompleted` 수
- [설계 노트 13](../design-notes/13-trading.md): 협상 상태의 위치, 버전과 동의, 확정 트랜잭션과 두 번에 걸친 이동, 멱등성, Zone의 몫, 떠나면 취소, 옮기면서 고친 결함
- README: "거래" 절(테이블, 규칙, 시퀀스 다이어그램), 프로그램 표, 데이터의 기준 표, 거래 e2e, 정합성 테스트 목록
- CLAUDE.md: 단계 목록에 9단계 완료

## 검증
- .NET 테스트: 107 → 108개(투영 1: 거래 하나가 한 번 세어지고 번 골드는 그대로). 빌드 경고 0, 형식 검사 통과.
- 로컬 e2e `traders.toml`: 확정 거래 25건(확정 50번), `TradeCompleted` 50개, 통계의 거래 합이 이벤트 수와 같음, 열린 거래 0.
- 로컬 e2e smoke·fighters·looters·travelers: 통과.
- C++: 테스트 327개, dev·release·ASan, clang-tidy, clang-format 통과(9단계 끝의 전체 검증).
- 용량(1,000명, release): Zone 0.17코어, Gateway 0.22코어, 놓친 틱 0, 미룬 스냅샷 0. 8c 끝(0.18/0.23)과 같다. 거래 경로는 처리량에 영향이 없다.

## 남긴 것
- 이 PR로 9단계가 끝난다. 머지 뒤 8c와 9단계를 묶어 0.9.0을 낸다.
- 다음: 10 공간 분할 Zone.

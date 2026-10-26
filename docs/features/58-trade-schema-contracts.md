# 58 (9단계) 거래의 바탕: ADR-0013, 스키마, Backend 계약, 확정 이벤트

## 배경과 목표
9단계는 두 플레이어의 거래다. 제안을 주고받다가 둘 다 같은 제안에 동의한 순간, 양쪽의 아이템과 금화가 한 번에 바뀌어야 한다. 이 프로젝트의 자산은 Backend(.NET)의 PostgreSQL이 기준이고, Zone(C++)은 거리처럼 자기만 아는 것을 검증한다. 이 PR은 단계의 바탕만 놓는다.
- 결정: ADR-0013
- 테이블과 제약: 마이그레이션 0009
- Backend gRPC 계약: `contracts/backend/trade.proto`
- 확정 이벤트: `TradeCompleted`

Backend 구현(59), Zone·Gateway와 클라이언트 메시지(60), 봇·뷰어(61), 조회 모델(62)이 이 위에 올라간다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 협상 상태 | Zone 메모리 / Backend 테이블 | Backend 테이블(ADR-0013). "한 캐릭터는 거래 하나"를 Zone 두 개 사이에서 지키려면 DB 행이 필요하다 |
| 아이템 맞교환 | 음수 주차 칸(이전 이력) / 용량 이상의 칸 / 지연 가능한 유일 제약 | 용량 이상의 칸. 이 저장소는 칸 번호 음수 금지 CHECK가 있다(마이그레이션 0005). 이전 이력은 그 제약 없이 음수 칸을 썼다. 용량 이상의 번호는 정상 칸과 겹치지 않아 같은 효과를 내고, 제약을 풀지 않는다 |
| 요청 결과·원장 | 거래용 테이블 / 기존 `command_results`·`economy_ledger` | 기존 것. scope에 `trade`, 원장 사유에 `trade`를 더한다. 이전 이력에는 두 CHECK가 없었다 |
| 클라이언트 메시지 | 이 PR / Zone·Gateway PR(60) | 60. 새 메시지가 생기면 Gateway와 Zone의 메시지 분기를 같은 커밋에서 다뤄야 빌드된다(경고를 오류로 다룸) |

## 구현
- `database/migrations/0009_trades.sql`
  - `trades`: 참여자 둘(서로 다름), 상태, 버전, 양쪽 금액(음수 불가), 양쪽이 동의한 버전(0~현재 버전, 또는 NULL), 만료 시각
  - `trade_participants`: (world, character) 기본 키. 열린 거래 하나
  - `trade_items`: 아이템 ID 기본 키. 열린 거래 하나
  - 캐릭터 참조는 이 저장소의 다른 테이블처럼 (world, character) 복합 외래 키다. 이전 이력은 character만 참조해 다른 World의 캐릭터를 막지 못했다
  - `command_results_scope`에 `trade`, `economy_ledger_reason`에 `trade`
  - `character_projections.trades_completed`와 음수 불가 CHECK
- `contracts/backend/trade.proto`: `Trade` 서비스(Open, Offer, Accept, Cancel), 결과 15가지, 거래 상태
- `contracts/events/envelope.proto`: `TradeCompleted`(참여자마다 하나, 주고받은 금화와 아이템, 인벤토리 버전)
- [ADR-0013](../adr/0013-trading-as-backend-owned-negotiation.md)

## 옮기면서 고친 것
- 음수 주차 칸 대신 용량 이상의 칸(위 표).
- 복합 외래 키로 World를 함께 묶었다.
- 동의 버전이 존재했던 버전인지(0~현재) CHECK를 더했다. 만료 시각이 생성 뒤인지도 CHECK한다.
- 요청 키를 Zone의 digest로 만든다고 ADR에 적었다(이슈 012의 교훈).
- 세션이 끝나는 모든 이유(무응답·느린 클라이언트 끊기, 링크 끊김 포함)에 취소를 요청한다고 ADR에 적었다. 이전 이력에는 그 끊기들이 없었다.

## 검증
- 마이그레이션 0009를 실제 PostgreSQL에 적용한 채 .NET 테스트 95개(단위 30, 통합 65)가 통과했다.
- C++·C# 빌드 통과(계약 생성 코드 포함).

## 남긴 것
- 59: Backend `Modules/Trading`과 통합 테스트
- 60: Zone 검증·중계, Gateway 전달, 클라이언트 메시지
- 61: Trader 봇과 뷰어
- 62: 조회 모델 `trades_completed`, 설계 노트, 단계 마무리

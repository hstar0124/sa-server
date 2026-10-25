# 61 (9단계) Trader 봇과 뷰어 거래 조작: 거래가 끝까지 도는 것을 봇과 화면으로

## 배경과 목표
59(Backend)와 60(Zone·Gateway)으로 거래의 서버 경로가 다 갖춰졌다. 이 PR은 그 경로를 끝까지 돌리는 클라이언트 두 가지를 넣는다.
- loadgen의 Trader 봇: e2e로 판정한다
- 브라우저 뷰어의 거래 조작: 사람이 직접 확인한다

완료 조건:
- 봇끼리 짝을 지어 열고, 제안하고, 동의해 확정한다.
- Backend 기록, 확정 이벤트, 봇이 본 확정 수가 서로 맞는다.
- 봇이 떠난 뒤 열린 거래가 남지 않는다(세션 종료 취소).

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 두 봇의 협조 | 역할을 나눈 두뇌(파는 쪽, 사는 쪽) / 같은 두뇌 | 같은 두뇌(`sa::bot::Trader`). 가까운 플레이어에게 걸어가 열고, 금화 1을 제안하고, 양쪽이 제안하면(버전 2 이상) 동의한다. 둘이 동시에 열어도 Backend가 하나만 앉히고, 진 쪽은 `TradeUpdate`로 그 거래를 알게 된다 |
| 단계의 순서 | 매번 새로 보내기(이전 이력) / 한 번에 한 단계 | 한 번에 한 단계. Zone이 그렇게 받는다(60). 관찰에 `trade_step_pending`을 두어 판정을 기다리는 동안 두뇌가 다음 단계를 내지 않는다 |
| 결과를 모를 때 | 버리기(이전 이력) / 같은 요청 id로 다시 | 같은 id로 다시. 결과가 UNKNOWN이면 세션이 `trader.request_retry_ms` 뒤에 같은 메시지를 다시 보낸다. 이동(ChangeZone)의 재시도와 같은 규칙이다 |
| 간격 단위 | 틱(이전 이력) / 벽시계 ms | ms. 이 저장소의 다른 두뇌처럼 프로필은 벽시계로 주고, 두뇌가 서버 틱 속도로 바꾼다 |
| 뷰어의 재시도 | 자동 / 같은 키를 다시 누르면 같은 id | 같은 키. 포털(G)과 같은 방식이다. 같은 단계(같은 대상·거래·버전)를 다시 누르면 같은 요청 id로, 다른 단계는 새 id로 보낸다 |

## 구현
- loadgen 두뇌: `observation.hpp`에 `SeenTrade`, `TradeIntent`, 관찰의 `gold`·`trade`·`trade_step_pending`. `trader.{hpp,cpp}`: 짝 찾기·열기·제안·동의·포기(`give_up_ms`)·쉬기(`cooldown_ms`). 아무도 없으면 Fighter처럼 사냥해 금화를 번다
- loadgen 클라이언트
  - `WorldModel::OnTradeUpdate`: 거래 상태와 확정·취소 수. 같은 거래의 옛 버전은 정합성 오류. 닫힌 거래는 두뇌가 한 번 본 뒤 잊는다. 새 Welcome에서 지운다
  - `BotSession`: 판정 전 단계 하나를 들고 UNKNOWN이면 다시 보낸다
  - 보고서: `trade settlements=… steps sent/accepted/rejected/unknown`, 통과 조건 `run.min_trade_settlements`
  - 프로필: `bots.traders`, `[trader]`, `run.min_trade_settlements`. 기존 프로필 모두에 0으로 더했다. 새 `profiles/traders.toml`(트레이더 6명, 확정 4번 이상)
- `scripts/lib/e2e.sh`: Backend가 확정한 거래 × 2 ≥ 요구 확정 수, `TradeCompleted` 이벤트 = 확정 거래 × 2, 이 실행의 열린 거래 0
- 뷰어: T(가장 가까운 플레이어와 열기, 거래 거리는 `economy.toml`에서 읽음), Y(금화 1 제안), Enter(화면의 버전에 동의), Esc(취소). HUD에 거래와 마지막 결과. 새 Welcome에서 지운다

## 지킨 것 (이번 저장소의 개선)
- 54의 외삽: 짝 찾기와 뷰어의 가까운 플레이어 고르기가 외삽 위치를 쓴다.
- `identity_seed`: 실행마다 새 캐릭터라 이전 실행의 금화·인벤토리에 결과가 흔들리지 않는다.
- 46의 Zone 변경 처리: 새 Welcome에서 거래 상태와 판정 전 단계를 지운다.

## 검증
- C++: 테스트 324 → 327개 통과(Trader 두뇌 3, 프로필 검사 갱신).
- 로컬 e2e `traders.toml`: 6명이 60초 동안 거래 25건 확정(확정 50번, 요구 4), `TradeCompleted` 50개, 이벤트 67개 발행·적용, 열린 거래 0, 정합성 오류 0. 거절 18건은 서로 동시에 연 경우(`ALREADY_TRADING`)다.
- 로컬 e2e smoke·fighters·looters·travelers: 통과.
- 뷰어: `node --check`, 그리고 실제 `economy.toml`로 거리 읽기와 거래 상태 갱신을 확인했다.

## 남긴 것
- 62: 조회 모델 `trades_completed`, 통계 API, 설계 노트 13, 9단계 마무리.

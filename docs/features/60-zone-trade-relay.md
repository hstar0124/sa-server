# 60 (9단계) Zone의 거래 검증과 중계, Gateway 전달, 클라이언트 메시지

## 배경과 목표
거래는 Backend가 협상 상태를 소유하고, Zone(C++)은 자기만 아는 것을 검증한 뒤 Backend에 넘기고 결과를 중계한다(ADR-0013). Zone만 아는 것은 다음과 같다.
- 두 사람이 가까이 서 있는가
- 살아 있는가
- 포털로 떠나는 중인가

이 PR은 클라이언트 메시지 네 가지(열기, 제안, 동의, 취소)와 `TradeUpdate`를 계약에 더한다. 그리고 Gateway 전달, Zone 시뮬레이션의 검증, 호스트의 Backend 호출과 중계를 넣는다. Backend `Trade` 서비스는 바로 앞의 59에 있고, 봇과 뷰어는 61이다.

완료 조건:
- 거래 단계가 Backend에 한 번만 간다. 재시도는 같은 키로 간다.
- 판정은 요청자에게, 상태는 이 Zone에 있는 두 참여자 모두에게 간다.
- 확정되면 두 사람의 자산을 다시 읽는다.
- 세션이 어떤 이유로 끝나든 열린 거래를 취소한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| Backend가 판단 중인 단계의 재시도 | 최근 결과에 Pending을 기억했다가 지우기(이전 이력) / 플레이어마다 판단 중인 단계 하나 | 판단 중인 단계 하나(`Player::trade_in_flight`). 이 저장소의 최근 결과 기록은 최종 판정만 담는다(획득은 예약에서, 이동은 `departing`에서 답한다). 이전 이력은 Pending을 기록했다가 지우는 `Forget`을 더했다. 같은 단계의 재시도는 Pending으로 답하고 다시 보내지 않는다 |
| 판단 중일 때 온 다른 단계 | 함께 보내기(이전 이력) / 처리하지 않고 "모름" | "모름". 동의는 제안이 만든 버전을 가리키므로 순서가 뒤집히면 안 된다. 클라이언트는 같은 요청 id로 다시 보낸다 |
| Backend 요청 키 | 클라이언트 요청 id / (캐릭터, 소유권 세대, 요청 id)의 HMAC digest | digest(`codec::TradeRequestKey`). 획득과 이동이 쓰는 `CommandDigest`를 도메인만 바꿔 공유한다(이슈 012의 교훈) |
| 세션 종료 때의 취소 키 | 요청 id 0의 digest(이전 이력) / 새로 만든 키 | 새로 만든 키. 이전 이력은 같은 캐릭터·세대에서 두 번째 취소가 첫 취소의 저장된 답을 받을 수 있었다. 키는 호출 하나의 재시도 사이에서만 유지된다 |
| 지역 이동 중 거래 | 이동 뒤 취소 / 이동 전에 취소 | 이동 전. Backend는 소유자의 취소만 받는다. 이동이 끝난 뒤의 취소는 낡은 권한으로 거절된다. 그래서 거래 중인 세션의 이동은 취소의 답을 받은 뒤 보낸다. 떠나는 사람은 거래를 열 수도 없고, 거래 상대가 될 수도 없다 |
| 세션이 거래 중인지 | 마지막 TradeUpdate만 보기(이전 이력) / 단계를 보낼 때 켜고 닫힌 상태를 들으면 끄기 | 후자. 답을 잃은 열기도 세션이 끝날 때 취소를 부른다 |
| 형식이 틀린 id | nil id로 바꿔 Backend에 보내기(이전 이력) / codec에서 거부 | codec에서 거부(`InputError::kMalformedId`). 이동·시전의 비정상 입력처럼 시뮬레이션에 닿지 않고 센다 |
| Backend 결과의 해석 | 이전 이력의 표 | 두 가지를 고쳤다. `STALE_AUTHORITY`는 "소유자 아님"으로 획득처럼 세션을 내보낸다(이전 이력은 "거래 닫힘"). `REQUEST_MISMATCH`는 "잘못된 요청"이다(이전 이력은 "잘못된 대상") |

## 구현
- 계약
  - `ClientMessage`: `TradeOpen`(14), `TradeOffer`(15), `TradeAccept`(16), `TradeCancel`(17)
  - `ServerMessage`: `TradeUpdate`(14). 수신자 기준의 `you`와 `other`. 상대가 이 Zone에 없으면 entity 0
  - `RejectCode` 40~45: 거래 중, 닫힘, 버전 불일치, 아이템 불가, 금화 부족, 상대 없음
- 게임 데이터: `economy.toml [trade] range_m`(5 m). 열 때만 검사한다. 확정은 두 사람이 같은 Zone의 소유인지로 Backend가 검사한다
- Gateway `SessionProtocol`: 네 메시지를 다른 명령처럼 Zone으로 전달한다(이동 중에는 버림)
- Zone 시뮬레이션 `zone_trade.cpp`: `TradeCommand` → 검증 → Pending과 `TradeRequest`. `ResolveTrade(request, reason)`가 판정을 반영한다. "모름"은 기억하지 않아 재시도가 Backend에 다시 간다
- Zone 호스트
  - `trade_client.{hpp,cpp}`: `TradeGateway` 인터페이스와 gRPC 구현. 공용 재시도 정책(`backend_call.hpp`)을 쓴다
  - `trade_codec.{hpp,cpp}`: 클라이언트 메시지 ↔ 명령, 명령 → Backend 요청, 결과 → 판정, 상태 → `TradeUpdate`. codec.cpp가 이미 450줄이라 거래를 따로 두었다
  - `zone_host_trade.cpp`: 단계 전달, 판정 반영, 두 참여자 중계, 확정 시 자산 재조회, 세션 종료 취소
  - `zone_host_placement.cpp`: 거래 중이면 취소 뒤 이동
  - 통계: `trade_steps_sent`, `trade_steps_refused`, `trades_settled`, `trade_cancels_on_leave`
- loadgen: 아직 거래하는 봇이 없어 `TradeUpdate`를 무시한다(61에서 처리)

## 지킨 것 (이번 저장소의 개선)
- 공용 재시도 정책과 digest 요청 키(`CommandDigest` 공유).
- 최근 결과 기록에는 최종 판정만 둔다는 규칙.
- 무응답·느린 클라이언트 끊기, 링크 끊김, 상위 세대 도착이 모두 `RemoveSession`을 거치므로, 취소도 그 한 곳에서 부른다.
- 46의 Zone 변경 처리: 이동 중에 온 거래 메시지는 Gateway가 버린다.

## 검증
- C++: 테스트 309 → 324개 통과.
  - 시뮬레이션 4: 열기 검증(거리, 자기 자신, 없는 대상, 요청 id 0), 판정 기억, 판단 중 재시도와 다른 단계, 떠나는 사람
  - 호스트 6: digest 키와 양쪽 중계, 확정 시 두 자산 재조회, 세션 종료 취소와 상대 통지, 이동 전 취소, 무응답 재시도 같은 키, 낡은 권한 퇴장
  - codec 4: 형식 오류 거부, 결과 해석, 수신자 기준 상태, 요청 키
  - Gateway 1: 네 메시지 전달
- dev·release·ASan, clang-tidy, clang-format: 9단계 브랜치를 마칠 때 함께 돌린다.

## 남긴 것
- 61: Trader 봇, `traders.toml`, 뷰어 거래 조작

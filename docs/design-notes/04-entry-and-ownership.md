# 04 입장 경로와 소유권

이 프로젝트의 정합성 주장은 "두 사람이 같은 캐릭터를 동시에 조종하지 않는다"에서 시작한다. 입장 경로가 그 첫 관문이다.

## 보장하려는 것

1. 티켓 한 장은 한 번만 쓰인다.
2. 캐릭터 하나에 살아 있는 세션은 하나다.
3. 캐릭터를 시뮬레이션하는 Zone은 한 시점에 하나이고, 소유권 번호(`owner_epoch`)는 단조 증가한다.
4. 죽은 프로세스가 남긴 상태는 다음 요청 때 자연히 치워진다.

## 흐름 (설계 문서 6.2절, ADR-0005)

```
클라이언트 ── HTTPS ──▶ Backend   : 로그인, 캐릭터 선택, 서명 티켓 발급
클라이언트 ── TCP ────▶ Gateway   : Hello(티켓)
Gateway    ── 로컬 ────           : HMAC 서명·만료 검증 (Backend 호출 전에 가짜를 걸러낸다)
Gateway    ── gRPC ───▶ Backend   : RedeemEntryTicket — 한 트랜잭션에서 1~3을 확정
Gateway    ── 링크 ───▶ Zone      : SessionAttach(캐릭터, owner_epoch)
Zone       ── 링크 ───▶ Gateway   : SessionAttached + Welcome, 이후 스냅샷
```

## 각 보장을 어디서 지키나

| 보장 | 메커니즘 | 왜 애플리케이션 검사만으로는 안 되나 |
|---|---|---|
| 티켓 1회 사용 | `UPDATE entry_tickets SET redeemed_at = now() WHERE ticket_id = ? AND redeemed_at IS NULL AND expires_at > now()` 의 반환 행 수 | 두 Gateway가 동시에 같은 티켓을 내면 SELECT 후 판단은 둘 다 통과한다. 조건부 UPDATE는 한쪽만 1행을 받는다 |
| 세션 하나 | `character_sessions (character_id) WHERE ended_at IS NULL` 부분 UNIQUE 인덱스 | 존재 확인과 삽입 사이의 틈을 DB가 막는다. 경합에서 진 쪽은 UNIQUE 위반을 받아 `ACTIVE_SESSION_EXISTS`로 바뀐다 |
| 소유 Zone 하나, epoch 증가 | `character_ownership` 행을 `FOR UPDATE`로 잠근 뒤 `ON CONFLICT DO UPDATE SET owner_epoch = owner_epoch + 1` | 잠금이 두 트랜잭션의 epoch 증가를 직렬화한다 |
| 죽은 프로세스 정리 | Zone·Gateway가 인스턴스 ID로 lease를 갱신한다. 소유권은 살아 있는 lease의 Zone에만 부여되고, Gateway lease가 끝난 세션은 다음 redeem에서 `ended_at`이 채워진다 | 프로세스가 죽을 때 정리 코드가 실행된다는 보장이 없다. 살아 있음을 증명하는 쪽이 책임을 진다 |

시계는 하나다. 티켓 발급·만료, lease 만료, 세션 종료 판정은 모두 PostgreSQL의 `now()`로 결정한다. Gateway의 로컬 만료 검사는 가짜를 걸러내는 1차 필터일 뿐이고, 허용 오차(`max_clock_skew_ms`)를 둔다.

## 왜 Redis가 아닌가

설계 문서 6.2절 초안은 Gateway가 Redis로 티켓을 원자 소비했다. 그러나 문서 13절의 원칙은 "Redis 소실이 자산·권한 소실로 이어지지 않는다"다. 티켓 소비와 세션 생성과 소유권 변경을 **같은 PostgreSQL 트랜잭션**에 두면 셋이 함께 성공하거나 함께 실패한다. Redis는 나중에 "이미 소비된 티켓" 조회 캐시로만 붙고, 없어도 정확성에 영향이 없다.

## 테스트가 증명하는 것

통합 테스트는 실제 PostgreSQL에서 다음을 확인한다.

- 같은 티켓 두 번: 첫 번째 GRANTED, 두 번째 ALREADY_REDEEMED.
- 두 Gateway가 같은 캐릭터의 티켓 두 장을 동시에 redeem: 5라운드 반복 모두 정확히 한 건만 GRANTED.
- 세션이 살아 있으면 ACTIVE_SESSION_EXISTS, EndSession 후 다시 GRANTED이고 epoch가 2로 증가.
- lease가 1초인 Gateway가 갱신을 멈추면, 다음 redeem에서 그 세션이 `gateway_lease_expired`로 끝나고 새 세션이 허용된다.
- 티켓과 다른 캐릭터를 요청하면 CHARACTER_NOT_OWNED이고 티켓은 소비된다(탬퍼링 시도는 티켓을 태운다).

## 아직 안 한 것

- 두 번째 Zone으로의 이전 상태 머신(설계 문서 14.2절)과 Zone 측 epoch 비교. 8단계.
- Gateway↔Backend 내부 호출의 신원 검증(mTLS 또는 토큰). 설계 문서 18절, 10단계.

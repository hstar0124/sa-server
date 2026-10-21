# 04 입장 경로와 소유권

## 배경

이 프로젝트에는 프로세스가 셋 있다. **Backend**(C#, PostgreSQL)가 계정·캐릭터·자산의 영구 상태를 가지고, **Gateway**(C++)가 클라이언트 연결을 받으며, **Zone**(C++)이 지역 하나를 시뮬레이션한다. 플레이어가 게임에 "들어간다"는 것은 Backend에서 로그인한 캐릭터를 Gateway를 거쳐 어느 Zone의 시뮬레이션에 붙이는 일이다. 이 글은 그 경로에서 "두 사람이 같은 캐릭터를 동시에 조종하지 않는다"를 어떻게 보장하는지 설명한다.

## 보장하려는 것

1. 입장 티켓 한 장은 한 번만 쓰인다.
2. 캐릭터 하나에 살아 있는 세션은 하나다.
3. 캐릭터를 시뮬레이션하는 Zone은 한 시점에 하나이고, 소유권 번호(`owner_epoch`)는 단조 증가한다.
4. 죽은 프로세스가 남긴 상태는 다음 요청 때 자연히 치워진다.

## 흐름

```
클라이언트 ── HTTPS ──▶ Backend   : 로그인, 캐릭터 선택, 서명 티켓 발급
클라이언트 ── TCP ────▶ Gateway   : Hello(티켓)
Gateway    ── 로컬 ────           : HMAC 서명·만료 검증 (Backend 호출 전에 가짜를 걸러낸다)
Gateway    ── gRPC ───▶ Backend   : RedeemEntryTicket — 한 트랜잭션에서 1~3을 확정
Gateway    ── 링크 ───▶ Zone      : SessionAttach(캐릭터, owner_epoch)
Zone       ── 링크 ───▶ Gateway   : SessionAttached + Welcome, 이후 스냅샷
```

티켓은 Backend가 발급하는 작은 Protobuf 메시지에 HMAC-SHA256 서명을 붙인 것이다. 내용은 티켓 ID, 계정, 캐릭터, 월드, 발급·만료 시각이다. 서명 키는 Backend와 Gateway만 안다. Gateway는 서명과 만료만 보고 Backend에 제출하며, **티켓이 유효하다는 것만으로 조종 권한이 생기지는 않는다.** 권한은 Backend 트랜잭션이 준다.

## 각 보장을 어디서 지키나

| 보장 | 메커니즘 | 왜 애플리케이션 검사만으로는 안 되나 |
|---|---|---|
| 티켓 1회 사용 | `UPDATE entry_tickets SET redeemed_at = now() WHERE ticket_id = ? AND redeemed_at IS NULL AND expires_at > now()` 의 반환 행 수 | 두 Gateway가 동시에 같은 티켓을 내면 "SELECT 후 판단"은 둘 다 통과한다. 조건부 UPDATE는 한쪽만 1행을 받는다 |
| 세션 하나 | `character_sessions (character_id) WHERE ended_at IS NULL` 부분 UNIQUE 인덱스 | 존재 확인과 삽입 사이의 틈을 DB가 막는다. 경합에서 진 쪽은 UNIQUE 위반을 받아 "이미 세션이 있다"로 바뀐다 |
| 소유 Zone 하나, epoch 증가 | `character_ownership` 행을 `FOR UPDATE`로 잠근 뒤 `ON CONFLICT DO UPDATE SET owner_epoch = owner_epoch + 1` | 잠금이 두 트랜잭션의 epoch 증가를 직렬화한다 |
| 죽은 프로세스 정리 | Zone과 Gateway가 각자 인스턴스 ID로 **lease**(기한 있는 등록)를 주기적으로 갱신한다. 소유권은 lease가 살아 있는 Zone에만 부여되고, Gateway lease가 끝난 세션은 다음 요청에서 종료 처리된다 | 프로세스가 죽을 때 정리 코드가 실행된다는 보장이 없다. 살아 있음을 증명하는 쪽이 책임을 진다 |

시계는 하나다. 티켓 발급·만료, lease 만료, 세션 종료 판정은 모두 PostgreSQL의 `now()`로 결정한다. Gateway의 로컬 만료 검사는 가짜를 걸러내는 1차 필터일 뿐이고, 시계 차이 허용치(`max_clock_skew_ms`)를 둔다.

`owner_epoch`는 fencing token이다. 8단계에서 캐릭터가 다른 Zone으로 옮겨가면 이 번호가 올라가고, 이전 Zone이 늦게 보낸 저장 요청은 낮은 번호라서 거부된다.

## 왜 Redis가 아닌가

초안에서는 Gateway가 Redis로 티켓을 원자 소비했다. 그러나 이 프로젝트의 원칙은 "Redis를 잃어도 자산과 권한은 잃지 않는다"이다. 티켓 소비와 세션 생성과 소유권 변경을 **같은 PostgreSQL 트랜잭션**에 두면 셋이 함께 성공하거나 함께 실패한다. Redis는 나중에 "이미 소비된 티켓" 조회 캐시로만 붙고, 없어도 정확성에 영향이 없다.

## 테스트가 증명하는 것

통합 테스트는 실제 PostgreSQL(Testcontainers)에서 다음을 확인한다. HTTP 상태 코드가 아니라 **DB의 행**을 단언한다.

- 같은 티켓 두 번: 첫 번째 GRANTED, 두 번째 ALREADY_REDEEMED.
- 두 Gateway가 같은 캐릭터의 티켓 두 장을 동시에 제출: 5라운드 반복 모두 정확히 한 건만 GRANTED.
- 세션이 살아 있으면 ACTIVE_SESSION_EXISTS, 세션을 끝낸 뒤 다시 GRANTED이고 epoch가 2로 증가.
- lease가 1초인 Gateway가 갱신을 멈추면, 다음 요청에서 그 세션이 `gateway_lease_expired`로 끝나고 새 세션이 허용된다.
- 티켓과 다른 캐릭터를 요청하면 CHARACTER_NOT_OWNED이고 티켓은 소비된다. 변조 시도는 티켓을 태운다.

## 아직 안 한 것

- 두 번째 Zone으로의 이전 상태 머신과 Zone 측 epoch 비교. 8단계.
- Gateway↔Backend 내부 호출의 신원 검증(mTLS 또는 토큰). 10단계.

# ADR-0005: 입장 티켓은 서명 토큰 + Backend 1회 소비 확정

- 상태: Accepted
- 날짜: 2026-10-12

## 맥락
문서 6.2절은 Gateway가 Redis로 티켓을 원자 소비한다고 적었으나 Redis는 7단계다. 3단계 접속 흐름이 7단계 인프라에 걸린다. 또한 문서 13절은 "Redis 소실이 자산·권한 소실로 이어지지 않는다"를 원칙으로 둔다.

## 결정
1. Backend가 발급하는 입장 티켓은 **HMAC-SHA256 서명 토큰**이다. 내용: `ticket_id, account_id, character_id, world_id, issued_at, expires_at`(기본 TTL 30초, config).
2. Gateway는 **서명과 만료만 로컬 검증**한 뒤, `RedeemEntryTicket` gRPC로 Backend에 제출한다.
3. Backend는 **PostgreSQL에서 `ticket_id` UNIQUE INSERT로 1회 소비를 확정**하고, 같은 트랜잭션에서 활성 세션과 Zone 소유권을 결정한다(6.2절 5단계).
4. Redis는 7단계에서 "이미 소비된 티켓" 조회 캐시로만 붙는다. 없어도 정확성에 영향이 없다.

## 결과
- 3단계에 Redis가 필요 없다. 티켓 소비와 소유권 확정이 한 트랜잭션에 묶여 문서 6.2절의 "티켓만으로 조종 권한을 확정하지 않는다"가 자연히 지켜진다.
- 서명 키는 Backend와 Gateway가 공유한다. Secret 주입(18절). 키 회전은 `key_id` 필드로 대비한다.

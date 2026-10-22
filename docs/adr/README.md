# Architecture Decision Records

형식: 제목 · 상태 · 날짜 · 맥락 · 결정 · 결과(장단점) · 대안. 번호는 순차, 폐기된 ADR은 삭제하지 않고 상태를 `Superseded by ADR-xxxx`로 바꾼다.

| 번호 | 제목 | 상태 |
|---|---|---|
| [0001](0001-adopt-architecture-baseline.md) | 설계 문서 v1.0을 기준으로 채택 (2026-10-16 보정: README·테스트 목록으로 흡수하고 제거) | Accepted |
| [0002](0002-development-order-and-environments.md) | 개발 순서, compose 개발 루프, K3s 도입 시점, 디스크 레이아웃 유지 | Accepted |
| [0003](0003-cpp-for-realtime-servers.md) | Gateway·Zone 언어는 C++23(GCC 14), 툴체인 고정 | Accepted |
| [0004](0004-protocol-stack.md) | Protobuf 단일 계약, 내부 gRPC, 게임 링크는 길이 프리픽스 TCP | Accepted |
| [0005](0005-signed-entry-ticket.md) | 입장 티켓은 서명 토큰 + Backend 1회 소비 확정 | Accepted |
| [0006](0006-bot-client-first.md) | 시드 기반 봇 클라이언트를 첫 클라이언트로 | Accepted |
| [0007](0007-backend-data-access-and-modules.md) | Backend는 단일 어셈블리 모듈형 모놀리스, Npgsql+Dapper, SQL 마이그레이션 | Accepted |
| [0008](0008-pure-simulation-core.md) | Zone 시뮬레이션은 시계·난수 주입형 순수 라이브러리 | Accepted |
| [0009](0009-redis-only-for-measured-bottlenecks.md) | Redis는 측정된 병목(랭킹의 내 순위)에만, 복사본으로 | Accepted |
| [0010](0010-zone-transfer-as-one-transaction.md) | 지역 이전은 Backend의 조건부 트랜잭션 하나, Gateway가 세션을 다시 붙인다 | Accepted |

# contracts — 메시지 계약의 단일 출처

| 디렉터리 | 패키지 | 용도 | 생성 대상 |
|---|---|---|---|
| `game/` | `sa.game.v1` | 클라이언트 ↔ Gateway ↔ Zone 게임 메시지 | C++ (gateway, zone, loadgen), C# (테스트 도구) |
| `backend/` | `sa.backend.v1` | Zone/Gateway ↔ Backend gRPC | C++ 클라이언트 스텁, C# 서버 스텁 |
| `events/` | `sa.events.v1` | Kafka 확정 이벤트 | C# (Backend, Worker) |
| `internal/` | `sa.internal.v1` | Gateway ↔ Zone 세션 다중화 링크 | C++ (gateway, zone) |

규칙 (ADR-0004):
- 필드 번호는 재사용하지 않는다. 제거한 필드는 `reserved`로 남긴다.
- 호환되지 않는 변경은 새 패키지 버전(`v2`)으로 한다.
- 64비트 ID는 `uint64`. Backend의 REST(JSON)는 지금 숫자로 보낸다. ID는 데이터베이스 IDENTITY로 1부터 오르므로 브라우저 정수의 정밀도 한계(2^53)보다 훨씬 작다. 그 한계에 가까워지는 ID를 JSON에 싣는 날에는 문자열로 바꾼다.
- 요청 멱등성 키는 `bytes request_id`(UUID 16바이트). 게임 링크의 `request_id`는 세션 내 단조 증가 `uint64`이며 Zone이 UUID로 바꿔 Backend에 전달한다.
- 프로토콜 한계값(프레임 크기 등)은 `game/framing.proto`의 enum 상수 한 곳에만 둔다.

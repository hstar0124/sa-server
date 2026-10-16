# ADR-0004: Protobuf 단일 계약, 내부 gRPC, 게임 링크는 길이 프리픽스 TCP

- 상태: Accepted
- 날짜: 2026-10-12

## 맥락
문서 6.1절은 클라이언트↔Gateway를 TCP+TLS, Zone/Gateway↔Backend를 gRPC로 둔다. WebSocket과 gRPC 스트리밍을 게임 링크 대안으로 검토했다. 브라우저 기반 디버그 뷰어가 필요하다(ADR-0006).

## 결정
| 구간 | 선택 |
|---|---|
| 메시지 정의 전부 | Protobuf 3, `contracts/` 단일 출처. C++·C#·봇은 생성 코드만 사용 |
| Zone·Gateway ↔ Backend | gRPC (unary, deadline 필수). 패키지 `sa.backend.v1` |
| 클라이언트 ↔ Gateway | 길이 프리픽스 TCP. 프레임 = `uint32 BE length` + Protobuf `ClientMessage`/`ServerMessage`. 최대 프레임 64 KiB(측정 후 조정). 패키지 `sa.game.v1` |
| 클라이언트 ↔ Backend | HTTPS JSON (로그인, 캐릭터 선택) |
| Kafka 이벤트 | Protobuf 직렬화, 봉투(event_id, type, schema_version, aggregate, seq) 공통. 패키지 `sa.events.v1` |

- Gateway의 **전송 계층은 프레이밍과 분리**한다(`Transport` 추상화). TCP가 기본, WebSocket 어댑터는 뷰어가 필요해지는 2~3단계에 추가한다. 두 어댑터는 같은 Protobuf 페이로드를 싣는다.
- TLS는 3단계에서 Gateway에 붙인다. 프레이밍 코드는 TLS 유무를 모른다.
- Protobuf 규칙: 필드 번호 재사용 금지, `reserved` 사용, 패키지에 `v1` 버전, 64비트 ID는 `uint64`(JSON 노출 시 문자열).

## 결과
- gRPC는 C++ 빌드 비용이 있지만 deadline·취소·타입 스텁을 얻는다. Zone의 `Pending` 명령 처리에 deadline이 필수다.
- WebSocket은 게임 링크로는 이점이 없으나(HTTP 업그레이드 + 마스킹된 TCP), 브라우저 뷰어에는 필수. 어댑터로 격리한다.
- gRPC 스트리밍을 게임 링크에 쓰지 않는다: HTTP/2 흐름 제어와 헤더 비용이 20 Hz 스냅샷에 맞지 않는다.

## 대안
- 내부 통신도 자체 TCP 프레이밍: 의존성은 줄지만 deadline·스텁을 직접 구현해야 하고 포트폴리오 가치가 낮다. 기각.

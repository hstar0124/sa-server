# 03 (0단계) Protobuf 계약: 네 패키지, 단일 출처

## 배경과 목표
C++(Gateway, Zone, 봇)과 C#(Backend, Worker)이 같은 메시지를 주고받는다. 손으로 두 번 적으면 어긋난다. 계약을 `.proto` 한 곳에 두고 양쪽은 생성 코드만 쓴다(ADR-0004). 완료 조건: 네 패키지의 메시지가 정의되고, 규칙(필드 번호 재사용 금지, 호환 불가 변경은 v2, ID는 `uint64`, 멱등성 키 형식, 프로토콜 한계값은 `framing.proto` 한 곳)이 README에 적힌다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 게임 링크 직렬화 | JSON / FlatBuffers / Protobuf | Protobuf. 내부 gRPC와 같은 도구, 스키마 진화 규칙이 명확(ADR-0004, 설계 노트 01) |
| 스냅샷 형식 | 전체 상태 / 관심 집합 **차이**(spawned·updated·despawned) + 수신자 자신의 상태 | 차이. 바이트가 N²이 아니라 변경량에 비례. 단, 차이이므로 프레임을 버릴 수 없다는 제약이 뒤 단계(역압)에서 결정을 좌우한다 |
| 입력 식별 | 타임스탬프 / 세션 내 단조 증가 `input_seq` | 단조 증가. 서버가 `last_processed_input_seq`로 돌려주어 클라이언트가 예측을 보정 |
| 요청 멱등성 키 | 게임 링크에서 UUID / 세션 내 `uint64` | 게임 링크는 `uint64`, Zone이 (캐릭터, 세대, 요청)으로 16바이트 키를 만들어 Backend에 전달. 클라이언트가 UUID를 만들게 하지 않는다 |
| Gateway↔Zone | 세션마다 연결 / 하나의 링크에 세션 다중화 | 다중화(`internal/zone_link.proto`). 세션 attach/detach/input/output이 한 연결을 탄다 |
| 프로토콜 한계값 | 코드 상수 / `.proto`의 enum 상수 | `game/framing.proto` 한 곳. C++·C#이 같은 값을 생성 코드로 읽는다(규칙 1) |

## 구현
- `contracts/game/`: `common.proto`(Vec2, EntityKind), `framing.proto`(프레임 한계), `client_messages.proto`(Hello, Ping, MoveInput), `server_messages.proto`(Welcome, WorldSnapshot, Kick, CommandResult)
- `contracts/backend/`: `entry_ticket.proto`(서명 티켓), `world_directory.proto`(RedeemEntryTicket, EndSession, lease 갱신)
- `contracts/events/envelope.proto`: 이벤트 봉투(event_id, aggregate_seq, request_id, payload)
- `contracts/internal/zone_link.proto`: LinkHello/Welcome, SessionAttach/Attached/Rejected/Evicted/Detach/Input/Output
- `contracts/CMakeLists.txt`: 패키지별 glob으로 C++ 생성. C# 생성은 1단계의 `Contracts` 프로젝트가 같은 파일을 읽는다

## 만난 문제
- 없음.

## 검증
- 루트 CMake 프로젝트가 아직 없어 CI의 C++ 잡은 probe에서 끝난다. 생성 코드 컴파일은 04(cpp-common)에서 루트 `CMakeLists.txt`와 함께 처음 돈다.

## 남긴 것
- 전투·획득·이전·거래 메시지는 그 단계에서 추가한다. 필드 번호는 그때도 재사용하지 않는다.
- `game/` 패키지의 C# 생성은 테스트 도구가 필요해질 때(1단계 통합 테스트 하네스)부터.

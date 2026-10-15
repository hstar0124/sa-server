# 08 (3단계) Gateway: 티켓 검증, 세션 상태 기계, Zone 링크, 디렉터리 클라이언트

## 배경과 목표
클라이언트는 Gateway에만 연결한다. Gateway는 입장 티켓의 서명과 만료를 확인하고, Backend에 티켓을 한 번 소비시키고(RedeemEntryTicket), 배정된 Zone에 세션을 붙이고, 이후 입력과 출력을 중계한다. 완료 조건: 세션의 모든 상태 전이가 I/O 없는 상태 기계로 단위 테스트되고, 티켓 검증이 키 회전·시계 오차·변조를 다루며, `gateway` 실행 파일이 TOML 설정으로 뜬다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 세션 로직 | 소켓 콜백에 분기 / I/O 없는 `SessionProtocol`이 효과(effect) 목록을 돌려주고 `ClientSession`이 수행 | 후자. 상태 전이(대기 → Redeem 중 → Attach 중 → Zone 안 → 종료)를 소켓 없이 전부 테스트한다 |
| 티켓 검증 | 매번 Backend 조회 / Gateway가 HMAC 서명·만료를 먼저 확인, Backend는 1회 소비만 | 후자(ADR-0005). 위조·만료 티켓은 Backend까지 가지 않는다. 키 ID로 회전, 허용 시계 오차는 설정값 |
| 입력 남용 | 무제한 / 연결당 초당 메시지 상한, 넘으면 kick | 상한(설정 `max_messages_per_second`). 1초 창의 토큰 버킷 |
| Zone 연결 | 세션마다 / Zone마다 영속 링크 하나에 세션 다중화, 끊기면 재연결과 해당 세션 종료 | 다중화(03의 `zone_link.proto`). 링크가 끊기면 그 Zone의 세션을 `ZONE_UNAVAILABLE`로 끝낸다 |
| 세션 종료 통보 | 없음 / 종료 시 Backend `EndSession` | 통보. 캐릭터당 세션 하나 규칙이 다음 입장을 막지 않게 |

## 구현
- `server/cpp/gateway/src/`: `entry_ticket_verifier`, `session_protocol`(상태 기계), `client_session`, `listener`, `zone_link`, `session_registry`, `directory_client`(gRPC), `gateway_config`, `main`
- `server/cpp/gateway/tests/`: 티켓 검증, 세션 상태 기계
- `deploy/compose/config/gateway.toml`
- 루트 CMake에 `server/cpp/gateway` 추가

## 만난 문제
- 없음. (로컬 디스크가 가득 차 ASan 링크가 한 번 실패했지만 저장소와 무관한 개발 머신 문제라 이슈로 남기지 않았다. 다시 만들 수 있는 빌드 산출물을 지워 해결.)

## 검증
- 커밋된 트리를 worktree에서: dev 빌드 + 테스트 96개(07까지 78 + 18) 통과, tidy 경고 0, ASan 96개 통과, clang-format 통과.

## 남긴 것
- 실제로 봇이 로그인 → 티켓 → Gateway → Zone까지 가는 end-to-end는 다음 피처(09 loadgen-client)에서 처음 돈다.
- WebSocket 전송은 3b단계(10).

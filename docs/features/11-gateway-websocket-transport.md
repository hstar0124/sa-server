# 11 (3b단계) WebSocket 전송: 브라우저가 같은 메시지로 Gateway에 붙는다

## 배경과 목표
다음 피처(웹 뷰어)는 브라우저에서 Gateway에 직접 붙는다. 브라우저는 원시 TCP를 열 수 없으니 WebSocket이 필요하다. 목표는 두 번째 프로토콜을 만드는 것이 아니라 **같은 Protobuf 메시지를 다른 틀로 나르는 것**이다. 완료 조건: Gateway가 두 번째 포트에서 WebSocket(RFC 6455)을 받고, 세션 상태 기계는 한 줄도 바뀌지 않으며, 기존 TCP 경로(end-to-end)가 그대로 통과한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 브라우저 경로 | 별도 프록시(websockify 등) / Gateway가 직접 WebSocket | 직접. 프로세스·홉이 하나 줄고, 프록시가 세션을 모르는 문제가 없다 |
| 구현 | Boost.Beast 등 라이브러리 / RFC 6455 필요한 부분만 직접 | 직접. 바이너리 프레임, 핸드셰이크, ping/pong, close만 필요하고 텍스트·분할 프레임은 게임 프로토콜이 쓰지 않으므로 거부한다. 의존성(vcpkg 캐시 키)이 늘지 않는다 |
| 전송 추상화 | 세션 코드에 `if (websocket)` / `MessageConnection` 인터페이스 아래 두 구현 | 인터페이스. 구현이 둘이라 과분할이 아니다. 세션 로직은 메시지만 본다 |
| 공통 부분 | 각 전송이 소켓을 직접 / `TcpByteStream` 하나가 읽기 루프·쓰기 큐·종료를 담당 | 하나. 쓰기 순서, 종료 중 쓰기, 단 한 번의 종료 통지처럼 틀리기 쉬운 부분을 한 곳에 둔다 |
| 포트 | 같은 포트에서 첫 바이트로 판별 / 포트 분리 | 분리(7777 TCP, 7778 WebSocket). 판별 로직과 그 오류를 없앤다 |

## 구현
- `server/cpp/common/`: `tcp_byte_stream`(공통 바이트 스트림), `message_connection`(인터페이스), `framed_connection`(길이 프리픽스, 이제 바이트 스트림 위), `websocket`(핸드셰이크·프레임 코덱), `websocket_connection`
- `server/cpp/gateway/`: 두 번째 리스너, 설정 `[websocket]`(`deploy/compose/config/gateway.toml`)
- Zone 링크와 봇 클라이언트는 새 `FramedConnection` 생성 방식에 맞춰 한 줄씩 바뀜
- README: 구조 그림의 WebSocket, "네트워크와 입장"의 전송 설명
- 이전 이력의 `docs: add the branch naming convention` 커밋은 가져오지 않았다. 같은 규칙이 01부터 CONTRIBUTING.md에 있다
- 덤: 0.1.0 릴리스 커밋이 피처 색인 표의 릴리스 열을 `| 3 || 0.1.0 |`처럼 칸 하나를 더 만들어 깨뜨렸던 것을 바로잡았다

## 만난 문제
- 없음.

## 검증
- 커밋된 트리를 worktree에서: dev 빌드 + 테스트 111개(10까지 103 + WebSocket 코덱 8: RFC 예제 accept 키, 핸드셰이크 판별, 마스킹, 길이 인코딩, 거부 사례), tidy 경고 0, ASan 111개, clang-format 통과.
- end-to-end(TCP 경로가 그대로인지): 봇 5개 전원 입장, 모순 0, 열린 세션 0, `RESULT: PASS`. Gateway 로그에 `listening on 0.0.0.0:7778`.

## 남긴 것
- 브라우저로 실제로 붙어 보는 것은 다음 피처(12 web-viewer)에서.

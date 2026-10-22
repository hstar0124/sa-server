# 48 (8단계 수정) 연결 생존 확인: 링크 하트비트와 무응답 기한

## 배경과 목표
이 프로젝트의 클라이언트는 Gateway(C++)에 TCP나 WebSocket으로 붙고, Gateway는 Zone마다 링크 하나로 그 Zone의 세션을 모두 나른다. 연결을 맺을 때와 닫을 때는 기한이 있다.
- 맺을 때: Hello 기한, 이슈 017의 링크 수립 기한
- 닫을 때: 닫기 대기 상한

하지만 맺어진 연결에서 상대가 **닫지 않고 사라지면** 남은 쪽은 TCP가 포기할 때까지(수 분~15분) 그 연결을 붙잡았다([이슈 019](../issues/019-silent-peers-held-until-tcp-gives-up.md)). 8단계에서 Zone이 둘이 되면서 Gateway가 지키는 링크도 둘이 됐다. 그래서 v0.6.0 전에 고친다.

완료 조건:
- 상대가 사라진 뒤 정해진 몇 초 안에 남은 쪽이 연결을 정리한다. 세션 정리와 재연결은 지금의 단절 처리를 그대로 탄다.
- 살아 있는 상대는 아무리 오래 조용해도(세션이 없어도) 끊기지 않는다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 생존 확인 수단 | TCP keepalive / 응용 계층 핑 | 응용 계층 핑. 계약에 `LinkPing`/`LinkPong`이 이미 있고 Zone은 답하고 있었다. 간격을 프로세스 설정으로 정하고 시작할 때 검증할 수 있다. keepalive는 커널·플랫폼마다 다르다 |
| "살아 있음"의 근거 | 퐁만 / 받은 것 무엇이든 | 무엇이든. 스냅샷이 오가는 링크는 퐁이 늦어도 살아 있다 |
| 누가 핑하나 | 양쪽 / Gateway만 | Gateway만. Zone은 퐁으로 답하고, 자기 쪽 침묵 기한으로 Gateway의 핑이 끊기는 것을 본다. 한 방향 핑으로 양쪽이 서로를 감시한다 |
| 클라이언트 기한 | 연결부터 / 첫 메시지부터 | 첫 메시지부터. 그 전은 Hello 기한(5초)이 맡는다. 클라이언트는 1초마다 핑하므로 10초면 아홉 번을 놓친 것이다 |
| 클라이언트 킥 사유 | 기존 `PROTOCOL_ERROR` / 새 `IDLE_TIMEOUT` | 새 사유. 프로토콜 위반이 아니다. 로그와 세션 종료 사유(`kick:KICK_REASON_IDLE_TIMEOUT`)에서 구분된다. Zone에 보내는 분리 사유도 `CLIENT_IDLE`로 따로 둔다 |
| 타이머 | 메시지마다 다시 걸기 / 기한에 한 번 깨어 확인 | 기한에 한 번. 초당 수십 메시지마다 타이머를 다시 걸지 않는다. 깨어났을 때 그사이 들은 것이 있으면 새 기한으로 다시 건다 |

## 구현
- 계약
  - `KICK_REASON_IDLE_TIMEOUT`, `DETACH_REASON_CLIENT_IDLE`
  - `LinkPing`/`LinkPong`에 하트비트 규칙 주석
- Gateway
  - `SessionProtocol`: `idle_timeout`, `OnIdleCheck(now)`, `IdleDeadline()`. 모든 클라이언트 메시지가 마지막 수신 시각을 갱신한다
  - `ClientSession`: 무응답 타이머. 연결 때 걸고, 깨어나면 상태 기계에 묻고, 기한으로 다시 건다
  - `ZoneLink`: 준비되면 하트비트 타이머. 받은 프레임마다 수신 시각을 갱신한다. 침묵이 기한을 넘으면 링크를 끊는다(재연결과 세션 정리는 기존 경로)
  - 설정: `protocol.idle_timeout_ms`(10000), `zone_link.heartbeat_interval_ms`(1000), `zone_link.silence_timeout_ms`(5000). 침묵 기한이 핑 간격 이하면 시작하지 않는다
- Zone
  - `GatewayLink`: 침묵 타이머. 받은 프레임마다 수신 시각을 갱신하고, 침묵이 기한을 넘으면 링크를 닫는다(호스트가 세션을 지우고 위치를 저장)
  - 설정: `gateway_link.silence_timeout_ms`(5000)
- README: Gateway 절의 시간 제한과 Zone 링크 생존

## 만난 문제
- [이슈 019](../issues/019-silent-peers-held-until-tcp-gives-up.md): 이 PR이 고치는 문제다.
- 첫 빌드에서 Gateway 링크 테스트가 실패했다. 테스트의 링크 설정에 새 하트비트 값이 없어(0) 준비되자마자 침묵으로 판정됐다. 운영 설정은 0을 거절하므로 테스트 설정을 채웠다.

## 검증
- C++: 테스트 277 → 285개 통과. Gateway 링크 2, Zone 링크 2, 상태 기계 2, 클라이언트 세션 1, 설정 1개를 더했다. 시간에 기대는 테스트 17개를 30번 반복해도 통과한다. dev·release·ASan, clang-tidy, clang-format 통과.
- 연습(dev 스택, 봇 5개, 40초)
  - Zone A를 `SIGSTOP`: 5.8초 뒤 Gateway가 "zone 1 silent"로 링크를 끊었다. 봇 5개가 `SERVICE_UNAVAILABLE`로 정리됐고, `SIGCONT` 뒤 같은 인스턴스로 다시 연결했다
  - Gateway를 `SIGSTOP`: 5초 뒤 두 Zone이 각각 "gateway link silent"로 링크를 닫았다. `SIGCONT` 뒤 Gateway가 두 링크를 다시 맺었다
  - loadgen을 `SIGSTOP`: 10초 뒤 세션 5개가 `IDLE_TIMEOUT`으로 끝났다. Backend의 세션 종료 사유는 `kick:KICK_REASON_IDLE_TIMEOUT`이고 열린 세션은 0이다
- 로컬 end-to-end(smoke·fighters·looters·travelers): 통과. 정상 실행에서 침묵 판정은 한 번도 없었다.

## 남긴 것
- Zone·Gateway의 Kubernetes readiness는 여전히 TCP 포트다. lease와 링크 상태를 반영하는 readiness는 장애 연습 단계에서 다룬다.
- 기한 값(1초·5초·10초)은 측정 전 제안값이다. 부하가 큰 Zone이 틱을 5초 넘게 멈추면 링크가 끊긴다. 그런 Zone은 이미 서비스하지 못하는 상태라고 본다.

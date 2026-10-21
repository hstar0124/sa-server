# 017 Gateway의 Zone 링크 연결에 기한이 없어 2분 넘게 멈춤

- 상태: 해결 (연결부터 LinkWelcome까지 기한 `zone_link.establish_timeout_ms`, 2026-10-21)
- 발견: 38 K3s 배포에서 end-to-end를 돌리다, 2026-10-21
- 영향: Gateway가 Zone보다 먼저, 또는 파드 네트워크가 준비되기 전에 연결을 시도하면 약 2분 동안 그 Zone에 세션을 보내지 못했다. 그동안 입장한 클라이언트는 `KICK_REASON_SERVICE_UNAVAILABLE`로 쫓겨났다

## 증상
K3s에 스택을 올리자마자 돌린 봇 6개가 모두 `KICK_REASON_SERVICE_UNAVAILABLE`로 쫓겨났다. Gateway 로그에는 zone 1에 대한 실패도 성공도 없다가, 시작하고 132초 뒤에야 다음이 찍혔다.

```
06:00:26 info  [gateway] gateway_instance_id=... zones=2
06:01:24 warning [gateway] session ... assigned to zone 1 which has no ready link
06:02:38 warning [gateway] zone 1 unreachable (connect: Connection timed out); retrying every 1000 ms
06:02:39 info  [gateway] zone 1 link ready
```

## 원인 분석
- 132초는 리눅스가 응답 없는 SYN을 포기하는 시간이다(`net.ipv4.tcp_syn_retries = 6`, 1+2+4+8+16+32+64초).
- 매니페스트는 Zone과 Gateway를 동시에 만든다. Gateway의 첫 연결 시점에 Zone 파드로 가는 경로가 아직 준비되지 않아 SYN이 버려졌다. 두 번째 시도는 곧바로 붙었다.
- `ZoneLink::Connect`는 resolve → connect → LinkHello/LinkWelcome을 비동기로 잇는데, 어느 단계에도 기한이 없었다. 실패해야 재시도하는 구조라, 응답이 없는 실패는 커널이 끝낼 때까지 재시도가 시작되지 않았다. 연결은 됐는데 Zone이 환영 메시지를 보내지 않는 경우에는 영원히 기다린다.
- 28(Gateway 리뷰 수정)에서 종료 중의 연결 취소는 넣었지만, 정상 상태의 시도 하나에 상한을 두는 일은 빠져 있었다.

## 원인
외부 프로세스와의 연결 수립에 시간 상한을 두지 않았다. compose에서는 Zone이 늘 먼저 떠 있어서 드러나지 않았다.

## 해결
- `ZoneLinkConfig.establish_timeout`: 한 시도가 주소 해석부터 LinkWelcome까지 걸릴 수 있는 시간. 기한이 지나면 연결 중이면 해석·연결을 취소하고 재시도를 예약하고, 핸드셰이크 중이면 연결을 끊어 재시도로 넘긴다.
- 설정 `[zone_link] establish_timeout_ms`(compose 기본 3000)는 필수 키이고 0이면 시작하지 않는다.

## 재발 방지
- 테스트: 연결은 받지만 환영하지 않는 Zone에 붙으면, 링크가 기한 뒤 포기하고 다시 시도한다. 기한을 1시간으로 바꾸면 실패한다.
- K3s end-to-end(`scripts/e2e-k3s.sh`)는 배포 직후에 돌려도 통과해야 한다.

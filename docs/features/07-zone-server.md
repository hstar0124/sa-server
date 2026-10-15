# 07 (2단계) Zone 서버 호스트: 틱 러너, Gateway 링크, 세션 표, lease 갱신, 프로세스 안 봇

## 배경과 목표
05의 순수 시뮬레이션을 실제 프로세스로 돌린다. 시계로 틱을 구동하고, Gateway가 붙을 링크를 열고, 세션과 플레이어를 묶고, Backend lease를 유지하며 잃으면 모든 세션을 내보낸다. 네트워크 없이도 밀도를 볼 수 있게 프로세스 안 봇(같은 Wanderer 두뇌)을 붙인다. 완료 조건: `zone_server`가 TOML 설정으로 뜨고, 호스트 로직(attach/detach/input/스냅샷 주기/lease 상실 시 추방)이 소켓 없이 단위 테스트로 고정된다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 스레드 모델 | 네트워크 스레드 + 시뮬레이션 스레드 + 큐 / io_context 하나에서 틱과 I/O를 모두 | 하나(설계 문서 7.1). 시뮬레이션 상태를 한 논리 스레드가 소유해 잠금이 없다. 밀도 문제가 생기면 그때 읽기 전용 단계만 나눈다 |
| 틱 구동 | `sleep` 루프 / Asio 타이머가 이전 마감 + 주기로 깨움 | 타이머. 깨어난 시각이 아니라 마감 기준이라 지터가 누적되지 않는다. 밀린 만큼은 `FixedStepScheduler`가 상한까지 따라잡는다 |
| 호스트와 링크 | 호스트가 소켓을 직접 / `LinkSink` 인터페이스 뒤에 | 인터페이스. 테스트는 벡터로 받는 가짜 싱크를 쓴다(구현이 둘이라 과분할이 아니다) |
| lease 상실 | 조용히 계속 / 모든 세션을 `LEASE_LOST`로 추방하고 플레이어 제거 | 추방. 권한 없는 Zone이 상태를 바꾸면 안 된다(설계 문서 14.3) |
| 설정 | 코드 상수 / `deploy/compose/config/zone-*.toml` + 환경 변수 | 설정 파일. 틱·스냅샷 주기, 세션 상한, 링크 포트, lease 주기, 프로세스 안 봇 수 |

## 구현
- `server/cpp/zone/server/`: `tick_runner`, `gateway_link`·`gateway_link_server`(LinkHello·Welcome, 세션 다중화), `zone_host`(세션 표, attach/detach/input, 스냅샷), `codec`(시뮬레이션 ↔ 계약 변환), `lease_keeper`, `directory_client`(gRPC), `local_bot_host`, `zone_server_config`, `main`
- `server/cpp/zone/server/tests/`: codec, zone_host(가짜 싱크)
- `deploy/compose/`: `docker-compose.yml`(PostgreSQL), `config/zone-a.toml`·`zone-b.toml`, `.env.example`
- zone 상위 CMake에 `server` 추가

## 만난 문제
- 없음.

## 검증
- 커밋된 트리를 worktree에서: dev 빌드 + 테스트 78개(05까지 68 + 10) 통과, tidy 경고 0, ASan 78개 통과, clang-format 통과.
- 이 PR부터 CI는 `develop`의 의존성 캐시를 쓴다(06에서 11분대).

## 남긴 것
- Gateway(08)가 붙어야 실제 세션이 생긴다. 지금은 프로세스 안 봇으로만 돈다(`SA_ZONE__DEV__LOCAL_BOTS=200`).
- `zone-b.toml`은 8단계(두 번째 Zone)까지 쓰지 않는다. 이전 이력 그대로 포함했다.

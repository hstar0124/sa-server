# 01 킬 랭킹: PostgreSQL 기준선과 Redis Sorted Set

## 배경
캐릭터별 처치 수는 EventWorker가 확정 이벤트로 만드는 통계 조회 모델(`character_projections`)에 있다. Backend는 그것으로 두 가지를 답한다. 상위 N명(리더보드)과 한 캐릭터의 순위("내 순위")다. 랭킹 저장소로 Redis를 들이기 전에, PostgreSQL만으로 얼마나 버티는지 먼저 잰다.

- 상위 N명: `(world_id, kills DESC, character_id)` 인덱스를 앞에서부터 N개 읽는다.
- 내 순위: 같은 인덱스에서 나보다 앞선 행을 센다. 순위가 낮을수록 셀 행이 많다.

## 조건
- 장비: AMD Ryzen 7 8845HS(16 스레드), 메모리 26 GiB, NVMe. PostgreSQL 17.6(compose, `shared_buffers=512MB`), Backend는 Debug 빌드 호스트 프로세스.
- 데이터: world 1에 벤치 캐릭터 N명, 처치 수는 0~4999 균등 난수(`scripts/bench/seed-rankings.sql`).
- 부하: `tools/httpbench`. 클라이언트 16개가 각자 응답을 받자마자 다음 요청을 보낸다(닫힌 루프). 10초.
- 명령: `scripts/dev-stack.sh up && scripts/bench/ranking.sh run <N>`. 측정 뒤 `scripts/bench/ranking.sh clean`.
- "내 순위"는 가장 낮은 순위의 캐릭터로 잰다. 가장 비싼 경우다.

## PostgreSQL 기준선 (2026-10-22)

| 경우 | N | 처리량(req/s) | p50 | p95 | p99 | 최대 |
|---|---:|---:|---:|---:|---:|---:|
| 상위 20 | 100,005 | 12,718 | 1.14 ms | 2.11 ms | 2.98 ms | 16.7 ms |
| 상위 20 | 1,000,005 | 13,832 | 1.07 ms | 1.85 ms | 2.35 ms | 10.7 ms |
| 최하위의 순위 | 100,005 | 615 | 25.27 ms | 32.41 ms | 39.92 ms | 57.5 ms |
| 최하위의 순위 | 1,000,005 | 62 | 249.75 ms | 291.04 ms | 325.29 ms | 365.1 ms |

원시 출력:

```
statistics rows in world 1: 100005; worst-ranked character: 98455; clients=16 duration=10s
top-20 (n=100005)            clients= 16 req/s=    12718 p50=   1.14ms p95=   2.11ms p99=   2.98ms max=    16.7ms errors=0
rank of worst (n=100005)     clients= 16 req/s=      615 p50=  25.27ms p95=  32.41ms p99=  39.92ms max=    57.5ms errors=0
statistics rows in world 1: 1000005; worst-ranked character: 998725; clients=16 duration=10s
top-20 (n=1000005)           clients= 16 req/s=    13832 p50=   1.07ms p95=   1.85ms p99=   2.35ms max=    10.7ms errors=0
rank of worst (n=1000005)    clients= 16 req/s=       62 p50= 249.75ms p95= 291.04ms p99= 325.29ms max=   365.1ms errors=0
```

## 해석 (기준선)
- 상위 N명은 규모와 무관하다. 인덱스 앞쪽 20행만 읽으므로 10만과 100만이 같다. 이 경우는 Redis가 필요 없다.
- 내 순위는 규모에 정비례한다. 10배 늘면 지연도 10배(25 → 250 ms), 처리량은 10분의 1(615 → 62 req/s)이다. 100만 캐릭터에서 최하위 플레이어 62명이 동시에 "내 순위"를 누르면 이 Backend 하나가 꽉 찬다.
- 다음 질문은 이 경우다. 순위를 원소 수의 로그에 비례해 답하는 Redis Sorted Set으로 옮겼을 때 같은 조건에서 어떻게 되는가.

## Redis Sorted Set을 들인 뒤 (2026-10-22)

같은 장비와 도구로, Backend를 PostgreSQL(`Rankings__Backend=Postgres`)과 Redis로 번갈아 띄워 연달아 쟀다. Redis는 EventWorker가 통계에서 채운 복사본이다. 벤치 스크립트가 시드 뒤 키를 지우고 다시 채워질 때까지 기다린 다음 잰다. 이름은 두 경우 모두 PostgreSQL에서 가져온다.

| 경우 | N | 저장소 | 처리량(req/s) | p50 | p99 |
|---|---:|---|---:|---:|---:|
| 상위 20 | 100,005 | PostgreSQL | 12,392 | 1.18 ms | 2.99 ms |
| 상위 20 | 100,005 | Redis | 13,634 | 1.05 ms | 2.90 ms |
| 상위 20 | 1,000,005 | PostgreSQL | 13,469 | 1.10 ms | 2.50 ms |
| 상위 20 | 1,000,005 | Redis | 15,619 | 0.96 ms | 2.01 ms |
| 최하위의 순위 | 100,005 | PostgreSQL | 679 | 22.66 ms | 35.67 ms |
| 최하위의 순위 | 100,005 | Redis | 47,422 | 0.29 ms | 0.88 ms |
| 최하위의 순위 | 1,000,005 | PostgreSQL | 62 | 251.23 ms | 343.86 ms |
| 최하위의 순위 | 1,000,005 | Redis | 47,240 | 0.29 ms | 0.87 ms |

원시 출력(Redis의 첫 "10만" 실행은 이미 100만이 시드된 뒤라 100만으로 찍혔고, 10만은 지운 뒤 따로 다시 쟀다):

```
== backend Postgres
statistics rows in world 1: 100005 (Redis copy refilled); worst-ranked character: 1096749; clients=16 duration=10s
top-20 (n=100005)            clients= 16 req/s=    12392 p50=   1.18ms p95=   2.14ms p99=   2.99ms max=    19.6ms errors=0
rank of worst (n=100005)     clients= 16 req/s=      679 p50=  22.66ms p95=  29.56ms p99=  35.67ms max=    54.0ms errors=0
statistics rows in world 1: 1000005 (Redis copy refilled); worst-ranked character: 1993479; clients=16 duration=10s
top-20 (n=1000005)           clients= 16 req/s=    13469 p50=   1.10ms p95=   1.92ms p99=   2.50ms max=    10.7ms errors=0
rank of worst (n=1000005)    clients= 16 req/s=       62 p50= 251.23ms p95= 287.06ms p99= 343.86ms max=   386.4ms errors=0
== backend Redis
statistics rows in world 1: 1000005 (Redis copy refilled); worst-ranked character: 1993479; clients=16 duration=10s
top-20 (n=1000005)           clients= 16 req/s=    13222 p50=   1.09ms p95=   2.07ms p99=   3.05ms max=    16.3ms errors=0
rank of worst (n=1000005)    clients= 16 req/s=    46713 p50=   0.30ms p95=   0.64ms p99=   0.88ms max=     9.7ms errors=0
statistics rows in world 1: 1000005 (Redis copy refilled); worst-ranked character: 1993479; clients=16 duration=10s
top-20 (n=1000005)           clients= 16 req/s=    15619 p50=   0.96ms p95=   1.55ms p99=   2.01ms max=    12.0ms errors=0
rank of worst (n=1000005)    clients= 16 req/s=    47240 p50=   0.29ms p95=   0.64ms p99=   0.87ms max=    12.1ms errors=0
== backend Redis (100k)
statistics rows in world 1: 100005 (Redis copy refilled); worst-ranked character: 2098055; clients=16 duration=10s
top-20 (n=100005)            clients= 16 req/s=    13634 p50=   1.05ms p95=   1.98ms p99=   2.90ms max=    19.5ms errors=0
rank of worst (n=100005)     clients= 16 req/s=    47422 p50=   0.29ms p95=   0.65ms p99=   0.88ms max=     8.1ms errors=0
```

## 해석 (Redis 이후)
- 최하위의 순위가 Redis에서 캐릭터 수와 무관해졌다. 10만과 100만이 같다(약 47,000 req/s, p99 0.9 ms). PostgreSQL 대비 10만에서 약 70배, 100만에서 약 760배다. 이 Backend에서는 이제 HTTP와 인증이 비용의 대부분이다.
- 상위 20은 두 저장소가 비슷하다(100만에서 Redis가 약 15% 빠르다). 상위 N명만 필요했다면 Redis를 들일 이유가 없었다.
- 대가는 쓰기 경로와 정합성이다. 통계 커밋 뒤 Redis 쓰기가 하나 더 생겼고, 둘은 한 트랜잭션이 아니다. 뒤처질 수 있는 세 경로(커밋 뒤 죽음, Redis 재시작, 쓰기 실패)를 EventWorker가 통계에서 다시 채워 고치고, Backend는 Redis가 비었거나 뒤처지거나 닿지 않을 때 PostgreSQL로 답한다. 설계 노트 09.

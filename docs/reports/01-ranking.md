# 01 킬 랭킹: PostgreSQL 기준선

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

## 결과 (2026-10-22)

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

## 해석
- 상위 N명은 규모와 무관하다. 인덱스 앞쪽 20행만 읽으므로 10만과 100만이 같다. 이 경우는 Redis가 필요 없다.
- 내 순위는 규모에 정비례한다. 10배 늘면 지연도 10배(25 → 250 ms), 처리량은 10분의 1(615 → 62 req/s)이다. 100만 캐릭터에서 최하위 플레이어 62명이 동시에 "내 순위"를 누르면 이 Backend 하나가 꽉 찬다.
- 다음 단계의 질문은 이 경우다. 순위를 O(log N)으로 답하는 Redis Sorted Set으로 옮겼을 때 같은 조건에서 어떻게 되는지, 그리고 그 대가(쓰기 경로와 정합성)가 무엇인지를 다음에 잰다.

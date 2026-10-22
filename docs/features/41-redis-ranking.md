# 41 (7단계) Redis 킬 랭킹: 측정된 병목만 옮기고, 복사본은 늘 통계로 되돌린다

## 배경과 목표
40에서 PostgreSQL만으로 킬 랭킹을 만들고 기준선을 쟀다. 상위 N명은 규모와 무관했지만, 한 캐릭터의 순위("내 순위")는 앞선 행을 세므로 캐릭터 수에 비례했다(100만에서 초당 62건). 이 PR은 그 질문만 Redis Sorted Set으로 옮기고, 같은 도구와 조건으로 다시 잰다. 랭킹은 EventWorker가 확정 이벤트로 만드는 통계(`character_projections`)의 복사본이고, 복사본이 비거나 뒤처져도 답이 틀리지 않아야 한다. 이것으로 7단계가 끝난다.

완료 조건: Redis와 PostgreSQL이 같은 데이터에서 같은 리더보드와 순위를 내고, 복사본이 뒤처지는 경로마다 복구가 테스트로 고정되며, 측정이 보고서 01에 있다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| Redis 범위 | 설계 기준의 네 용도(티켓, presence, Zone 조회, 랭킹) / 잰 병목만 | 랭킹만(ADR-0009). 다른 셋은 잴 부하가 없다 |
| 동점 순서 | 점수에 ID 접기(`처치 × 2³² + …`) / 점수는 처치 수, 멤버로 순서 | 멤버로. 멤버를 `long.MaxValue − ID`의 19자리 고정 폭으로 두면, Redis의 동점 역사전순이 "낮은 ID가 앞"이 된다. 점수에 접는 방식은 처치 약 200만, ID 32비트에서 순서가 깨진다 |
| 키 이름 | `ranking:{world}:kills` / 인코딩 버전 포함 | 버전 포함(`ranking:kills:v1:{world}`). 형식이 바뀌면 키가 바뀌어, 두 형식이 한 집합에 섞이지 않는다. 이 장비의 Redis에 이전 형식의 100만 멤버가 같은 키 이름으로 남아 새 멤버와 섞인 것을 보고 정했다 |
| 쓰기 | "+1" / 절대 합계를 `GT`로 | 절대 합계와 `GT`. 처치 수는 늘기만 하므로 늦은 옛 값이 새 값을 낮추지 못한다 |
| 뒤처짐 복구 | 키가 없을 때만 다시 채우기 / 시작할 때·키가 없을 때·쓰기가 실패했을 때 | 셋 다. 통계 커밋 뒤 Redis 쓰기 전에 죽으면 다시 온 이벤트는 Inbox가 중복으로 걸러 아무것도 쓰지 않는다. 쓰기가 한 번 실패해도 키는 남는다. 키가 없을 때만 채우면 이 둘은 영원히 고쳐지지 않는다 |
| 월드 목록 | 설정 / DB의 `worlds` | DB. 설정에 또 적지 않는다 |
| 읽기 실패 | 오류 / PostgreSQL로 대체 | 대체. 키가 없거나, 캐릭터가 집합에 없거나, Redis에 닿지 않으면 PostgreSQL로 답한다. Redis의 상태는 속도만 바꾼다 |
| 두 프로세스의 약속 | 각자 상수 / 공용 프로젝트 | 공용(`server/dotnet/Shared`): Redis 키와 인코딩, 집합 종류 이름 `character`(35에서 남긴 일) |

## 구현
- `server/dotnet/Shared`: `Ranking/RankingKeys`(키, 멤버, 점수), `Events/AggregateTypes`
- EventWorker
  - `ProjectionStore`가 갱신 뒤 처치 합계를 돌려준다(`ApplyResult`)
  - 소비자가 Redis에 쓰고, 실패하면 그 월드를 "뒤처짐"으로 표시
  - `Ranking/RankingWriter`(쓰기, 다시 채우기, 월드 목록)
  - `Ranking/RankingReconciler`(시작할 때 전부, 이후 키가 없거나 뒤처진 월드만)
  - 설정 `Redis:ConnectionString`, `Ranking:ReconcileInterval`·`RebuildBatchSize`
- Backend `Modules/Rankings`
  - `IRankingSource`: PostgreSQL(`RankingRepository`)과 `RedisRankingSource`(PostgreSQL 대체, 이름은 PostgreSQL에서)
  - 설정 `Rankings:Backend`(기본 Redis), `Redis:ConnectionString`(Redis일 때 필수)
- compose `redis`(7.4, 디스크 없음, 256 MB 상한, noeviction), K3s `data/redis.yaml`, Backend·EventWorker의 Redis 주소
- `scripts/lib/e2e.sh`: 이번 실행이 건드린 캐릭터마다 Redis 점수가 통계와 같은지(로컬·K3s 공용)
- `scripts/bench/ranking.sh`: 시드 뒤 Redis 복사본이 다시 채워질 때 기다리고, `clean` 뒤 복사본을 비운다
- `deploy/docker/dotnet.Dockerfile`: 게시하는 세 프로젝트만 복원(솔루션에 이미지 밖의 도구가 생겼다)
- 테스트
  - 단위 3개: 멤버 순서가 ID 순서, 키에 버전, 왕복과 범위
  - EventWorker 5개: 늦은 낮은 값 무시, 동점 순서, 사라진 키, 시작할 때의 복구, 실패 표시 뒤에만 복구. 소비 루프 테스트에 Redis 확인
  - Backend 통합 3개: 두 소스가 같은 답, 비었거나 뒤처진 Redis는 PostgreSQL로, 닿지 않는 Redis도 PostgreSQL로
- ADR-0009, 설계 노트 09, 보고서 01의 비교, README(전체 구조, 프로그램·데이터 표, 랭킹 절, 성능, 기술 선택, 배포 그림, 검증, 저장소 구성), CLAUDE.md 7단계 완료

## 옮기면서 고친 것
- 이전 점수 인코딩은 처치 수와 ID에 한계가 있었다(처치 약 200만, ID 32비트를 넘으면 예외). 멤버 인코딩으로 바꿔 한계를 없앴다.
- 이전 재구축기는 키가 없을 때만 채웠다. 커밋 뒤 죽은 경우와 쓰기 실패는 키가 남아 있는 한 고쳐지지 않았다. 셋 다 고친다.
- 이전 Backend는 키가 비면 빈 리더보드를, Redis가 죽으면 오류를 냈다. PostgreSQL로 답한다.
- 이전에는 Redis가 관리할 월드를 설정에 적었다. DB에서 읽는다.
- 이전 Backend 통합 테스트는 Redis 설정이 기본이라, 그 장비에 떠 있는 아무 Redis에 붙을 수 있었다. 테스트는 기본으로 PostgreSQL을 쓰고, Redis 테스트는 자기 컨테이너를 쓴다.
- 이전 벤치는 시드 뒤 Redis를 채우지 않아, Redis 측정이 대부분 PostgreSQL 대체 경로를 쟀을 수 있다. 다시 채워질 때까지 기다린다.
- 측정은 이 저장소에서 PostgreSQL과 Redis를 같은 세션에 연달아 다시 쟀다.

## 만난 문제
- 로컬 Redis(이전 저장소와 같은 컨테이너)에 예전 인코딩의 100만 멤버가 같은 키 이름으로 남아 있었고, 새 멤버가 그 안에 섞였다. 키에 인코딩 버전을 넣어 다시 일어나지 않게 했다. 예전 키는 복사본이라 지웠다.
- 통계가 없는 월드는 키가 생기지 않아, 재구축기가 주기마다 "0명 다시 채움" 로그를 남겼다. 0명이면 로그를 남기지 않는다.
- .NET 이미지 빌드가 솔루션 전체를 복원하다, 이미지 컨텍스트 밖의 `tools/httpbench`를 못 찾아 실패했다. 게시하는 프로젝트만 복원한다.
- 기본 페이지 검증 테스트가 Redis 설정 검증과 함께 실패해 `AggregateException`을 받았다. 메시지로 확인하게 고쳤다.

## 검증
- .NET: 빌드 경고 0, `dotnet format` 통과. 단위 10 → 13개, Backend 통합 54 → 57개, EventWorker 12 → 17개 통과.
- 측정(보고서 01): 최하위의 순위가 Redis에서 10만·100만 모두 약 47,000 req/s, p99 0.88 ms. PostgreSQL은 같은 세션에서 10만 679 req/s, 100만 62 req/s. 상위 20은 두 저장소가 비슷하다. 측정 뒤 벤치 데이터를 지웠다.
- 로컬 end-to-end(looters): PASS, 랭킹이 이번 실행 캐릭터의 통계와 같다.
- K3s 전체 주기(이미지 빌드 → 반입 → down → up → end-to-end): PASS, 랭킹 검사 포함.
- K3s에서 Redis 파드를 지웠다. 비어 있는 동안에도 리더보드는 같은 답을 냈고(PostgreSQL), 몇 초 뒤 EventWorker가 다시 채웠다.
- C++: 변경 없음.

## 남긴 것
- 이 PR로 7단계가 끝난다. 머지 뒤 0.5.0을 낸다.
- presence와 Zone 조회 캐시는 그것을 자주 묻는 기능이 생길 때 같은 방법으로 정한다.

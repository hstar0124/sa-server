# tools/loadgen

시드 기반 봇 클라이언트 (ADR-0006).

- `bot/` — 봇 두뇌 라이브러리 `sa::bot`. 관측(`Observation`)을 받아 이동 의도를 돌려준다. 네트워크를 모른다.
  - `Wanderer`: 맵 위의 무작위 지점을 골라 걸어간다. 같은 시드는 같은 경로.
  - `Fighter`: 보이는 몬스터 중 가장 가까운 것에 걸어가 사거리 안에서 주기적으로 공격한다. 없으면 배회.
  - `Looter`: 보이는 바닥 아이템에 걸어가 획득을 요청한다. 없으면 파이터처럼 사냥.
  - `Traveler`: 한동안 배회한 뒤 가장 가까운 포털 중심 가까이 가서 Zone 이동을 요청하고, 다음 Zone의 Welcome을 기다린다. 도착하면 그 Zone의 시계로 다시 배회한다.
- `client/` — Gateway에 TCP로 접속해 `contracts/game` 메시지로 두뇌를 구동하는 실행 파일. 프로파일(TOML)이 역할 수·시드·판정 기준을 정한다.
  - `profiles/smoke.toml`: 배회 봇 5개, 서로를 보는지.
  - `profiles/fighters.toml`: 파이터 5 + 배회 1, 40초 안에 몬스터 2마리 이상.
  - `profiles/looters.toml`: 루터 5 + 배회 1, 처치 2와 획득 1 이상.
  - `profiles/travelers.toml`: 트래블러 5 + 배회 1, 60초 안에 Zone 이동 5번 이상. 두 Zone이 모두 떠 있어야 한다.
- Zone 이동 요청은 Zone이 결정(수락·거절)할 때까지 같은 요청 ID로 다시 보낸다. 결과를 모른다는 답 뒤에도 마찬가지다. 그래야 Zone이 Backend에 같은 이전을 묻는다.
- 봇의 입력은 프로파일의 `seed`에서, 계정·캐릭터 이름은 따로 `identity_seed`에서 나온다. `identity_seed`가 없으면 실행마다 새로 정해 로그에 남긴다. 캐릭터에는 인벤토리와 마지막 위치가 쌓이므로, 지난 실행의 상태에서 출발하지 않게 하기 위해서다. 같은 계정을 다시 쓰려면 그 값을 프로파일이나 `SA_LOADGEN__BOTS__IDENTITY_SEED`로 고정한다([이슈 018](../../docs/issues/018-e2e-bots-reuse-characters-across-runs.md)).
- 보고서는 입장·스냅샷·정합성·전투(시전 수락/거절, 적중, 처치, 사망)를 집계하고 종료 코드로 판정을 돌려준다.

zone_server의 `[dev] local_bots`는 같은 두뇌를 네트워크 없이 프로세스 안에서 돌린다. 시뮬레이션 개발용이다.

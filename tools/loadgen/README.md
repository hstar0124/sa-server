# tools/loadgen

시드 기반 봇 클라이언트 (ADR-0006).

- `bot/` — 봇 두뇌 라이브러리 `sa::bot`. 관측(`Observation`)을 받아 이동 의도를 돌려준다. 네트워크를 모른다.
  - `Wanderer`: 맵 위의 무작위 지점을 골라 걸어간다. 같은 시드는 같은 경로.
  - `Fighter`: 보이는 몬스터 중 가장 가까운 것에 걸어가 사거리 안에서 주기적으로 공격한다. 없으면 배회.
  - 이후: Looter, Trader, Flapper (5~9단계).
- `client/` — Gateway에 TCP로 접속해 `contracts/game` 메시지로 두뇌를 구동하는 실행 파일. 프로파일(TOML)이 역할 수·시드·판정 기준을 정한다.
  - `profiles/smoke.toml`: 배회 봇 5개, 서로를 보는지.
  - `profiles/fighters.toml`: 파이터 5 + 배회 1, 40초 안에 몬스터 2마리 이상.
- 보고서는 입장·스냅샷·정합성·전투(시전 수락/거절, 적중, 처치, 사망)를 집계하고 종료 코드로 판정을 돌려준다.

zone_server의 `[dev] local_bots`는 같은 두뇌를 네트워크 없이 프로세스 안에서 돌린다. 시뮬레이션 개발용이다.

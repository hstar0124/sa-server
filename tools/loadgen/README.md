# tools/loadgen

시드 기반 봇 클라이언트 (ADR-0006).

- `bot/` — 봇 두뇌 라이브러리 `sa::bot`. 관측(`Observation`)을 받아 이동 의도를 돌려준다. 네트워크를 모른다.
  - `Wanderer`: 맵 위의 무작위 지점을 골라 걸어간다. 같은 시드는 같은 경로.
  - 이후: Fighter, Looter, Trader, Flapper (4~9단계).
- `client/` (3단계) — Gateway에 TCP로 접속해 `contracts/game` 메시지로 두뇌를 구동하는 실행 파일과 프로파일(TOML).

zone_server의 `[dev] local_bots`는 같은 두뇌를 네트워크 없이 프로세스 안에서 돌린다. 시뮬레이션 개발용이다.

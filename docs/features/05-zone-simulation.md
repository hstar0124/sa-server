# 05 (2단계) Zone 시뮬레이션: 이동·충돌·격자 AOI, game-data 로더, 시드 봇 두뇌

## 배경과 목표
Zone의 핵심은 틱마다 상태를 바꾸는 순수한 시뮬레이션이다. 소켓·시계·난수를 모르게 만들어 두면(ADR-0008) 같은 시드와 같은 입력으로 같은 결과가 나오고, 단위 테스트가 네트워크 없이 돈다. 완료 조건: 플레이어 이동과 AABB 장애물 충돌, 격자 기반 관심 집합(진입 40 m / 이탈 45 m 히스테리시스), 플레이어별 뷰 델타(spawned·updated·despawned)가 테스트로 고정되고, 맵과 이동 규칙은 `game-data/`에서 읽으며, 봇 두뇌(Wanderer)가 시드만으로 재현되는 이동 의도를 낸다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 틱 스케줄 | 가변 dt / 고정 스텝 + 따라잡기 상한 | 고정 스텝(20 Hz). 밀리면 상한까지만 따라잡고 나머지는 **버리고 센다**. 멈춘 프로세스가 수백 틱을 몰아 돌리는 소용돌이를 막는다 (설계 문서 7.2) |
| 충돌 | 폴리곤 / 축 분리 AABB | AABB에 축 분리(x 먼저, y 다음). 벽을 따라 미끄러진다. 지형이 단순한 단계라 충분 |
| 관심 관리 | 매 틱 전원 거리 비교(N²) / 균일 격자 셀 질의 | 격자. 반경 안 셀의 후보만 거리를 잰다. 진입·이탈 반경을 다르게 둬 경계에서 떨리지 않게 |
| 뷰 형식 | 전체 상태 / 관측자가 아는 집합과의 차이 | 차이. `known`과 `interest`가 정렬된 벡터라 set_difference 한 번 |
| 수치의 위치 | 코드 상수 / `game-data/rules/movement.toml`, `maps/zone-1.toml` | game-data(규칙 1). 로더가 구조를 검증한다(반경 순서, 장애물이 지도 안에, 스폰 지점 존재) |
| 봇 | 네트워크 클라이언트부터 / 두뇌만 먼저 | 두뇌만(ADR-0006). 프로세스 안 봇과 3단계 네트워크 봇이 같은 두뇌를 쓴다 |

## 구현
- `server/cpp/zone/sim/`: `fixed_step_scheduler`, `spatial_grid`, `map_data`(`MapData`, `MovementRules` + 검증), `zone`(`Zone::Tick`, `Submit`, `CollectView`, `RecomputeInterest`), `rng`, `duration`
- `server/cpp/zone/data/`: `game_data_loader`(TOML → `MapData`/`MovementRules`)
- `game-data/maps/zone-1.toml`, `game-data/rules/movement.toml`
- `tools/loadgen/bot/`: `Observation`/`Intent`, `Wanderer`
- 루트 CMake에 `server/cpp/zone`(sim, data)과 `tools/loadgen`(bot) 추가

## 만난 문제
- [006 의존성 캐시가 그것을 만든 PR에서만 보임](../issues/006-ci-dependency-cache-scoped-to-one-pull-request.md) — 이 PR의 CI가 의존성을 다시 2시간 빌드하기 시작했다. CI가 `develop` 푸시에도 돌아 공유 캐시를 저장하도록 이 PR에서 고쳤다.

## 검증
- 커밋된 트리를 worktree에서: dev 빌드 + 테스트 68개(04의 26개 + 42개) 통과, tidy 경고 0, ASan 68개 통과, clang-format 통과.
- 재현성: 같은 시드·같은 입력의 두 실행이 같은 위치를 낸다는 테스트(`ZoneMovement`), 히스테리시스 테스트(`ZoneInterest`), 군중 10명 시야 테스트.

## 남긴 것
- 틱을 실제 시계로 돌리는 `TickRunner`와 프로세스 안 봇 호스트는 07(zone-server)에서.
- 설계 노트 03의 밀집 추정은 봇 200개를 프로세스 안에서 돌려 보는 07에서 첫 수치를 얻는다.

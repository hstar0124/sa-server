# sa-server

MMORPG 서버 포트폴리오. `sa`는 Server Architecture의 약자다. 미니 PC 한 대에서 구현·운영하며 **정합성과 성능을 실측으로 입증**하는 것이 목표이고, 설계 문서(`docs/Server_Architecture.md`)에서 출발해 단계별로 만들며 각 단계의 결정(ADR), 이유(설계 노트), 측정(보고서), 작업 서사(피처 문서)를 함께 남긴다.

| 구성 | 언어 | 역할 |
|---|---|---|
| GameGateway | C++23 | 클라이언트 TCP 연결, 프레이밍, 세션, Zone 라우팅 |
| ZoneServer | C++23 | 20 Hz tick, 이동·전투·AI·AOI |
| GameBackend | C# / .NET 10 | 인증, 캐릭터, 자산·거래 트랜잭션, 소유권 |
| EventWorker | C# / .NET 10 | Outbox → Kafka 발행, 소비, 조회 모델·캐시 |
| loadgen | C++23 | 시드 기반 봇 클라이언트 (첫 클라이언트, 부하 생성기, 검증기) |

저장: PostgreSQL(기준) · Redis(측정으로 근거가 서는 용도에만) · Kafka(확정 이벤트). 배포: docker compose(개발) → K3s(시연·장애 테스트).

- 설계: [docs/Server_Architecture.md](docs/Server_Architecture.md)
- 결정 기록: [docs/adr/](docs/adr/)
- **설계 노트**(왜 이렇게 만들었나): [docs/design-notes/](docs/design-notes/) — 전송 계층 선택, UDP 유실 보정, 밀집 전투 확장, 입장 경로의 정합성, 순수 시뮬레이션
- 피처별 작업 기록: [docs/features/](docs/features/), 작업 중 만난 문제: [docs/issues/](docs/issues/)
- 작업 규칙: [CONTRIBUTING.md](CONTRIBUTING.md) (브랜치·PR 절차), [CLAUDE.md](CLAUDE.md) (코드 규칙·단계)

## 진행 상태

| 단계 | 내용 | 상태 |
|---|---|---|
| 0 | 저장소 골격, 툴체인, CI, 설계 문서와 첫 ADR | 진행 중 |
| 1 | Backend 인증·캐릭터, SQL 마이그레이션, 실제 PostgreSQL 통합 테스트 | 예정 |
| 2 | Zone 시뮬레이션: 이동·충돌·격자 AOI, 봇 두뇌 | 예정 |
| 3 | 입장 경로: 서명 티켓, 세션·소유권·lease, Gateway↔Zone 링크, 네트워크 봇, end-to-end | 예정 |
| 4~10 | 전투 → 자산·멱등성 → Outbox·Kafka·K3s → Redis → 두 번째 Zone·이전 → 거래 → 부하·장애·백업·보고서 | 예정 |

진행은 gitflow로 한다. 모든 변경은 `feature/*` 브랜치에서 PR로 `develop`에 들어가고, 단계가 끝나면 `release/x.y.z` PR로 `main`에 병합해 태그를 단다.

## 빌드

```bash
export VCPKG_ROOT=/opt/vcpkg
scl enable gcc-toolset-14 -- cmake --preset dev     # 프리셋: dev / release / asan / tsan / tidy
scl enable gcc-toolset-14 -- cmake --build --preset dev
ctest --preset dev
scripts/check-format.sh
```

C++ 트리와 .NET 솔루션은 다음 단계에서 생긴다. CI(`.github/workflows/ci.yml`)는 있는 것만 검사한다.

## 저장소 구성 (계획)

```
contracts/         Protobuf 계약 (game / backend / events) — 단일 출처 (0단계)
server/cpp/        common, gateway, zone(sim + server)
server/dotnet/     Contracts, GameBackend, EventWorker, Migrator, tests
database/          SQL 마이그레이션 (1단계)
game-data/         맵·스킬·아이템·몬스터 정의 (2단계)
deploy/            compose(개발), k3s(6단계부터)
observability/     대시보드·알림 (10단계)
tools/loadgen/     봇 클라이언트 (2단계 두뇌, 3단계 네트워크 클라이언트)
tests/integration  정합성 통합 테스트 (1단계)
docs/              설계, ADR, 설계 노트, 측정 보고서, 피처 문서, 이슈
```

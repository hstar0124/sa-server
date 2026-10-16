# sa-server

MMORPG 서버 포트폴리오. `sa`는 Server Architecture의 약자다. 미니 PC 한 대에서 구현·운영하며 정합성과 성능을 실측으로 입증하는 것이 목표이고, 단계별로 만들며 각 단계의 결정(ADR), 이유(설계 노트), 작업 서사(피처 문서)를 함께 남긴다. 이 README에는 **지금까지 만든 것**만 적는다. 단계가 끝날 때마다 그 단계의 구조·흐름·측정이 여기에 더해진다.

- 결정 기록: [docs/adr/](docs/adr/) · 설계 노트(왜 이렇게 만들었나): [docs/design-notes/](docs/design-notes/)
- 피처별 작업 기록(곧 진행 기록): [docs/features/](docs/features/) · 작업 중 만난 문제: [docs/issues/](docs/issues/)
- 작업 규칙: [CONTRIBUTING.md](CONTRIBUTING.md)(브랜치·PR 절차), [CLAUDE.md](CLAUDE.md)(코드 규칙)

## 전체 구조

```mermaid
flowchart TD
    C["클라이언트 / 봇"]
    G["C++ GameGateway"]
    Z["C++ ZoneServer"]
    B["C# GameBackend"]
    P["PostgreSQL"]

    C <-->|"게임 입력과 결과 (길이 프리픽스 TCP / WebSocket)"| G
    C <-->|"HTTP: 가입·로그인·캐릭터·입장 티켓"| B
    G <-->|"Zone마다 링크 하나에 세션 다중화"| Z
    G <-->|"gRPC: 티켓 소비·세션 종료"| B
    Z <-->|"gRPC: lease 갱신"| B
    B <--> P
```

| 프로그램 | 언어 | 책임 |
|---|---|---|
| GameGateway | C++23 | 연결, 프레이밍, 티켓 검증, 세션 상태 기계, Zone 라우팅, 유량 제한 |
| ZoneServer | C++23 | 지역 시뮬레이션(이동·충돌·관심 관리), 세션 표, Backend lease 유지 |
| GameBackend | C# / .NET 10 | 가입·로그인, 캐릭터, 입장 티켓과 세션·소유권(WorldDirectory). 모듈형 모놀리스 |
| loadgen | C++23 | 시드 기반 봇. 가입·로그인·캐릭터·티켓을 거쳐 Gateway로 입장해 걷고, 받은 스냅샷이 서로 모순되지 않는지 검사한다. 같은 두뇌(Wanderer)가 Zone 안의 프로세스 봇으로도 돈다 |

모든 메시지는 `contracts/`의 Protobuf가 단일 출처다(ADR-0004).

## 데이터의 기준

| 데이터 | 실행 중 변경 주체 | 기준 저장 |
|---|---|---|
| 위치·속도, 관심 집합 | Zone | 메모리. 재시작하면 초기화 |
| 계정, 캐릭터 | Backend | PostgreSQL |
| 입장 티켓, 세션, 캐릭터의 소유 Zone과 소유권 세대(owner_epoch), 프로세스 lease | Backend WorldDirectory | PostgreSQL |

규칙은 DB 제약으로도 지킨다. 티켓 소비는 조건부 UPDATE, 캐릭터당 활성 세션 하나는 부분 유일 인덱스, 소유권 변경은 같은 트랜잭션.

## 네트워크와 입장

| 구간 | 선택 |
|---|---|
| 클라이언트 ↔ Backend | HTTP(JSON) |
| 클라이언트 ↔ Gateway | 길이 프리픽스 TCP(봇·네이티브). 브라우저는 같은 Protobuf 메시지를 WebSocket 바이너리 프레임으로(두 번째 포트). 프레임 상한 64 KiB, 연결당 초당 메시지 상한 |
| Gateway ↔ Zone | Zone마다 영속 연결 하나에 세션을 다중화 |
| Zone·Gateway ↔ Backend | gRPC 비동기 호출, 모든 호출에 마감 시간 |

TCP는 초기 개발과 디버깅을 위한 선택이다(설계 노트 01·02).

입장 순서(ADR-0005, 설계 노트 04):
1. 클라이언트가 Backend에 로그인하고 캐릭터를 고르면 Backend가 소유를 확인하고 짧게 유효한 **서명 티켓**을 발급한다.
2. 클라이언트가 Gateway에 티켓을 낸다. Gateway는 서명·만료를 먼저 거른다.
3. Backend가 **한 트랜잭션에서** 티켓을 1회 소비하고, 같은 캐릭터의 다른 활성 세션을 거절하고, lease가 살아 있는 Zone을 배정한다.
4. Gateway가 세션을 그 Zone에 붙이고, 이후 입력과 출력을 중계한다.

Gateway의 세션 로직은 I/O 없는 상태 기계라서 모든 전이를 소켓 없이 테스트한다. 세션 로직은 전송을 모른다. TCP와 WebSocket은 같은 `MessageConnection` 인터페이스 뒤에 있고, 둘 다 한 가지 TCP 바이트 스트림 구현(읽기 루프, 순서 있는 쓰기 큐, 종료 통지)을 쓴다(ADR-0004).

## Zone의 한 틱

한 Zone의 상태는 하나의 논리 실행 흐름이 바꾼다. 시뮬레이션 코어는 소켓·시계·난수를 모르는 순수 라이브러리다(ADR-0008).

```mermaid
flowchart LR
    A["입력 적용<br/>(틱당 예산)"] --> B["이동·충돌<br/>(AABB, 축 분리)"] --> C["관심 집합 갱신<br/>(격자 + 히스테리시스)"] --> D["주기마다 스냅샷 델타"]
```

| 설정 | 값(측정 전 기본값) |
|---|---|
| 시뮬레이션 | 20 Hz(틱당 50 ms) |
| 스냅샷 | 10 Hz |
| 관심 반경 진입 / 이탈 | 40 m / 45 m |
| lease 갱신 / 만료 | 5 s / 15 s |

틱은 Asio 타이머가 이전 마감 기준으로 깨운다. 밀리면 상한까지만 따라잡고 나머지는 버리고 센다. 플레이어마다 "알려준 집합"과의 차이(spawned·updated·despawned)만 보낸다. 수치는 `game-data/`와 `deploy/compose/config/`에 있다.

## 소유권과 lease

- Backend가 캐릭터마다 소유 Zone 인스턴스와 증가하는 소유권 세대(owner_epoch)를 기록한다. Zone 프로세스는 재시작하면 새 인스턴스 ID를 받는다.
- Zone과 Gateway는 Backend lease를 주기적으로 갱신한다. lease가 끊긴 Zone에는 배정하지 않는다.
- Zone은 lease를 잃으면 모든 세션을 내보내고 플레이어를 지운다. 권한 없는 프로세스가 상태를 바꾸지 않는다.
- Gateway의 Zone 링크가 끊기면 그 Zone의 세션을 끝내고 Backend에 세션 종료를 알린다.

## 검증

**end-to-end**: `scripts/e2e-local.sh` 한 번으로 PostgreSQL(compose) + Backend + Zone + Gateway를 띄우고 봇 5개가 로그인 → 티켓 → Gateway → Zone 입장까지 TCP로 통과하는지, 서로를 보는지, 끝난 뒤 열린 세션이 0인지 판정한다(종료 코드가 판정). 봇의 월드 모델은 스냅샷마다 모순(모르는 엔티티의 갱신, 이미 보이는 엔티티의 재등장, 자기 자신이 시야에 등장, 입력 순번 역행)을 세고, 하나라도 있으면 실패다.

**정합성 테스트**: [tests/integration/README.md](tests/integration/README.md)의 시나리오를 실제 PostgreSQL(Testcontainers)에서 돌린다. HTTP 응답 코드가 아니라 DB 상태를 단언한다. 지금 통과하는 것: 티켓 1회 소비, 두 Gateway의 동시 Redeem 중 한 건만 승인, 캐릭터당 세션 하나, lease 만료 Zone 배정 거절.

## 기술 선택

| 기술 | 이유 |
|---|---|
| C++23(GCC 14), CMake, Asio | 네트워크·메모리·실시간 루프를 직접 구현하고 측정(ADR-0003) |
| C# / .NET 10, ASP.NET Core, Npgsql + Dapper | 인증·API·트랜잭션. SQL을 그대로 리뷰한다(ADR-0007) |
| Protobuf, gRPC | C++와 C#의 단일 계약, 타입 있는 내부 호출(ADR-0004) |
| PostgreSQL | 티켓·세션·소유권을 한 트랜잭션과 제약으로 검증 |

라이브러리 버전은 vcpkg baseline(C++)과 `Directory.Packages.props`·잠금 파일(.NET), 도구 버전은 CI 설정과 `global.json`에 고정한다.

## 빌드

```bash
export VCPKG_ROOT=/opt/vcpkg
scl enable gcc-toolset-14 -- cmake --preset dev     # 프리셋: dev / release / asan / tsan / tidy
scl enable gcc-toolset-14 -- cmake --build --preset dev
ctest --preset dev
dotnet build server/dotnet/GameServer.slnx
dotnet test  server/dotnet/GameServer.slnx          # 통합 테스트는 Docker(Testcontainers)
scripts/check-format.sh

# 로컬 실행용 비밀 값과 DB (저장소에 넣지 않는다)
cp deploy/compose/.env.example deploy/compose/.env   # 비밀번호 수정

# 전체 스택 end-to-end: 봇이 로그인 → 티켓 → Gateway → Zone 입장, 서로를 보는지 검증
scripts/e2e-local.sh

# 시뮬레이션만 돌려보기: 네트워크 없이 프로세스 안에서 봇 200개
SA_ZONE__DEV__LOCAL_BOTS=200 build/dev/server/cpp/zone/server/zone_server --config deploy/compose/config/zone-a.toml
```

## 저장소 구성

```
contracts/         Protobuf 계약 (game / backend / events / internal)
server/cpp/        common, gateway, zone(sim + data + server)
server/dotnet/     Contracts, GameBackend, Migrator, tests
database/          SQL 마이그레이션
game-data/         맵·이동 규칙
deploy/compose/    로컬 PostgreSQL과 프로세스 설정
tools/loadgen/     봇 두뇌(bot/)와 네트워크 봇 클라이언트(client/, 프로필)
scripts/           end-to-end, 형식 검사
tests/integration  정합성 시나리오 목록
docs/              ADR, 설계 노트, 피처 문서, 이슈
```

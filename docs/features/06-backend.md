# 06 (1단계) GameBackend: 인증·캐릭터·WorldDirectory, SQL 마이그레이션, 실제 PostgreSQL 통합 테스트

## 배경과 목표
영구 상태(계정, 캐릭터, 세션, 소유권)는 Backend의 PostgreSQL이 기준이다. Zone과 Gateway가 생기기 전에, 그들이 기댈 규칙(티켓은 한 번만 소비된다, 캐릭터당 세션은 하나, 소유 Zone은 하나, lease가 끊긴 프로세스는 권한이 없다)을 DB 제약과 트랜잭션으로 먼저 고정한다. 완료 조건: 모듈형 모놀리스의 Auth·Characters·WorldDirectory 모듈, DbUp 마이그레이션 0001~0003, 그리고 실제 PostgreSQL(Testcontainers)에서 동시 요청을 포함한 통합 테스트가 통과한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 구조 | 기능별 마이크로서비스 / 모듈형 모놀리스 | 모놀리스. 같은 경제 트랜잭션에 참여하는 모듈은 같은 연결·트랜잭션을 쓴다(ADR-0007) |
| 데이터 접근 | EF Core / Npgsql + Dapper + 손으로 쓴 SQL | 후자. 조건부 UPDATE, `INSERT ... ON CONFLICT`, `FOR UPDATE`를 SQL 그대로 리뷰한다 |
| 마이그레이션 | EF 마이그레이션 / SQL 파일 + DbUp(`Migrator`) | SQL 파일. 제약(UNIQUE, CHECK, 부분 인덱스)이 리뷰 대상이라 SQL이 원본이어야 한다 |
| 입장 | 세션 토큰 조회 / 서명 티켓 발급 + Redeem 1회 소비 | 서명 티켓(ADR-0005). 티켓 행의 소비는 조건부 UPDATE, 세션·소유권은 같은 트랜잭션 |
| 프로세스 권한 | 정적 설정 / lease(갱신 주기 5 s, 만료 15 s) | lease. 만료된 Zone에는 배정하지 않고, 인스턴스 ID가 바뀌면 다른 프로세스로 본다 |
| 테스트 | 인메모리 DB / Testcontainers의 실제 PostgreSQL | 실제 PG. 제약과 잠금은 진짜 DB에서만 검증된다(규칙 7) |
| 도구 버전 | SDK `10.0.x` / `global.json` 고정, 전이 의존성 잠금 | 고정. `packages.lock.json`, CI는 locked 모드 복원 |

## 구현
- `server/dotnet/GameBackend/Modules/Auth`(가입·로그인, PBKDF2, JWT, 자격 증명 시도 제한), `Modules/Characters`, `Modules/WorldDirectory`(티켓 발급·Redeem·EndSession, Zone·Gateway lease, 소유권)
- `server/dotnet/Contracts`(Protobuf C# 생성), `Migrator`(DbUp), `EventWorker`(골격만)
- `database/migrations/0001`~`0003`
- `server/dotnet/tests/GameBackend.Tests`(설정 검증 단위 4개), `GameBackend.IntegrationTests`(26개: 인증, 캐릭터, 티켓 1회 소비, 동시 Redeem 중 한 건만 승인, lease 만료, 소유권)
- `global.json`(SDK 10.0.112), `Directory.Build.props`의 `RestorePackagesWithLockFile`, 프로젝트별 `packages.lock.json`, CI `.NET` 잡의 `--locked-mode`

## 만난 문제
- 없음. (CI 의존성 캐시 문제는 05에서 다뤘다.)

## 검증
- 로컬: Release 빌드 경고 0(analyzers `latest-all`), 단위 4개 + 통합 26개 통과(실제 PostgreSQL 컨테이너), `dotnet format --verify-no-changes` 통과.
- CI `.NET` 잡이 이 PR부터 실제로 빌드·테스트한다. 러너에 Docker가 있어 Testcontainers가 돈다.

## 남긴 것
- 로컬 실행용 compose와 비밀 값 안내는 3단계(입장 경로) e2e에서.
- EventWorker는 이름만 있다. 6단계(Outbox)에서 채운다.

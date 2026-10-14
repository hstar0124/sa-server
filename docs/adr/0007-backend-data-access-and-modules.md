# ADR-0007: Backend는 단일 어셈블리 모듈형 모놀리스, Npgsql+Dapper, SQL 마이그레이션

- 상태: Accepted
- 날짜: 2026-10-12

## 맥락
문서 4.1절은 GameBackend를 모듈형 모놀리스로 두고 19절에 `server/dotnet/Modules/`를 둔다. 정합성 핵심은 조건부 UPDATE·UNIQUE·`INSERT ... ON CONFLICT` 같은 **명시적 SQL**이다. ORM이 SQL을 숨기면 리뷰어가 정합성 규칙을 코드에서 읽을 수 없다.

## 결정
1. **GameBackend는 단일 프로젝트**다. 모듈은 `Modules/<Feature>/`(Auth, Characters, Economy, WorldDirectory, …) 폴더이며 각 모듈 안에 endpoint, 도메인 규칙, SQL 접근을 함께 둔다. 모듈 간 접근은 `internal` 공개 API를 통해서만. 프로젝트 분리는 빌드 시간이나 배포 단위가 요구할 때 한다.
2. 데이터 접근은 **Npgsql + Dapper**. SQL은 모듈 안의 `*.sql` 상수 또는 명명된 메서드로 둔다. EF Core는 쓰지 않는다.
3. **마이그레이션은 순수 SQL 파일**(`database/migrations/NNNN_name.sql`)을 **DbUp** 기반 `Migrator` 콘솔이 적용한다. compose/K3s에서 Job으로 실행한다. 되돌리기 스크립트는 작성하지 않고 전진 마이그레이션만 둔다.
4. 트랜잭션 경계는 모듈의 use-case 메서드 하나다. Controller/endpoint에서 `NpgsqlTransaction`을 열지 않는다.
5. ID는 `readonly record struct`(예: `CharacterId`)로 감싸고 Dapper 타입 핸들러로 매핑한다.

## 결과
- 리뷰어가 10~11절의 SQL을 코드에서 그대로 확인할 수 있다.
- Dapper는 매핑 보일러플레이트가 있다. 모듈마다 작은 매핑 코드가 생기는 것을 허용한다.

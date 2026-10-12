# tests/integration

정합성 통합 테스트는 .NET 솔루션 안의 `server/dotnet/tests/GameBackend.IntegrationTests/`에 있다. 저장소 전체 컴파일 정책(Directory.Build.props)과 패키지 버전(Directory.Packages.props)을 상속받기 위해서다.

- 실제 PostgreSQL을 Testcontainers로 띄우고 `database/migrations/`를 적용한 뒤 실행한다.
- 설계 문서 21.1절의 표가 테스트 목록이다. 각 테스트는 HTTP 상태 코드가 아니라 **DB 상태(소유자 수, 잔액, 원장, Inbox)**를 단언한다.
- C++ 쪽(Zone·Gateway) 통합 테스트가 생기면 이 디렉터리에 둔다.

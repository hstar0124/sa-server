# SA — MMORPG Server (portfolio project)

지금까지 만든 것의 요약은 `README.md`(단계마다 더해진다). 설계 결정과 변경은 `docs/adr/`에 ADR로 남긴다.
README와 ADR이 충돌하면 **ADR이 우선**하고 README를 고친다.

## 무엇을 만드나
- `server/cpp/gateway` — C++23 (GCC 14). 클라이언트 TCP 연결, 프레이밍, 세션, Zone 라우팅.
- `server/cpp/zone` — C++23. `sim/`은 I/O를 모르는 순수 시뮬레이션(이동·충돌·격자 AOI), `data/`는 game-data 로더, `server/`는 tick 구동·네트워크 호스트.
- `server/dotnet/GameBackend` — .NET 10 모듈형 모놀리스. 인증, 캐릭터, 자산, 거래, WorldDirectory.
- `server/dotnet/EventWorker` — Outbox 발행, Kafka 소비, 조회 모델/캐시 갱신.
- `contracts/` — 모든 메시지의 단일 출처(Protobuf). C++과 C#은 생성 코드만 사용한다.
- `tools/loadgen` — 시드 기반 봇. `bot/`은 네트워크를 모르는 두뇌(`sa::bot::Wanderer` 등), `client/`(3단계)는 Gateway에 접속하는 실행 파일.

## 빌드
```bash
# C++ (vcpkg manifest, gcc-toolset-14). 프리셋은 dev / release / asan / tsan / tidy
scl enable gcc-toolset-14 -- cmake --preset dev
scl enable gcc-toolset-14 -- cmake --build --preset dev && ctest --preset dev
scl enable gcc-toolset-14 -- cmake --build --preset tidy      # clang-tidy, 커밋 전 필수
scl enable gcc-toolset-14 -- cmake --build --preset asan && ctest --preset asan
# C# (1단계부터)
dotnet build server/dotnet/GameServer.slnx
# 로컬 인프라 (1단계부터, deploy/compose)
docker compose -f deploy/compose/docker-compose.yml up -d
# 전체 스택 end-to-end (종료 코드 = 판정), 대화형 스택
scripts/e2e-local.sh [profile.toml]
scripts/dev-stack.sh up && scripts/viewer.sh
# K3s (6단계부터, deploy/k3s/README.md): 이미지 빌드·반입 후 data → migrate → game, 같은 검사로 확인
scripts/build-images.sh && scripts/k3s-stack.sh import && scripts/k3s-stack.sh up
scripts/e2e-k3s.sh tools/loadgen/client/profiles/looters.toml
```
`VCPKG_ROOT=/opt/vcpkg`. 빌드 산출물은 `build/`.

## 코드 규칙 (리뷰에서 걸리는 것)
1. **숫자 리터럴을 로직에 쓰지 않는다.** 튠값은 프로세스별 config 구조체 하나(TOML + env override, 시작 시 검증). 게임 수치는 `game-data/`. 프로토콜 상수는 `contracts/` 또는 `common/protocol_limits.hpp` 한 곳.
2. **ID는 강타입.** C++은 `StrongId<Tag>`, C#은 `readonly record struct`. 원시 int64/Guid를 인자로 넘기지 않는다. 시간은 `std::chrono` / `TimeSpan`.
3. **기능 단위로 응집.** Backend 모듈은 `Modules/<Feature>/` 아래에 endpoint·도메인 규칙·DB 접근을 함께 둔다. 기술 종류별 폴더(controllers/, services/, repositories/)로 한 기능을 흩지 않는다.
4. **과분할 금지.** 구현체가 하나인 인터페이스는 테스트 경계가 필요할 때만. 한 번만 쓰는 추상화 계층, 한 줄짜리 파일을 만들지 않는다. 반대로 500줄을 넘는 파일은 책임 분리를 검토한다.
5. **도메인은 I/O를 모른다.** 시뮬레이션·경제 규칙은 시계·난수·저장소를 주입받는다. 단위 테스트는 네트워크·DB 없이 돈다.
6. **오류는 타입으로.** C++은 `std::expected`/명시적 결과 enum, 예외는 프로그래밍 오류에만. C#은 Result 타입 또는 도메인 예외를 모듈 경계에서 변환. `bool` 반환으로 성공/실패를 표현하지 않는다.
7. **정합성 규칙은 DB 제약으로도 지킨다.** UNIQUE, CHECK, 조건부 UPDATE. 애플리케이션 검사만 믿지 않는다.
8. **경고는 에러.** C++: `-Wall -Wextra -Wpedantic -Werror`, clang-tidy, ASan/UBSan 프리셋. C#: `Nullable`, `TreatWarningsAsErrors`, analyzers `latest-all`.
9. **네이밍.** C++: 타입 `CamelCase`, 함수 `CamelCase`, 변수 `lower_case`, 멤버 `trailing_`, 상수 `kCamelCase`, 네임스페이스 `sa::<component>`. C#: .NET 관례, private 필드 `_camelCase`.
10. **커밋.** Conventional Commits (`feat(zone): ...`, `docs(adr): ...`). 한 커밋은 빌드되고 테스트가 통과한다.

## 작업 방식
- 코드를 쓰기 전에 어느 모듈에 어떤 타입이 생기는지 먼저 정리한다.
- 설계 결정(기술 선택, 경계 변경, 프로토콜 변경)은 ADR 먼저, 코드 다음.
- "왜 이렇게 만들었나"를 설명하는 토론·비교·수치 추정은 `docs/design-notes/`에 노트로 남기고 README에서 링크한다. 개발 절차가 아닌 어필 내용이 들어가는 곳이다.
- 문서 역할: README는 개요·구성·다이어그램·빌드 방법으로 **지금까지 만든 것의 큰 그림**을 주는 곳이다. 진행 상태 절은 두지 않고, 측정치는 그 단계에서 잰 것만 "성능과 개선"에 적는다. 설계 노트는 **그 문서만으로** 이해되어야 한다. 서두에 배경(프로젝트, 등장 구성 요소, 용어)을 2~4문장으로 두고, 다른 문서의 내용은 그 자리에 요지를 다시 적는다. 다른 문서의 절 번호나 CLAUDE.md 규칙 번호로 설명을 떠넘기지 않는다.
- Git: gitflow. `feature/*` → `develop`(`--no-ff`) → `release/x.y.z` → `main`(태그). 커밋은 컴포넌트·단계별로 나누고 메시지는 변경 사항만 쓴다. 공동 작업 표기(Co-Authored-By 등)는 넣지 않는다.
- 문서의 "제안값"은 측정 전까지 config 기본값일 뿐이다. 측정 결과는 `docs/reports/`.
- 테스트 없는 정합성 코드는 미완성이다. `tests/integration/README.md`의 표가 통합 테스트 목록이다.

## 비밀 값
`Auth:SigningKey`(base64 32바이트 이상)와 DB 비밀번호는 user-secrets 또는 환경 변수로만 준다. appsettings.json에는 빈 값만 있다.

## 단계 (ADR-0002)
완료: 0 골격·툴체인(저장소 도구, ADR-0001~0008, 설계 노트 01~05), 1 Backend 인증·캐릭터·WorldDirectory, 2 Zone 시뮬레이션·서버 호스트, 3 입장 경로(Gateway·네트워크 봇·`scripts/e2e-local.sh`), 3b WebSocket 전송·브라우저 뷰어(`tools/viewer`, `scripts/dev-stack.sh`), 4 전투(game-data 클래스·스킬·몬스터, 시뮬레이션의 스킬 검증·시전·투사체·몬스터 AI, 호스트의 결과·이벤트 전달, Fighter 봇, 뷰어 전투, 설계 노트 06), 5 자산·멱등성(items·drops·economy 데이터, 경제 스키마, Backend `Modules/Economy`: 처치 기록·획득·자산 조회, Zone 드롭 굴림·바닥 아이템·획득 예약과 정산, Looter 봇·`looters.toml`, 뷰어 획득, 설계 노트 07), 6 확정 이벤트·배포(Backend Outbox, EventWorker 발행자·소비자, `inbox_events`·`character_projections`, compose Kafka, 통계 API, 컨테이너 이미지 `deploy/docker`, K3s `deploy/k3s`·`scripts/k3s-stack.sh`·`scripts/e2e-k3s.sh`, CI release 빌드, 설계 노트 08).
다음: 7 Redis → 8 두 번째 Zone·이전 → 9 거래 → 10 부하·장애·백업·보고서

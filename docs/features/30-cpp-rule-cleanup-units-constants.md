# 30 C++ 규칙 정리 2: 시간은 chrono로, ID는 타입으로, 수치는 데이터로

## 배경과 목표
회고 리뷰([2026-10-20 회고 리뷰](../reviews/2026-10-20-retrospective.md))에서 남은 작업 규칙 위반을 PR 셋으로 나눠 고치고 있다. [29](29-cpp-rule-cleanup-errors-config.md)는 실행 중 결함이 될 수 있는 것을 다뤘다(결과를 버리는 `bool`, 조용한 설정 기본값, Zone 종료). 이 PR은 그 다음인 표현의 규칙을 다룬다.
- **시간은 `std::chrono`**: 밀리초 정수를 넘기지 않는다.
- **ID는 강타입**: 원시 정수나 문자열을 ID로 쓰지 않는다.
- **로직에 숫자를 쓰지 않는다**: 게임 수치는 game-data, 튠값은 설정, 프로토콜 상수는 계약이나 한 곳.

결함은 아니지만, 단위나 종류가 다른 값을 섞어도 컴파일러가 막지 못하는 자리들이다.

완료 조건: 위반마다 고치거나 남기는 이유를 적고, 바뀐 동작은 테스트로 고정한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 시간의 종류 | 모두 `steady_clock` / 모두 `system_clock` / 용도별 | 용도별. 기계를 넘는 시각(티켓 유효기간, Pong의 서버 시각, lease 만료)은 벽시계 `WallTime`이다. 한 프로세스 안의 간격(레이트 창)은 단조 시계다. 전에는 레이트 창이 벽시계를 써서, 시계가 바뀌면 창이 다시 열리거나 늘어날 수 있었다. 프로토콜의 Unix 밀리초는 `ToUnixMillis`와 `FromUnixMillis` 두 함수에서만 숫자로 바뀐다 |
| 상태 기계에 시간을 주는 방식 | 시계 객체 주입 / 도착 시각을 인자로 | 인자로(`ArrivalTime{monotonic, wall}`). 세션 상태 기계는 I/O가 없고 시간도 입력이다 |
| 클래스·스킬 ID의 자리 | 시뮬레이션 안 / 공통 어휘 | 공통(`common/ids.hpp`). 클래스는 Backend의 캐릭터 기록에서, 스킬은 클라이언트 요청에서 오는, 프로세스를 넘는 ID다. 봇도 같은 타입을 쓴다 |
| Zone 호스트의 세션 키 | 와이어 바이트 문자열 / `InstanceId` | `InstanceId`. 링크 경계에서 한 번 해석하고, 해석이 안 되면 잘못된 입력으로 센다 |
| 사거리 허용치·귀환 도착 거리·무리 반경 | 코드 상수 / 맵 파일 / 전투 규칙 | 전투 규칙(`movement.toml`의 `[combat]`). 무리 반경은 맵의 지형이 아니라 스폰을 놓는 규칙이고, 걷기 가능 검증이 같은 값을 받는다 |
| 개발 봇 값 | 코드 상수 / Zone 설정 | 설정(`[dev]`). 개발 봇을 켤 때만 읽는다 |
| 봇 파라미터의 기본값 | 헤더 기본값 유지 / 없앰 | 없앰. 프로필이 덮어쓰는 기본값은 프로필과 어긋나도 아무도 모른다. 테스트는 자기 값을 명시한다 |
| `TickRunner`의 시계 | 그대로 / 시계 함수 주입 | 주입(기본은 `steady_clock::now`). 타이머는 실제 시계로 깨우고, 밀린 정도만 주입한 시계로 잰다. 그래서 오래 멈춘 상황을 기다리지 않고 시험한다 |
| 계약의 0 | 필드마다 optional / 0은 없음이라고 적음 | 적음. proto3는 0과 보내지 않음을 구별하지 않는다. id와 순번은 1부터이고 0의 결과(요청 id 0은 거절)를 계약에 적었다. 스킬 0은 몬스터 기본 공격으로 예약하고, 콘텐츠에 스킬 0이 있으면 로드에서 거절한다 |

## 고친 것
| 위반 | 고친 것 |
|---|---|
| `now_ms`, `expires_at_ms`, `max_clock_skew_ms`, `DurationFromServerExpiry(uint64, uint64)`, `UnixMillisNow()` | `WallTime`, `std::chrono::milliseconds`, `ArrivalTime`, 레이트 창은 `steady_clock` |
| 티켓 키 id, 클래스·스킬 id, 봇의 월드·클래스·스킬 id와 틱, Zone 호스트의 세션 id | `TicketKeyId`, `common::ClassId`·`SkillId`, `common::WorldId`·`TickIndex`, `common::InstanceId` |
| `kRangeTolerance`, `kArriveDistanceM`, `kPackRingRadiusM`(Z8) | `combat.range_tolerance`, `combat.monster_home_arrive_m`, `combat.monster_pack_ring_radius_m` |
| `kDevBotParams`, `kDevBotClassId`(Z8) | Zone 설정 `dev.bot_class_id`, `[dev.wanderer]` |
| `kMinKeyBytes = 32` | `common::kHmacSha256Bytes`(RFC 2104: 해시 출력보다 짧은 키는 HMAC을 약하게 한다) |
| WebSocket 버전 `"13"`, 헤더 끝 `+ 4`, 관심 버퍼 `+ 4` | `kVersion`, `kHandshakeEnd.size()`, 여분 없이 |
| 공격 간격 변환의 `kMillisPerSecond` | chrono 나눗셈 |
| `TickRunner`가 시계를 직접 읽음(29에서 남긴 것) | 시계 주입과 따라잡기 테스트 |
| 계약에서 0의 뜻이 적혀 있지 않음(L10) | 클라이언트 메시지에 적음, `kMonsterBasicAttack`, 스킬 0 거절 |

남긴 것과 이유:
- `kReadChunkBytes`(바이트 스트림의 읽기 버퍼)는 동작을 바꾸지 않는 구현 크기다. 이름 붙은 한 곳에 있다.
- `kMaxHandshakeBytes`(WebSocket 요청 헤더 상한)는 전송 모듈의 상수로 `websocket.hpp` 한 곳에 있다. `protocol_limits.hpp`는 계약에서 오는 상수의 자리라 옮기지 않았다.
- 획득 요청 키의 HMAC 도메인 문자열은 키를 만드는 한 함수 안의 이름 붙은 상수다.
- 봇 캐릭터 이름의 16진 자릿수(13)는 이름 규칙(2~16자)에서 나온 값이고, 근거를 주석에 적었다.
- 뷰어의 스킬 상수(스페이스는 스킬 1, E는 스킬 2)는 이름과 출처(`skills.toml`)를 적은 한 곳에 있다. 정적 페이지라 game-data를 읽지 못한다. 서버가 Welcome에 클래스의 스킬 목록을 실으면 없앨 수 있지만, 그것은 프로토콜 변경이라 클라이언트 UI가 필요해질 때 한다.
- 스냅샷의 `template_id`는 세 종류(클래스, 몬스터, 스킬·아이템) 중 하나를 담는 계약 필드 그대로 정수다.

## 구현
- `common/wall_clock.hpp`: `WallClock`, `WallTime`, `WallNow`, `ToUnixMillis`, `FromUnixMillis`. `UnixMillisNow`는 없앴다
- `common/ids.hpp`: `ClassId`, `SkillId`
- `gateway`: `TicketKeyId`, `EntryTicketVerifier`(chrono), `ArrivalTime`과 레이트 창, 설정의 시계 오차
- `zone/sim`:
  - `CombatRules`에 세 값
  - `MapData::HomesOf`·`ValidateWalkable`이 반지름을 받음
  - `kMonsterBasicAttack`, 스킬 0 거절
- `zone/server`:
  - 세션 키 `InstanceId`
  - `DevBotConfig`
  - `TickRunner::Clock`, `TickRunner`를 호스트 라이브러리로
- 봇: 타입 있는 ID와 틱, 기본값 없는 파라미터, chrono 공격 간격
- 계약 `contracts/game/client_messages.proto`: 0의 뜻(주석만, 와이어 변화 없음)
- game-data `rules/movement.toml`, 설정 `zone-a/b.toml`의 `[dev]`
- README: 전투 규칙 행

## 만난 문제
- `TickRunner` 테스트가 끝나지 않았다. 가짜 시계를 시작 전에 앞당기는 바람에, 시작할 때 읽은 시각이 이미 앞당겨진 값이 됐다. 그래서 첫 깨어남에 할 틱이 없었고, 틱이 돌지 않으니 시계도 더 움직이지 않았다. 시작한 뒤에 앞당기게 바꾸고, 루프에 시간 상한을 두었다.
- Zone 호스트 테스트는 세션 이름("s1")을 그대로 세션 id로 썼다. id를 경계에서 해석하자 16바이트가 아니라서 모두 거절됐다. 이름을 올바른 id로 바꾸는 테스트 도우미(`SessionWire`)를 두었다.

## 검증
- C++: dev 224 → 227개 통과, clang-tidy 경고 0, ASan·UBSan 227개 통과, 형식 검사 통과. 커밋 5개가 각각 빌드되고 테스트를 통과한다.
- .NET: 계약 주석만 바뀌어 다시 생성된 코드로 빌드 경고 0.
- end-to-end 연속 실행. 기본: 봇 5개 전원 입장, 모순 0. 곧바로 전투: 시전 35건 모두 수락, DB 처치 영수증 9. 둘 다 PASS다.
- 웹 뷰어(WebSocket): 헤드리스 브라우저가 입장해 몬스터를 잡았다(모순 0, JS 오류 0).
- 개발 봇: `[dev]` 설정에서 읽은 값으로 Zone 안에 봇 50개가 떴다.

## 남긴 것
- Backend 규칙 정리: 강타입 ID, 결과 타입, 상수, 클래스 검증(B7), DB 제약(B10).
- 그 다음 이관을 재개한다(Looter 봇, 0.3.0 릴리스).

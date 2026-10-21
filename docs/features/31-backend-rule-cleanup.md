# 31 Backend 규칙 정리: 타입 있는 ID, 결과 타입, 모듈 경계, DB 제약

## 배경과 목표
회고 리뷰([2026-10-20 회고 리뷰](../reviews/2026-10-20-retrospective.md))에서 남은 작업 규칙 위반을 PR 셋으로 나눠 고쳤다. C++은 [29](29-cpp-rule-cleanup-errors-config.md)와 [30](30-cpp-rule-cleanup-units-constants.md)이 다뤘다. 이 PR은 마지막인 Backend다. Backend는 인증·캐릭터·입장(WorldDirectory)·자산(Economy) 모듈로 된 .NET 모듈형 모놀리스이고, 기준 상태를 PostgreSQL에 둔다.

다루는 규칙:
- ID는 `readonly record struct`, 시간은 `TimeSpan`
- 성공·실패를 `bool`이나 `null`로 알리지 않는다
- 로직에 숫자를 쓰지 않는다
- 한 기능의 코드는 그 모듈 안에 모으고, 다른 모듈의 내부(리포지토리)에 손대지 않는다
- 정합성 규칙은 DB 제약으로도 지킨다

리뷰가 미룬 두 가지도 여기서 처리한다. 캐릭터 생성의 클래스 검증(B7)과 코드로만 지키던 규칙의 DB 제약(B10)이다.

완료 조건: 위반마다 고치거나 남기는 이유를 적고, 바뀐 동작은 테스트로 고정한다. 제약은 Backend 코드를 거치지 않은 쓰기로 시험한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 콘텐츠 카탈로그의 자리(B7) | Economy 안에 두고 Characters가 참조 / 공용 인프라 | 공용(`Infrastructure/GameData`, 설정 `GameData:ContentDir`). 여러 모듈이 같은 game-data를 읽는다. 한 모듈의 내부를 다른 모듈이 참조하면 규칙 3을 어긴다 |
| 와이어의 ID | DTO·protobuf까지 강타입 / 경계에서 변환 | 경계에서 변환. JSON 본문과 protobuf 필드는 와이어 형식이라 숫자로 둔다. 경로 파라미터(`{worldId}`)는 `IParsable`로 바로 `WorldId`·`CharacterId`가 된다 |
| 결과 타입이 필요한 곳 | 모든 `bool`·`null` / 여러 결과가 섞이는 곳과 성공 여부 | 섞이는 곳. lease 갱신 거절의 `null`은 "다른 Zone의 id"와 "더 새 인스턴스가 대신함"을 한데 담아 결과 타입(`ZoneLeaseRenewal`)으로 바꿨다. 세션 종료의 `bool`도 `EndSessionOutcome`이 됐다. 이름이 실패를 말하는 Try 패턴(`TryInsertAsync`는 사용자명 중복 하나, 마이그레이터 인자 확인은 사용법 오류 하나)과 술어(`IsOwnedByAsync`)는 둔다. C++ 쪽(29)의 결정과 같다 |
| 모듈 사이 호출 | 다른 모듈의 리포지토리를 그대로 / 모듈이 내놓은 표면 | 표면. 입장 엔드포인트는 Characters의 리포지토리 대신 그 모듈이 내놓은 `CharacterOwnership`을 쓴다. 메서드가 하나뿐이지만 그것이 모듈 경계다. WorldDirectory도 Economy처럼 자기 엔드포인트와 gRPC를 모듈에서 붙인다 |
| 인벤토리 버전의 시작 | 코드 0 / DB 기본값 1 | 0. 코드는 늘 0으로 만들었고 Zone은 큰 버전만 받는다. 열 기본값을 0으로 맞추고 이름을 붙였다 |
| 제약을 거는 방식(B10) | 앱 검사만 / 제약 | 제약(마이그레이션 0005). 복합 외래 키, 원장 행 모양, 범위·길이 CHECK. 열거형 같은 문자열(`end_reason`, `command_results.outcome`)은 값이 코드와 함께 늘어나서 CHECK로 묶지 않았다. 범위(scope)와 원장 사유(reason)는 묶었고, 거래가 들어오면 그 마이그레이션이 값을 더한다 |

## 고친 것
| 위반 | 고친 것 |
|---|---|
| 캐릭터 생성이 클래스를 콘텐츠와 대조하지 않아, 어느 Zone도 세울 수 없는 캐릭터가 생긴다(B7) | 카탈로그가 클래스 파일을 읽고, 모르는 클래스는 422 `unknown-class` |
| 엔드포인트의 원시 `long` 경로 id, `int`·`long` Zone id·세대·클래스·템플릿 id(B11) | `ZoneId`, `OwnerEpoch`, `ClassId`, `ItemTemplateId`, `MonsterTemplateId`와 Dapper 처리기, 경로 id 바인딩 |
| lease 갱신 거절의 `null`, 세션 종료의 `bool`(B11) | `ZoneLeaseRenewal`(거절 사유를 행에 물어 구별하고 로그), `EndSessionOutcome` |
| 토큰 시계 오차 30초, 시도 창 1분·대기열 0, 요청 길이 128·24, 첫 세대 1(SQL 안의 숫자)(B11) | `AuthOptions.TokenClockSkew`, 이름 붙은 창과 대기열, `AccountRules`, `OwnerEpoch.First` |
| 위치 없는 드롭이 맵 원점(0, 0)에 떨어진다 | 잘못된 요청(InvalidArgument) |
| 입장 엔드포인트가 Characters 모듈의 리포지토리를 직접 쓴다 | `CharacterOwnership`, `UseWorldDirectoryModule` |
| 코드로만 지키는 규칙(B10) | 마이그레이션 0005 |

마이그레이션 0005가 거는 것:
- 캐릭터를 월드나 계정과 함께 가리키는 모든 행(티켓, 소유권, 세션, 인벤토리, 지갑, 아이템, 원장, 처치 영수증)이 그 캐릭터의 월드·계정과 맞아야 한다(복합 외래 키)
- 소유 Zone 인스턴스는 같은 월드·Zone ID의 lease다
- 세션은 사유와 함께 시작 뒤에 끝난다
- 세대·템플릿 id·Zone id는 양수, 칸 번호는 음수가 아니다
- 원장 행은 재화 이동이거나 아이템 이동 중 하나로 온전하다
- 요청 결과의 범위와 해시 길이, 지갑 통화 이름의 형식이 정해져 있다
- 이벤트 행의 월드와 순번이 올바르다
- 인벤토리 버전은 0에서 시작한다

## 구현
- `Infrastructure/GameData`: `ContentCatalog`(클래스 추가), `GameDataOptions`, `AddGameData`·`UseGameData`
- `Common/Ids.cs`: 새 id 타입, `WorldId`·`CharacterId`의 `IParsable`. `IdTypeHandlers`: `Int32WrapperHandler`
- `Modules/WorldDirectory`: `ZoneLeaseOutcome`·`ZoneLeaseRenewal`, `EndSessionOutcome`, 갱신 거절 로그, `UseWorldDirectoryModule`
- `Modules/Characters`: 클래스 검증, `CharacterOwnership`
- `Modules/Auth`: `TokenClockSkew`, `AccountRules`의 길이, 이름 붙은 창과 대기열
- `Modules/Economy`: 타입 있는 템플릿 id와 세대, 위치 없는 드롭 거절, `InitialInventoryVersion`
- `database/migrations/0005_consistency_constraints.sql`
- 스크립트·테스트 하네스: `GameData__ContentDir`
- 테스트: 모르는 클래스, lease 거절 사유, 위치 없는 드롭, Backend를 거치지 않는 쓰기 셋(티켓의 계정·월드, 음수 칸, 원장 행 모양)
- README: DB 제약, game-data 경로

## 만난 문제
- 원장 제약 테스트가 실패했다. 금액이 NULL인 재화 행이 들어갔다. `amount > 0`은 NULL과 비교하면 "모름"이 되고, CHECK는 "모름"을 통과시킨다. 분기마다 필요한 열을 `IS NOT NULL`로 명시하고 그 이유를 마이그레이션에 적었다.
- 제약을 걸자 기존 테스트 둘이 실패했다. 하나는 만료 티켓을 만들려고 만료 시각만 과거로 옮겨, 발급 시각보다 앞서게 했다(새 수명 제약 위반). 다른 하나는 경합을 만들려고 세대 0인 세션 행을 넣었다(새 양수 제약 위반). 둘 다 테스트가 실제로 생길 수 없는 행을 만들고 있었다. 발급 시각도 함께 옮기고, 세대를 1로 바꿨다.
- Python으로 C# 파일을 고치다가 C#의 원시 문자열(`"""`)이 Python 문자열을 끊었다. 따옴표를 바꿔 다시 적용했다.

## 검증
- .NET: 빌드 경고 0, `dotnet format` 통과. 단위 10개, 통합 42 → 48개 통과.
- 마이그레이션 0005가 몇 주 동안의 실행 데이터가 든 개발 DB에도 위반 없이 적용됐다(end-to-end 스크립트의 마이그레이터).
- end-to-end 연속 실행. 기본: 봇 5개 전원 입장, 모순 0. 곧바로 전투: DB 처치 영수증 13. 둘 다 PASS다.
- C++: 변경 없음.

## 남긴 것
- 먼저 만든 이력의 마이그레이션 0005~0008(Inbox·조회 모델, 랭킹 인덱스, 체크포인트·이전, 거래)은 다시 옮길 때 0006~0009가 된다. 거래 마이그레이션은 `command_results.scope`와 원장 `reason` CHECK에 값을 더해야 한다.
- 회고 리뷰의 지적은 송신 역압·밀집 처리량 피처(G3, Z3, Z6)와 보안 단계(내부 gRPC 인증, 계정 단위 로그인 시도 상한)로 넘긴 것만 남았다.
- 다음은 이관 재개다. Looter 봇과 0.3.0 릴리스.

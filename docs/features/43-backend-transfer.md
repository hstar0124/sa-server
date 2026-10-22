# 43 (8단계) Backend 이전: 체크포인트, 한 트랜잭션의 Zone 이전, 체크포인트를 보는 배치

## 배경과 목표
8단계는 두 번째 Zone과 소유권 이전이다. 캐릭터의 소유자는 Backend의 소유권 행(`character_ownership`: Zone 인스턴스와 세대 `owner_epoch`)이 정하고, Zone은 lease로 살아 있음을 알린다. 42에서 계약(`SaveCheckpoint`, `TransferCharacter`)과 테이블(`character_checkpoints`, `zone_transfers`)을 깔았다. 이 PR은 Backend가 그 둘을 실제로 처리하게 하고, 입장할 때 마지막 위치를 보고 배치하게 한다. 결정은 ADR-0010을 따른다. 이전은 Backend의 조건부 트랜잭션 하나이고, Gateway가 세션을 다시 붙인다(44).

완료 조건:
- 이전 소유자의 늦은 저장은 거절된다.
- 이전은 재시도와 동시 중복에도 한 번만 일어난다.
- lease가 끝난 Zone은 아무것도 하지 못한다.
- 다시 들어오면 마지막 위치에서 시작한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 체크포인트 덮어쓰기 | 마지막 쓰기가 이긴다 / 세대와 버전 비교 | 세대와 버전. 새 세대는 언제나 이기고, 같은 세대는 버전이 커야 한다. 조건부 upsert 하나라 경쟁이 없다 |
| 이전 | 출발·도착 두 번의 호출 / 트랜잭션 하나 | 하나(ADR-0010). 한 커밋에서 다음을 모두 한다. 출발 권한 확인(`FOR UPDATE`), 도착 Zone의 살아 있는 인스턴스, 소유권 이동과 세대 + 1, 도착 지점 체크포인트(버전 0), 열린 세션의 세대, 결정 기록 |
| 이전 재시도 | 클라이언트 요청 ID / 출발 Zone이 만든 이전 ID | 이전 ID가 `zone_transfers`의 기본 키. 다시 물으면 처음 결정이 돌아온다. 동시에 두 번 오면 뒤에 커밋하는 쪽이 키 충돌을 보고 저장된 결정을 읽는다 |
| 출발 Zone의 lease | 소유권 행만 확인 / lease도 확인 | lease도. 처치·획득과 같은 규칙이다. lease가 끝난 Zone은 소유권 행이 아직 자기를 가리켜도 권한이 없다 |
| 체크포인트의 Zone | 요청이 준 값 / 소유권 행과 맞아야 | 맞아야. 소유 인스턴스는 한 Zone ID만 시뮬레이션한다. 다른 Zone의 위치를 저장하려는 요청은 권한이 없는 것으로 본다 |
| 배치 | 가장 낮은 살아 있는 Zone / 소유 Zone → 체크포인트의 Zone → 가장 낮은 Zone | 후자. 체크포인트의 위치는 그 Zone으로 들어갈 때만 쓴다 |
| 권한 타입 | 모듈마다 / 공용 | 공용(`Common/ZoneAuthority`, proto 변환 포함). 경제와 이전이 같은 권한을 싣는다 |

## 구현
- `Common/ZoneAuthority.cs`: 권한 레코드와 `TryFrom`(proto). Economy의 사본과 변환 함수를 지웠다
- `Modules/WorldDirectory/Placement.cs`(`PlacementRepository`): `SaveCheckpointAsync`, `TransferAsync`, 결과 열거형
- `Modules/WorldDirectory/Ids.cs`: `TransferId`
- `WorldDirectoryRepository`: 입장 때 체크포인트를 읽어 배치하고, 같은 Zone이면 응답에 싣는다
- `WorldDirectoryGrpcService`
  - `SaveCheckpoint`·`TransferCharacter`
  - 입장 응답의 체크포인트
  - 유한하지 않은 좌표는 `INVALID_ARGUMENT`
- 테스트 하네스: `DirectoryClient.SaveCheckpointAsync`·`TransferAsync`, `EnteredCharacter`에 계정과 세션
- 통합 테스트 6개
  - 체크포인트 버전과 늦은 저장
  - 한 번의 이전, 재시도, 세션 세대, 재입장 위치
  - 동시에 두 번 온 이전
  - 도착 없음·같은 Zone
  - lease가 끝난 Zone
  - 다른 Zone의 체크포인트와 유한하지 않은 좌표
- README: 소유권 절의 체크포인트·이전·배치, 정합성 테스트 목록

## 옮기면서 고친 것
- 이전 이력은 출발 Zone의 lease를 보지 않아, lease가 끝난 Zone도 소유권 행만 맞으면 체크포인트를 저장하고 캐릭터를 넘길 수 있었다. 처치·획득과 같이 lease를 확인한다. 새 테스트는 lease를 보지 않게 바꾸면 실패한다.
- 이전 체크포인트 저장은 요청이 준 Zone ID를 그대로 썼다. 소유권 행의 Zone과 다르면 거절한다.
- 이전 이력에서는 유한하지 않은 좌표가 테이블 CHECK(42)에 걸려 내부 오류가 됐다. 경계에서 `INVALID_ARGUMENT`로 돌려준다.
- 세션 세대를 맞추는 UPDATE에 월드 조건이 빠져 있었다.
- 이전 이력은 `ZoneAuthority`를 WorldDirectory에 한 벌 더 정의하고 세대를 원시 `long`, Zone을 `int`로 받았다. 공용 타입 하나와 `OwnerEpoch`·`ZoneId`로 바꿨다.
- 동시에 두 번 온 이전 테스트를 더했다(이전 이력에는 순차 재시도만 있었다).

## 만난 문제
- `ZoneAuthority`를 공용으로 옮기자 gRPC 서비스에서 proto의 같은 이름과 모호해졌다. 두 서비스 파일에서 별칭으로 공용 타입을 가리킨다.

## 검증
- .NET: 빌드 경고 0, `dotnet format` 통과. 단위 13개, Backend 통합 59 → 65개, EventWorker 17개 통과.
- 로컬 end-to-end(looters): PASS. 입장 경로가 체크포인트를 읽게 된 뒤에도 같다.
- C++: 변경 없음.

## 남긴 것
- Gateway의 재부착(44), Zone의 포털·출발·체크포인트 주기(45).
- 도착 Zone이 커밋 뒤에 죽은 경우의 복구는 재입장이 맡는다(ADR-0010). 이 PR의 배치 규칙이 그 바탕이다.

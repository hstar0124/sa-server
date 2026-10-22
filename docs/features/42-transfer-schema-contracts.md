# 42 (8단계) 지역 이전의 바탕: ADR-0010, 계약, 체크포인트·이전 스키마, 포털과 두 번째 맵

## 배경과 목표
8단계는 두 번째 Zone과 소유권 이전이다. 지금까지 월드에는 Zone 하나(초원)뿐이었다. 캐릭터의 소유자는 Backend의 소유권 행(`character_ownership`: Zone 인스턴스와 세대 `owner_epoch`)이 정한다. 8단계에서는 캐릭터가 포털로 다른 Zone에 가고, 그때 소유권이 정확히 한 번 옮겨 가야 한다. 이 단계는 PR 여섯으로 나눈다. 이 PR은 다음 PR들이 딛고 설 바탕을 깐다.
- 설계 결정(ADR-0010)
- 계약
- 스키마
- 콘텐츠

완료 조건: 이전과 체크포인트의 계약과 테이블이 있고, 두 맵이 포털로 이어지며, 맵·DB 규칙이 테스트로 고정된다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 이전 절차 | 설계 기준의 여덟 걸음(도착 Zone 사전 준비, `Transferring` 상태) / Backend 트랜잭션 하나 + Gateway 재부착 | 후자(ADR-0010). 지금 Zone 사이를 오가는 상태는 위치와 생명력뿐이라 도착 Zone이 준비할 것이 없다. 쓰지 않는 상태 열은 만들지 않는다 |
| 권한 메시지 | 경제 계약 안에 / 공용 `authority.proto` | 공용. 처치·획득에 이어 체크포인트·이전도 같은 권한(Zone 인스턴스, 세대)을 싣는다 |
| 체크포인트 위치 | 따로 테이블 / 소유권 행에 열 추가 | 따로(`character_checkpoints`). 소유 Zone이 자주 쓰고, 세대와 버전으로 늦은 저장을 거절한다. 소유권 행의 잠금과 섞지 않는다 |
| 이전 기록 | 결과만 / 결정마다 한 행 | 결정마다 한 행(`zone_transfers`, 기본 키 = 출발 Zone이 만든 16바이트 이전 ID). 같은 ID로 다시 물으면 처음 결정을 돌려준다 |
| 포털의 도착 지점 검사 | 로더에서 / 도착 Zone이 받을 때 | 도착 Zone이 받을 때(45). Zone은 자기 맵만 읽는다. 대신 배포되는 두 맵은 서로의 도착 지점에 설 수 있는지 테스트로 확인한다 |

## 구현
- `docs/adr/0010-zone-transfer-as-one-transaction.md`
- 계약
  - `backend/authority.proto`(`ZoneAuthority`, 경제에서 옮김)
  - `world_directory.proto`: `SaveCheckpoint`, `TransferCharacter`, `Checkpoint`, 결과 열거형, 입장 응답의 체크포인트
  - `server_messages.proto`: Welcome의 포털 목록, 이동 거절 사유
  - `zone_link.proto`: `SessionAttach.position`
- `database/migrations/0008_transfers.sql`: `character_checkpoints`, `zone_transfers`
- 콘텐츠
  - `maps/zone-2.toml`(고원: 능선 둘, 바위, 늑대와 슬라임)
  - 두 맵의 포털(초원 동쪽 끝 ↔ 고원 서쪽 끝)
  - `deploy/compose/config/zone-b.toml`이 고원을 읽는다
  - 콘텐츠 버전 `2026.10.22-1`
- Zone `sim::Portal`·`MapData::FindPortal`, 포털 검증, 로더의 `[[portals]]`
- 테스트
  - 맵 3개: 포털 검증(NaN 반지름 포함), 닿을 수 없는 포털
  - 로더 1개: 배포 맵이 포털로 이어지고 도착 지점에 설 수 있다
  - DB 제약 2개: 체크포인트 좌표·세대·FK, 이전 행의 모양
- README: 데이터의 기준 표, 콘텐츠 표의 포털, 걸을 수 있는 곳 검사. `game-data/README.md`

## 옮기면서 고친 것
- 이전 마이그레이션은 캐릭터를 월드와 따로 참조했다. 0005의 규칙대로 (월드, 캐릭터) 복합 FK로 건다.
- 이전 체크포인트에는 좌표 검사가 없었다. NaN과 무한대를 거절한다. PostgreSQL은 `NaN = NaN`을 참으로 보므로 자기 자신과 비교해서는 막을 수 없어, 값을 이름으로 적었다.
- 이전 이전 기록의 CHECK는 실패한 이전에 도착 인스턴스·세대가 들어 있어도 받아들였다. 이제 성공한 이전만 도착을 갖고, 그 세대는 출발보다 크다. 세대·Zone ID·인스턴스 ID 길이도 검사한다.
- 이전 포털 검증 `radius_m <= 0`은 NaN 반지름을 통과시켰다. 포털이 장애물 안이나 맵 가장자리에 붙어 닿을 수 없는 경우도 거절한다.
- 맵이 바뀌어 시뮬레이션 결과가 달라지므로 콘텐츠 버전을 올렸다. 이전 이력은 올리지 않았다.
- 계약과 ADR 주석의 설계 문서 절 번호는 지우고 이유를 그 자리에 적었다.

## 만난 문제
- 계약을 옮기자 거절 사유 열거형이 충돌했다. 이 저장소가 31에서 더한 `REJECT_CODE_NOT_OWNER = 24`를 지키고, 이동 사유(30~33)를 더했다.
- 맵 검증 함수가 tidy의 복잡도 기준을 넘어 포털 검증을 따로 떼었다.

## 검증
- C++: dev 237 → 240개 통과(release에서도), clang-tidy 경고 0, ASan 240개 통과, 형식 검사 통과.
- .NET: 빌드 경고 0, `dotnet format` 통과. 단위 13개, Backend 통합 57 → 59개, EventWorker 17개 통과.
- 로컬 end-to-end(smoke, looters): 마이그레이션 0008이 적용되고 PASS.

## 남긴 것
- Backend의 체크포인트·이전 구현(43), Gateway 재부착(44), Zone의 포털·출발(45), 봇과 뷰어(46), Zone B 배포와 문서(47).
- 포털이 자기 Zone을 가리키지 않는지는 Zone 서버가 시작할 때 자기 `zone_id`로 확인한다(45).

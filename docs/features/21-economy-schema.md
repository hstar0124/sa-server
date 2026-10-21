# 21 (5단계) 경제 스키마: 아이템·지갑·영수증·요청 결과를 제약으로

## 배경과 목표
5단계는 자산과 멱등성이다. 몬스터를 잡으면 드롭과 골드가 생기고, 바닥의 아이템을 주워 인벤토리에 넣는다. 이때 아이템이 두 사람에게 가거나, 같은 처치가 두 번 보상되거나, 재시도가 효과를 두 번 내면 안 된다. 이 PR은 그 첫 조각으로 **데이터와 계약**을 들인다. 아이템·드롭·경제 규칙 데이터, Zone과 Backend 사이의 Economy 계약, 자산 규칙을 테이블 제약으로 거는 마이그레이션 0004, Zone 로더다. 이것을 쓰는 Backend 모듈과 Zone 쪽 드롭·획득은 다음 PR들이다. 완료 조건: 마이그레이션이 실제 PostgreSQL에 적용되고, 실린 데이터가 로더 검증을 통과하며, 잘못된 드롭 표는 거절된다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 규칙을 어디서 지키나 | 애플리케이션 검사 / 테이블 제약까지 | 제약까지. 아이템의 위치(바닥 또는 한 칸), 칸당 아이템 하나, 음수 잔액 금지, 처치당 영수증 하나, 요청 ID당 결과 하나를 CHECK·유일 인덱스·기본 키로 건다. 코드가 틀려도 DB가 거절한다 |
| 멱등성 | 메모리 캐시 / 요청 결과 테이블 | `command_results`. 먼저 `INSERT ... ON CONFLICT`로 넣은 쪽이 이기고, 진 쪽은 저장된 결과를 읽는다. 본문 해시로 같은 ID에 다른 내용을 실은 요청을 가려낸다 |
| 처치 보상의 근거 | 요청 ID / 몬스터 생명주기 ID | 생명주기 ID(`kill_id`)를 영수증의 기본 키로. 요청 ID를 바꿔 다시 보내도 같은 처치는 한 번만 보상된다 |
| 아이템 종류 | 쌓이는 아이템까지 / 쌓이지 않는 개별 아이템만 | 개별 아이템만. 한 인스턴스를 두 사람이 다투는 경합을 먼저 증명한다. `stackable`은 자리만 둔다 |
| 이벤트 기록 | 나중에 / 같은 트랜잭션의 outbox 행 | outbox 테이블을 지금 만든다. 집합별 순번이 유일해야 소비자가 순서를 지킬 수 있다 |
| 인벤토리 전달 | 바뀐 칸만 / 전체 상태 | 전체 상태(`InventoryUpdate`). 유실돼도 다음 것이 대체하고, 버전으로 최신을 판단한다 |

## 구현
- game-data: `items/items.toml`(아이템 3종), `drops/drops.toml`(몬스터별 드롭 표와 골드), `rules/economy.toml`(인벤토리 용량, 획득 거리, 바닥 아이템 소멸 시간), 콘텐츠 버전
- 계약: `contracts/backend/economy.proto`(Economy 서비스: `RecordKill`, `PickupItem`, `GetCharacterAssets`), `server_messages.proto`의 `InventoryUpdate`
- `database/migrations/0004_economy.sql`: `item_instances`, `inventories`, `wallets`, `economy_ledger`, `kill_receipts`, `command_results`, `aggregate_sequences`, `outbox_events`
- `zone/sim`·`zone/data`: 아이템·드롭·경제 규칙을 콘텐츠 묶음에 싣고 검증(모르는 몬스터·아이템, 음수 골드, 확률 범위)
- loadgen: 서버 메시지 switch에 `InventoryUpdate`
- 테스트 2개: 실린 아이템·드롭·경제 규칙이 로드된다, 드롭 표는 아는 아이템과 [0, 1] 확률만 받는다
- README: "자산과 멱등성" 절(테이블이 지키는 규칙, 계약, 콘텐츠), 저장소 구성
- 계약 충돌: 이전 이력의 `server_messages.proto`가 `CombatEvent` 주석을 15 이전 문구로 되돌리려 했다. 15에서 고친 문구를 유지했다

## 만난 문제
- 이전 이력의 주석에 지워진 설계 문서를 가리키는 참조가 여섯 곳 있었다. 숫자만 남은 절 번호(`(8.2)`, `(9.2)`, `(12.2)`)와, 검토 패턴에 없던 "appendix A·B"다. 하나는 자동 치환이 "전투" 절로 잘못 바꾸기도 했다. 숫자는 지우고 자산 설명은 README "자산과 멱등성"을 가리키게 했다. 검토 패턴에 `appendix X`·`부록 X`를 더했다([이슈 007](../issues/007-bare-design-section-numbers-left-in-comments.md)의 검토 범위). ADR-0001의 같은 표현은 당시 결정을 남긴 기록이라 두었다.

## 검증
- C++: dev 142개 통과(20보다 2개 늘었다), clang-tidy 경고 0, ASan·UBSan 142개 통과, 형식 검사 통과.
- .NET: 단위 4개, 통합 26개 통과. 통합 테스트는 Testcontainers의 실제 PostgreSQL에 마이그레이션 0004까지 적용하고 돈다. `dotnet format` 통과.
- end-to-end: 스크립트의 Migrator가 로컬 DB에 0004를 적용했고(`1 script(s) applied`), Zone은 콘텐츠 버전 `2026.10.12-2`로 시작했다. 기본(smoke)은 봇 5개 전원 입장·모순 0·남은 세션 0. 전투(fighters)는 봇 6개, 시전 수락 42·거절 1, 처치 10, 봇 사망 0, 모순 0. 거절 1번은 봇이 노린 몬스터가 그 사이 죽거나 멀어진 경우로, 봇은 서버의 쿨타임·대상 판정을 복제하지 않으므로 생길 수 있다.

## 남긴 것
- Backend `Modules/Economy`(처치 기록, 획득, 자산 조회)와 통합 테스트(동시 획득, 요청 중복, ID 오용, 응답 유실)는 다음 PR이다.
- README 데이터 모델 그림에는 경제 테이블을 Backend 모듈이 들어올 때 더한다.

# 44 (8단계) Gateway 이전: 도착 Zone 재부착, Zone별 세션 메시지

## 배경과 목표
8단계는 두 번째 Zone과 소유권 이전이다. Gateway는 클라이언트 연결을 받아 세션을 Zone 하나에 붙이고 입력과 출력을 중계한다. 세션 로직은 I/O 없는 상태 기계(`SessionProtocol`)라서 전이를 소켓 없이 테스트한다. 43에서 Backend가 이전을 한 트랜잭션으로 확정하게 됐다(소유권 이동, 세대 `owner_epoch` + 1, 도착 지점 체크포인트). 이 PR은 그다음, Gateway가 세션을 도착 Zone에 다시 붙이는 부분이다(ADR-0010). 출발 Zone이 캐릭터를 내려놓고 Gateway에 `SessionTransfer`를 보내면, Gateway는 연결을 끊지 않고 도착 Zone에 부착을 다시 요청한다.

완료 조건:
- 이전 중에도 클라이언트 연결은 유지되고, 도착 Zone이 받아 주면 다시 InZone이 된다.
- 이전 중 클라이언트 입력은 벌하지 않고 버린다.
- 도착 Zone이 거절하거나 링크가 끊기면 입장 실패와 똑같이 정리한다.
- 세션을 더는 갖지 않은 Zone(이전 뒤의 출발 Zone)이 보낸 메시지는 세션을 바꾸지 못한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 재부착 내용 | Backend에 다시 묻기 / 입장 때 만든 부착 요청을 보관해 고쳐 쓰기 | 보관. 신원(계정·캐릭터·이름·클래스)은 서명 티켓과 승인에서 이미 정해졌다. 바뀌는 것은 Zone, 세대, 도착 위치뿐이고 셋 다 `SessionTransfer`에 있다 |
| 출발 Zone 분리 | Gateway가 Detach를 보낸다 / 출발 Zone이 이미 내려놓았다 | 후자. 출발 Zone은 Backend 커밋 뒤 캐릭터를 지우고 통지한다. Gateway가 보낼 것은 도착 Zone의 Attach 하나다 |
| 이전 중 입력 | 프로토콜 오류로 킥 / 버림 | 버림. 클라이언트는 도착 Zone의 Welcome을 보기 전까지 자기 Zone이 바뀐 줄 모른다. Ping은 그대로 답한다 |
| 메시지의 출처 | 세션 ID만 본다 / 실어 온 링크의 Zone ID도 본다 | Zone ID도. 링크가 Zone ID를 붙여 넘기고 상태 기계가 현재 Zone과 비교한다. 검사를 순수 상태 기계에 둬 단위 테스트로 고정한다 |
| 이전 통지 검증 | 그대로 따른다 / 출처·상태·세대 검사 | 검사. 현재 Zone이 보낸 것, InZone일 때, 도착 Zone이 있고 자기 자신이 아닐 때, 세대가 지금보다 클 때만 따른다. 나머지는 무시한다 |

## 구현
- `contracts/internal/zone_link.proto`: `ZoneToGateway.session_transfer`(`SessionTransfer`: 세션 ID, 도착 Zone ID·인스턴스, 새 세대, 도착 위치)
- `SessionProtocol`
  - `kTransferring` 상태와 상태도 주석
  - 승인 때 부착 요청을 `attach_`에 보관하고, 체크포인트가 있으면 위치로 싣는다
  - `OnZoneTransfer(from, transfer)`: 검사 뒤 현재 Zone을 도착 Zone으로 바꾸고 새 세대·위치로 `AttachToZone`
  - Zone에서 온 메시지(`OnZoneAttached`·`Rejected`·`Evicted`·`Output`·`Transfer`)는 출처 Zone을 받아 현재 Zone이 아니면 무시
  - `AttachedOrAttaching()`: 퇴출·링크 단절·정리가 Transferring도 붙은 상태로 본다. 이전 중 연결이 끊기면 도착 Zone에 Detach를 보낸다
- `ZoneLink::Handler`: 세션 메시지마다 링크의 Zone ID를 넘기고 `OnSessionTransfer`를 더했다. `SessionRegistry`·`ClientSession`이 그대로 전달한다
- 테스트 7개: 체크포인트 위치, 재부착과 입력 버림, 이전 뒤 출발 Zone 무시, 세션을 갖지 않은 Zone 무시, 낡거나 잘못된 이전 통지, 도착 거절·이전 중 연결 종료, InZone이 아닐 때의 이전 통지
- README: 세션 상태도에 Transferring, Zone 이전과 Zone별 메시지 설명

## 옮기면서 고친 것
- 이전 이력의 상태 기계는 메시지가 어느 Zone에서 왔는지 보지 않았다. Zone이 둘이 되자 이전 뒤 출발 Zone이 늦게 보낸 출력·퇴출이 도착 Zone에 있는 세션에 그대로 적용될 수 있었다. 퇴출이면 멀쩡한 세션이 킥된다. 이제 현재 Zone에서 온 것만 받는다.
- 이전 이력은 `SessionTransfer`를 무조건 따랐다. 세대가 같거나 낮은 통지, 도착 Zone이 없거나 자기 자신인 통지, 다른 Zone이 보낸 통지는 무시한다.
- 클라이언트 세션의 거절 로그에 어느 Zone이 거절했는지 남긴다.

## 만난 문제
없음.

## 검증
- C++: dev·release·ASan 테스트 240 → 247개 통과, clang-tidy 경고 0, clang-format 통과.
- 로컬 end-to-end(smoke·fighters·looters): Zone 하나에서 기존 입장 경로가 그대로 동작한다. 실제 이전은 Zone이 `SessionTransfer`를 보내기 시작하는 45 뒤에 확인한다.

## 남긴 것
- Zone의 포털·출발·체크포인트 주기·도착 처리(45), 봇과 뷰어(46), 두 번째 Zone 배포와 end-to-end(47).
- 이전 중 도착 Zone 링크가 끊기면 세션은 끝나고, 캐릭터는 재입장 때 Backend의 배치(소유 Zone → 체크포인트의 Zone)로 돌아온다(ADR-0010).

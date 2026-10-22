# 47 (8단계) Zone B 배포, 이동 end-to-end, 설계 노트 10

## 배경과 목표
8단계는 두 번째 Zone과 소유권 이전이다. 지금까지 이렇게 왔다.
- 42~45: 이전 경로(계약·스키마·지도, Backend 트랜잭션, Gateway 재부착, Zone 포털·출발·체크포인트)
- 46: 그 경로를 쓰는 Traveler 봇과 뷰어 포털

하지만 로컬 스크립트와 K3s는 아직 Zone A 하나만 띄웠다. 46은 Zone B를 손으로 띄워 확인했다. 이 PR은 두 번째 Zone(고원)을 모든 실행 환경에 넣는다.
- `scripts/dev-stack.sh`: 대화형 로컬 스택
- `scripts/e2e-local.sh`: 로컬 end-to-end
- `deploy/k3s`·`scripts/k3s-stack.sh`: 단일 노드 K3s

그리고 이동을 end-to-end 판정에 넣고, 설계 노트 10으로 8단계를 정리한다.

완료 조건:
- 스크립트 한 번으로 두 Zone이 뜨고, Gateway가 두 링크를 모두 맺은 뒤에야 봇이 시작한다.
- travelers 프로필이 로컬과 K3s에서 통과한다. 통과 조건은 봇이 본 이동과 Backend가 기록한 이전이 모두 기준 이상인 것이다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| K3s Zone B 설정 | `deploy/k3s`에 TOML 사본 / compose TOML을 그대로 | compose TOML(`deploy/compose/config/zone-b.toml`). 6단계부터의 규칙이다. 프로세스 설정은 한 벌이고 주소만 환경 변수로 바꾼다. Gateway의 두 번째 Zone 주소는 `SA_GATEWAY__ZONES__1__ADDRESS`다 |
| 준비 대기 | 아무 `link ready` 한 줄 / Zone마다 `zone N link ready` | Zone마다. 하나만 기다리면 Zone B 링크가 맺어지기 전에 봇이 출발할 수 있다. 그 봇의 첫 이동은 `SERVICE_UNAVAILABLE`로 끝난다 |
| 이동 판정 | 봇의 Welcome 수만 / DB의 `zone_transfers`도 | DB도. 봇이 본 것만으로는 Backend가 실제로 소유권을 옮겼는지 알 수 없다(이슈 013의 교훈). 추가로, 이번 실행에서 마지막 이전으로 옮겨진 캐릭터가 아직 그 세대라면 소유 Zone이 도착 Zone과 같아야 한다 |

## 구현
- `scripts/dev-stack.sh`: Zone B 시작·종료·상태. 두 lease와 두 링크를 기다린다
- `scripts/e2e-local.sh`: Zone B(같은 고정 시드), 포트 7802 점유 검사, 두 링크 대기
- `scripts/lib/e2e.sh`: `run.min_zone_changes`만큼 `zone_transfers`의 `transferred` 행이 있는지, 마지막 이전과 소유권 행이 맞는지 검사한다. 로컬과 K3s가 함께 쓴다
- K3s
  - `game/zone-b.yaml`: Service `zone-b`(7802)와 Deployment(Recreate). Zone A와 같은 보안 설정이다
  - kustomization의 `zone-b-config`
  - Gateway의 두 번째 Zone 주소
  - `k3s-stack.sh up`: Zone B 롤아웃과 lease, 두 링크를 기다린다
  - `deploy/k3s/README.md`의 표와 "아직 없는 것"
- 문서
  - 설계 노트 10 "두 번째 Zone: 소유권을 옮기고 낡은 소유자를 막는다"(이전의 흐름, 응답 유실과 이전 ID, 낡은 소유자를 막는 곳, 체크포인트와 재입장, 도착 Zone이 죽을 때, 증명한 것)
  - README: 배포 그림 두 개에 Zone B, 이동 end-to-end, 설계 노트 링크
  - CLAUDE.md: 8단계 완료와 다음 순서

## 옮기면서 고친 것
- 이전 이력의 `e2e-local.sh`는 Zone B를 띄운 뒤 고정 1.5초를 기다렸다. 지금 스크립트는 Zone마다 lease와 Gateway 링크 로그를 기다린다.
- 이전 이력의 이동 판정은 봇이 본 횟수뿐이었다. DB의 이전 기록과 소유권을 함께 본다.
- 이전 이력은 K3s용 Zone B 설정을 따로 복사해 두었다. 그 사본에는 옛 설정 이름(`kill_record_attempts`)이 남아 있었다. 지금은 compose 설정 하나를 쓴다.

## 만난 문제
없음.

## 검증
- 로컬 end-to-end
  - travelers: PASS. 봇이 본 이동 18번, 승인 18, Backend의 이전 기록 18건. 마지막 이전과 소유권이 일치하고, 남은 세션 0, 정합성 오류 0
  - smoke·fighters·looters: PASS(Zone B가 함께 뜬 상태)
  - 새 소유권 검사가 비어서 통과하는 것이 아님을 SQL로 확인했다. 캐릭터 5개의 마지막 이전과 소유권 행을 비교한다
- K3s: 이미지를 다시 빌드·반입하고 `k3s-stack.sh up`으로 Zone A·B를 띄웠다. travelers PASS(이동 18번, 이전 기록 18건), looters PASS(처치 14, 획득 8).
- C++·.NET 코드 변경 없음.

## 남긴 것
- v0.6.0 릴리스(8단계).
- 다음 단계 전 용량 기준선(봇 50~1000)을 이 구성으로 다시 잰다.
- Zone·Gateway의 readiness는 여전히 TCP 포트다. lease와 링크를 반영하는 readiness는 장애 연습 단계에서 다룬다.

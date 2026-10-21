# 38 (6단계) K3s 배포: 같은 이미지, 같은 설정, 같은 검사로 단일 노드 클러스터에

## 배경과 목표
개발 루프는 docker compose(PostgreSQL·Kafka)와 호스트 프로세스다. 37에서 프로세스 여섯(Zone, Gateway, loadgen, Backend, EventWorker, Migrator)의 이미지를 만들었다. 이 PR은 그 이미지를 단일 노드 K3s(경량 Kubernetes)에 올리는 매니페스트와 스크립트를 만든다. K3s는 시연과, 이후 단계의 장애·부하 연습 무대다(ADR-0002). 이 PC에 K3s를 처음 설치한 것도 이 PR에서다.

완료 조건: `scripts/k3s-stack.sh up`으로 올린 스택에서, 로컬 end-to-end와 같은 봇 프로필과 같은 DB 검사가 통과한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| Zone·Gateway 설정 | K3s용 TOML을 따로 / compose TOML 하나에 주소만 환경 변수 | 후자. 따로 두면 조율값이 두 벌이 되어 언젠가 어긋난다. kustomize가 `deploy/compose/config/`를 읽고(`--load-restrictor LoadRestrictionsNone`), ConfigMap 이름에 내용 해시가 붙어 파일이 바뀌면 파드가 다시 뜬다 |
| 배열 항목의 주소 | 바꿀 수 없음 / 번호로 환경 변수 | 번호로(`SA_GATEWAY__ZONES__0__ADDRESS`). 이전 설정 코드는 "반복 섹션은 배포 토폴로지라 환경 변수가 닿지 않는다"고 정했는데, 배포마다 바뀌는 것이 바로 토폴로지다. 없는 번호를 가리키는 변수는 오류다. 오타가 조용히 무시되지 않는다 |
| Backend 내부 gRPC | 클러스터 전체에 열기 / loopback 유지 / 모든 주소 + NetworkPolicy | 마지막. 호출자 인증이 아직 없어 기본은 127.0.0.1이다. 클러스터에서는 Zone·Gateway가 다른 파드라 모든 주소로 열고, 같은 네임스페이스의 파드만 5101에 닿게 한다. HTTP(5100)는 NodePort로 공개한다 |
| 데이터 저장소 접근 | 열어 둠 / `game` 네임스페이스만 | `game`만(NetworkPolicy). 다른 것이 닿을 이유가 없다 |
| 컨테이너 보안 | 기본 / 비루트·권한 상승 금지·capability 없음·읽기 전용 루트 | 후자. .NET은 `/tmp`만 emptyDir. PostgreSQL·Kafka 공식 이미지는 손대지 않는다 |
| EventWorker 배포 전략 | Recreate / 롤링 | 롤링. 발행자 락(35)이 있어 잠깐 둘이 떠도 발행자는 하나고, 소비자는 그룹이 파티션을 나눈다. Zone·Gateway는 Recreate다. 둘이 뜨면 안 된다 |
| K3s 검증 | 수동 / end-to-end 스크립트 | `scripts/e2e-k3s.sh`. DB 검사(남은 세션, 처치·획득 기록, 이벤트 발행·적용)를 `scripts/lib/e2e.sh`로 빼서 로컬과 K3s가 같은 검사를 쓴다 |
| K3s 데이터 위치 | 기본 `/var/lib/rancher`(루트) / `/home/k3s` | `/home/k3s`. 루트(100G)가 89%라 kubelet이 반입한 이미지를 지웠다([016](../issues/016-k3s-pods-unreachable-and-images-collected.md)). 사용자와 정했다 |

## 구현
- `deploy/k3s/`(kustomize)
  - `namespaces.yaml`
  - `data/`: `postgres`·`kafka` StatefulSet, NetworkPolicy. DB 계정은 Secret에서
  - `migrate/`: `migrator` Job
  - `game/`: `backend`(gRPC 0.0.0.0, NodePort 30500), `eventworker`, `zone-a`(Recreate), `gateway`(Recreate, NodePort 30777·30778), NetworkPolicy, compose TOML의 ConfigMap 생성기
  - `README.md`: 구성, 설치와 배포 순서, 결정
- `scripts/k3s-stack.sh import|up|down|status`
  - Secret과 `backend-env` ConfigMap을 만든다
  - data → migrate → game 순서로 적용한다
  - Zone·Gateway가 lease와 Zone 링크를 얻고(로그), NodePort로 Backend가 답할 때까지 기다린다
  - `down`은 파드가 사라질 때까지 기다린다
- `scripts/e2e-k3s.sh`: 호스트의 봇으로 NodePort에 붙고, 클러스터 안 PostgreSQL에서 DB 검사를 한다
- `scripts/lib/e2e.sh`: 로컬·K3s 공용 검사(`check_run`). `scripts/e2e-local.sh`가 이것을 쓴다
- `common/config_source`: 배열 섹션 항목에 환경 변수 덮어쓰기(번호로), 없는 번호는 오류
- Gateway `ZoneLink`: 연결 수립 기한 `[zone_link] establish_timeout_ms`(compose 3000)([017](../issues/017-gateway-zone-link-connect-without-a-deadline.md))
- `common/logging`: 로그 시각을 UTC로(형식은 UTC를 뜻하는 `Z`를 붙이는데 지역 시각을 찍고 있었다)
- 테스트 2개: 배열 항목 덮어쓰기와 없는 번호 거절, 환영하지 않는 Zone을 포기하고 다시 시도
- README: 배포 절의 K3s 설명과 그림, 검증의 K3s end-to-end, 빌드 명령, 저장소 구성

## 옮기면서 고친 것
- 이전 매니페스트에서 Backend의 gRPC는 loopback에서만 들었다. Zone·Gateway 파드가 Backend에 닿지 못했을 것이다. 모든 주소로 열고 NetworkPolicy로 막았다.
- 이전에는 K3s용 Zone·Gateway TOML이 따로 있어 조율값이 두 벌이었다. compose TOML 하나만 쓰고 주소만 바꾼다. 그러려고 배열 항목 덮어쓰기를 더했다.
- 이전 Backend 이미지 이름과 키를 지금 코드에 맞췄다(`sa/eventworker`, `GameData__ContentDir`는 37에서).
- 이전 PostgreSQL 매니페스트는 DB 이름과 계정을 매니페스트에, 비밀번호만 Secret에 두었다. 연결 문자열은 `.env`에서 만들어서, 둘이 다르면 어긋났다. 셋 다 같은 Secret에서 읽는다.
- 이전 스크립트는 롤아웃이 끝나면 바로 끝났다. 하지만 롤아웃 완료는 포트가 열렸다는 뜻일 뿐이다. 곧바로 봇을 돌리면 두 가지가 실패했다. NodePort가 아직 답하지 않았고, Gateway의 Backend gRPC 채널이 시작 때의 실패로 재연결 대기 중이라 티켓 소비가 즉시 실패했다. 그래서 `up`은 lease·링크 로그와 NodePort 응답을 기다린다. `down`은 파드가 사라질 때까지 기다린다.
- 보안 컨텍스트, 데이터 저장소 NetworkPolicy, K3s end-to-end 스크립트는 새로 더했다.

## 만난 문제
- 파드끼리 `No route to host`: 켜져 있던 firewalld가 파드 대역을 막았다. 반입한 이미지가 지워짐: 루트 디스크가 차서 kubelet이 이미지를 회수했다. firewalld 신뢰 영역에 K3s 대역을 넣고, K3s를 `/home/k3s`로 다시 설치했다. 루트를 채운 이전 작업 트리의 빌드 디렉터리(18G)는 사용자 확인 뒤 지웠다([016](../issues/016-k3s-pods-unreachable-and-images-collected.md)).
- 배포 직후 봇이 모두 `SERVICE_UNAVAILABLE`로 쫓겨났다. Gateway의 첫 Zone 연결이 응답 없는 SYN에 132초 동안 묶였다. 연결 수립에 기한을 두었다([017](../issues/017-gateway-zone-link-connect-without-a-deadline.md)).
- 호스트에서 찍은 로그 시각이 9시간 앞서 있었다. UTC라고 표시하면서 지역 시각을 찍었다. 컨테이너는 UTC라 드러나지 않았다.
- 고친 뒤 처음부터 다시 올리자, 봇이 이번에는 티켓 소비에서 `SERVICE_UNAVAILABLE`을 받았다. Gateway가 Backend보다 먼저 떠 gRPC 채널이 재연결 대기에 들어가 있었다. 파드의 Ready는 포트만 본다. 위 "옮기면서 고친 것"대로 `up`이 lease와 링크를 기다리게 했다.

## 검증
- C++: dev 235 → 237개 통과, clang-tidy 경고 0, ASan 237개 통과, 형식 검사 통과.
- 새 연결 기한 테스트는 기한을 1시간으로 바꾸면 실패한다.
- K3s 전체 주기(이미지 빌드 → 반입 → down → up → end-to-end, `looters.toml`): PASS.

| 항목 | 값 |
|---|---|
| 봇 | 6개 전원 입장, 남은 세션 0 |
| 시전 | 50회, 수락 49, 거절 0 |
| 처치 | 14 (DB 처치 영수증 14) |
| 획득 | 요청 9회, 획득 9 (DB 원장 9) |
| 이벤트 | 이번 실행 23개, 모두 발행되고 한 번씩 적용 |
| 정합성 오류 | 0 |

- 로컬 end-to-end(smoke, looters): 검사를 공용 스크립트로 뺀 뒤에도 PASS. looters는 처치 12, 획득 11, 이벤트 23개 발행·적용.
- NetworkPolicy: `default` 네임스페이스 파드에서 Backend gRPC(5101)와 PostgreSQL은 막히고 Backend HTTP(5100)는 열린다. `game` 네임스페이스 파드에서는 gRPC가 열린다.

## 남긴 것
- C++ 빌드 기반 이미지(GHCR)와 CI 전환은 39에서. 그것으로 6단계가 끝나고 0.4.0을 낸다.
- Zone·Gateway의 readiness가 포트만 본다. 받을 준비(lease, Zone 링크)를 반영하려면 C++ 프로세스에 상태를 묻는 수단(HTTP 헬스나 파일)이 필요하다. 장애 연습 단계에서 정한다. 지금은 `up`이 로그로 기다린다.
- Gateway의 Backend 호출은 채널이 재연결 대기 중이면 즉시 실패한다. 호출별로 연결을 기다리게(`wait_for_ready`) 할지는 같은 단계에서 본다.
- Zone B, 관측, 외부 백업은 이후 단계에서.

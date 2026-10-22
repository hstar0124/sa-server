# deploy/k3s

단일 노드 K3s(경량 Kubernetes)에 전체 스택을 올리는 매니페스트(kustomize)와 순서다. 개발 루프는 여전히 `deploy/compose/`(PostgreSQL·Kafka만 컨테이너, 나머지는 호스트 프로세스)이고, K3s는 시연과 장애 연습용이다(ADR-0002).

## 구성

| 네임스페이스 | 리소스 | 비고 |
|---|---|---|
| `data` | `postgres` StatefulSet, local-path 볼륨 20Gi | 내구성 설정은 PostgreSQL 기본값 |
| `data` | `kafka` StatefulSet, 볼륨 10Gi | KRaft 단일 노드, 복제 없음, 보관 24시간·2 GiB |
| `data` | `redis` StatefulSet | 킬 랭킹의 복사본. 디스크에 아무것도 두지 않고 메모리 상한과 noeviction. 재시작하면 EventWorker가 다시 채운다 |
| `data` | NetworkPolicy `data-from-game-only` | `game` 네임스페이스만 닿는다 |
| `game` | `migrator` Job | `database/migrations` 적용. 다시 돌리면 아무것도 하지 않는다 |
| `game` | `backend` Deployment, Service `backend`(5100/5101), `backend-public`(NodePort 30500) | HTTP는 노드 밖으로, gRPC는 클러스터 안으로만 |
| `game` | NetworkPolicy `backend-grpc-from-game-only` | 호출자 인증이 아직 없는 내부 gRPC(5101)는 같은 네임스페이스의 파드만 |
| `game` | `eventworker` Deployment | Outbox 발행자와 통계 소비자. 발행자 락이 있어 롤링 업데이트로 잠깐 둘이 떠도 안전하다 |
| `game` | `zone-a`·`zone-b` Deployment(Recreate), Service `zone-a`(7801)·`zone-b`(7802) | 초원과 고원. 각각 절대 둘이 뜨지 않는다. 재시작은 새 인스턴스 ID. 캐릭터는 포털로 오간다 |
| `game` | `gateway` Deployment(Recreate), Service `gateway`(NodePort 30777 TCP, 30778 WebSocket) | 클라이언트가 닿는 유일한 게임 프로세스 |

- **설정**: Zone·Gateway의 TOML은 `deploy/compose/config/`의 파일 그대로다. 매니페스트는 주소만 환경 변수로 바꾼다(`SA_ZONE__BACKEND__ADDRESS`, 배열 항목은 `SA_GATEWAY__ZONES__0__ADDRESS`처럼 번호로). ConfigMap 이름에 내용 해시가 붙어, 파일이 바뀌면 그 파일을 쓰는 파드가 다시 뜬다. 디렉터리 밖 파일을 읽으므로 렌더링에 `--load-restrictor LoadRestrictionsNone`이 필요하다.
- **비밀 값**: 매니페스트에 없다. `scripts/k3s-stack.sh up`이 `deploy/compose/.env`의 DB 계정과 생성한 서명 키로 Secret(`sa-postgres`, `sa-database`, `sa-keys`)을 만들고, 노드 IP와 NodePort로 `backend-env` ConfigMap을 채운다.
- **보안 컨텍스트**: 게임 파드와 마이그레이터는 비루트, 권한 상승 금지, 모든 capability 제거, 읽기 전용 루트 파일 시스템(.NET은 `/tmp`만 emptyDir)이다.

## 순서

```bash
# 1. K3s 설치 (한 번). swap이 있는 장비라 kubelet의 swap 검사를 끄고, 쓰지 않는 Traefik은 뺀다.
#    데이터(이미지, 볼륨)는 루트(100G)가 아니라 /home(322G)에 둔다. 아래 "결정한 것"의 스토리지.
curl -sfL https://get.k3s.io | sh -s - --write-kubeconfig-mode 644 --kubelet-arg=fail-swap-on=false \
  --disable traefik --data-dir /home/k3s
export KUBECONFIG=/etc/rancher/k3s/k3s.yaml K3S_DATA_DIR=/home/k3s
# firewalld가 켜진 장비는 파드(10.42.0.0/16)·서비스(10.43.0.0/16) 대역을 신뢰 영역에 넣는다.
# 넣지 않으면 파드끼리 "No route to host"로 닿지 않는다.
for cidr in 10.42.0.0/16 10.43.0.0/16; do
  firewall-cmd --zone=trusted --add-source=$cidr && firewall-cmd --permanent --zone=trusted --add-source=$cidr
done

# 2. 이미지 빌드와 반입 (레지스트리 없이 K3s의 containerd로)
scripts/build-images.sh && scripts/k3s-stack.sh import

# 3. 배포: data → migrate → game 순서로 적용하고 준비될 때까지 기다린다
scripts/k3s-stack.sh up
scripts/k3s-stack.sh status

# 4. 봇으로 확인: compose의 e2e와 같은 DB 검사까지
scripts/e2e-k3s.sh tools/loadgen/client/profiles/looters.toml

scripts/k3s-stack.sh down
```

매니페스트만 보려면 클러스터 없이 `kubectl kustomize --load-restrictor LoadRestrictionsNone deploy/k3s`로 렌더링한다.

## 결정한 것

- **swap**: `fail-swap-on=false`. 장비 레이아웃을 바꾸지 않는다는 결정(ADR-0002)을 따른다. 메모리 상한은 컨테이너마다 `limits`로 둔다.
- **스토리지**: K3s 기본 `local-path`. 데이터는 노드 디스크(`<data-dir>/storage`)에 남는다. `down`은 볼륨을 지우지 않는다. 이 장비는 루트가 100G라 K3s 데이터 전체를 `/home/k3s`에 둔다. 기본 위치(`/var/lib/rancher`)에 두었을 때 루트 사용률이 85%를 넘자 kubelet이 반입한 이미지를 지워 배포가 `ImagePullBackOff`로 멈췄다([016](../../docs/issues/016-k3s-pods-unreachable-and-images-collected.md)).
- **노출**: 게임 포트는 raw TCP와 WebSocket이라 Ingress가 아니라 NodePort로 명시적으로 연다. Backend HTTP도 NodePort로 열어 뷰어와 봇이 바로 붙는다.
- **이미지**: 태그 `dev`, `imagePullPolicy: IfNotPresent`. 레지스트리 없이 `k3s ctr images import`로 반입한다.
- **헬스**: Backend는 `/health/live`·`/health/ready`, Zone·Gateway는 수신 포트의 TCP 프로브. 포트가 열렸다고 받을 준비가 된 것은 아니다. Backend lease와 Zone 링크가 있어야 한다. 그래서 `up`은 그 로그(`zone lease acquired`, `gateway lease acquired`, `link ready`)를 기다린 뒤 끝난다. lease와 링크를 반영하는 readiness는 장애 연습 단계에서 다룬다. 지금은 Gateway가 그 상태가 아닐 때 들어온 클라이언트를 `SERVICE_UNAVAILABLE`로 돌려보낸다.
- **종료**: `terminationGracePeriodSeconds: 10`은 Zone·Gateway의 `[shutdown] grace_ms`(2~3초)보다 넉넉하다. Zone이 세션을 내보내고 Gateway가 세션을 끝낼 시간이다.

## 아직 없는 것

관측(Prometheus·Grafana), 외부 백업·복원.

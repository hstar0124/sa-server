# 016 K3s에서 파드끼리 닿지 않고, 반입한 이미지가 지워짐

- 상태: 해결 (firewalld 신뢰 영역에 K3s 대역 추가, K3s 데이터를 /home으로, 2026-10-21)
- 발견: 38 K3s 배포를 처음 올리다, 2026-10-21
- 영향: 처음 설치한 K3s에서 마이그레이션 Job이 DB에 닿지 못해 배포가 멈췄다. 고친 뒤에는 반입한 이미지가 사라져 `ImagePullBackOff`로 다시 멈췄다. 개발 루프(compose)와 기존 컨테이너는 영향이 없었다

## 증상
1. 마이그레이터가 PostgreSQL 파드에 연결하지 못했다.
   ```
   Npgsql.NpgsqlException: Failed to connect to 10.42.0.7:5432
    ---> System.Net.Sockets.SocketException (113): No route to host
   ```
2. 다시 배포하자 마이그레이터 파드가 `ImagePullBackOff`였다. 레지스트리에 없는 `docker.io/sa/migrator:dev`를 받으려 했다.

## 원인 분석
1. **방화벽**: `game` 네임스페이스의 시험 파드에서 `nc`로 PostgreSQL에 붙어 봤다. NetworkPolicy가 있을 때와 지웠을 때 모두 `No route to host`였으므로 정책이 아니었다. 이 장비는 firewalld가 켜져 있고, 파드 대역(10.42.0.0/16)이 어느 영역에도 없어서 파드 사이 트래픽이 거부됐다. K3s 문서가 firewalld를 끄거나 두 대역을 신뢰 영역에 넣으라고 안내하는 경우다.
2. **디스크**: kubelet 로그에 `Removing image to free bytes`와 `Insufficient free disk space on the node's image filesystem (91% of 99.9 GiB used)`가 있었다. K3s의 기본 데이터 위치 `/var/lib/rancher`는 루트 파일 시스템(100G)에 있고, 루트는 89% 차 있었다. kubelet은 이미지 파일 시스템 사용률이 85%를 넘으면 쓰지 않는 이미지를 지운다. 레지스트리 없이 `ctr images import`로 넣은 이미지는 다시 받을 곳이 없다.
   - 루트를 채운 것은 주로 작업 디렉터리였다. 피처 12에서 멈춘 이전 작업 트리의 `build/`가 18G였고, 그 안의 `vcpkg_installed`를 지금 작업 트리가 심볼릭 링크로 쓰고 있었다.

## 원인
설치 때 이 장비의 두 조건(firewalld 켜짐, 루트가 작고 거의 참)을 확인하지 않고 K3s 기본값으로 설치했다.

## 해결
- firewalld 신뢰 영역에 파드 대역 10.42.0.0/16과 서비스 대역 10.43.0.0/16을 넣었다(런타임과 영구 설정 둘 다, reload 없이). 외부에 새로 여는 포트는 없다.
- K3s를 지우고 `--data-dir /home/k3s`로 다시 설치했다. 이미지, 볼륨, containerd가 모두 `/home`(322G, 26% 사용)에 있다. 클러스터는 그날 만든 것이라 잃을 데이터가 없었다. `k3s ctr`도 같은 디렉터리를 쓰도록 `K3S_DATA_DIR`을 준다.
- 이전 작업 트리의 `build/`를 지웠다. 먼저 `vcpkg_installed`를 지금 작업 트리로 옮기고(같은 파일 시스템이라 이동만) 심볼릭 링크를 없앴다. 루트는 77%가 됐다.
- 디스크 레이아웃(루트·docker·home LV)은 바꾸지 않았다.

## 재발 방지
- `deploy/k3s/README.md`의 설치 순서에 `--data-dir`, `K3S_DATA_DIR`, firewalld 신뢰 영역 설정을 넣었다. 스토리지 결정에 이유를 적었다.
- `scripts/k3s-stack.sh` 주석에 `K3S_DATA_DIR`을 적었다.

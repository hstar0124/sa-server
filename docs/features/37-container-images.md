# 37 (6단계) 컨테이너 이미지: 프로세스 여섯을 이미지로, release 빌드를 처음으로

## 배경과 목표
지금까지 모든 프로세스는 호스트에서 돌았다. 개발 루프는 docker compose(PostgreSQL·Kafka)와 호스트 프로세스(Backend, EventWorker, Zone, Gateway, 봇)다. 6단계는 K3s(단일 노드 Kubernetes) 배포를 들인다. 이 PR은 그 첫 조각으로 프로세스 여섯의 컨테이너 이미지를 만든다. 매니페스트와 배포 스크립트는 38, C++ 빌드용 기반 이미지와 CI 전환은 39에서 한다.

완료 조건: 이미지 여섯이 빌드되고, 이미지만으로 띄운 스택이 로컬 end-to-end와 같은 획득 시나리오를 통과한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| C++ 빌드 환경 | 배포판 기본 컴파일러 / 개발 장비와 같은 Rocky 9·gcc-toolset-14·vcpkg baseline | 후자. 같은 컴파일러와 baseline이면 호스트의 vcpkg 바이너리 캐시를 그대로 쓸 수 있다. 첫 이미지 빌드도 의존성을 다시 컴파일하지 않았다 |
| 캐시 전달 | 매번 의존성 빌드 / BuildKit 캐시 마운트 + 호스트 캐시를 씨앗 컨텍스트로 | 후자(`--build-context vcpkg-seed=~/.cache/vcpkg/archives`). 저장소 컨텍스트에 캐시를 넣지 않는다 |
| C++ 이미지 구성 | 프로세스마다 Dockerfile / 빌드 단계 하나에 런타임 대상 셋 | 후자(`--target zone|gateway|loadgen`). 한 번 빌드로 셋이 나온다. 런타임은 rockylinux:9-minimal에 libstdc++, 고정 uid 10001의 비루트 |
| 빌드 구성 | Debug / release 프리셋 | release. 그러다 release 프리셋이 처음부터 컴파일되지 않았다는 것을 찾았다([015](../issues/015-release-preset-never-compiled.md)) |
| .NET 런타임 기반 | Backend는 aspnet, 나머지는 runtime / 셋 다 aspnet | 셋 다 aspnet. Npgsql이 찾는 Kerberos 라이브러리 설치를 한 단계에서 하고, 세 이미지가 같은 층을 나눠 쓴다 |
| 복원 | 일반 복원 / 잠금 파일 | `--locked-mode`. CI와 같은 의존성으로 빌드한다 |
| 설정과 비밀 | 이미지에 넣기 / 밖에서 주기 | 밖에서. TOML은 마운트(`/etc/sa/*.toml`), 연결 문자열과 서명 키는 환경 변수다. 이미지에는 game-data와 마이그레이션 SQL처럼 코드와 함께 버전이 매겨지는 것만 넣는다 |

## 구현
- `deploy/docker/cpp.Dockerfile`: 빌더(Rocky 9, gcc-toolset-14, CMake·Ninja, baseline에 고정한 vcpkg) → release 프리셋으로 `zone_server`, `gateway`, `loadgen`. 런타임 대상 `zone`, `gateway`, `loadgen`(프로필 포함). 모두 game-data를 싣는다
- `deploy/docker/dotnet.Dockerfile`: SDK에서 잠금 복원과 publish → 런타임 대상 `backend`(game-data, `GameData__ContentDir`), `eventworker`, `migrator`(마이그레이션 SQL, `MIGRATOR_connection`)
- `.dockerignore`: 빌드 산출물, VCS, 문서, 뷰어, 로컬 비밀
- `scripts/build-images.sh [tag]`: 여섯 이미지를 `sa/<이름>:<태그>`(기본 `dev`)로 빌드
- `cmake/CompilerWarnings.cmake`: `-Wnull-dereference`를 Debug 빌드로 한정
- README: 빌드 절의 이미지 설명과 명령, 저장소 구성

## 옮기면서 고친 것
- 이전 Backend 이미지는 game-data 경로를 `Economy__ContentDir`로 넘겼다. 31에서 설정이 `GameData__ContentDir`로 옮겨 갔으므로, 그대로 옮겼다면 Backend가 시작에서 멈췄을 것이다.
- 이전 .NET 이미지는 세 런타임 대상마다 같은 패키지 설치를 반복했다. 기반 단계 하나로 합쳤다. 복원은 잠금 파일로 한다.
- 이전 C++ 빌드 명령의 `cp ... || true && cmake ...`는 셸 우선순위 때문에 의도대로 돌긴 했지만 읽기 어려웠다. 씨앗 복사를 묶어 의도를 드러냈다.
- 이전 스크립트는 이미지 빌드와 K3s 반입을 함께 했다. 반입은 K3s를 다루는 38로 넘기고, 이 PR은 빌드만 한다.

## 만난 문제
- release 프리셋이 한 번도 컴파일되지 않았다. GCC의 `-Wnull-dereference`가 `-O2`에서 인라인된 asio 코드에 거짓 경고를 내고 `-Werror`가 빌드를 멈췄다. 개발과 CI가 모두 Debug 프리셋만 써서 몰랐다. Debug 빌드로 한정했다([015](../issues/015-release-preset-never-compiled.md)).

## 검증
- release 프리셋: 고친 뒤 `zone_server`, `gateway`, `loadgen` 빌드 성공.
- C++ Debug: dev 235개 통과, clang-tidy 경고 0, ASan 235개 통과, 형식 검사 통과.
- 이미지 여섯 빌드 성공. vcpkg 의존성은 씨앗 캐시에서 복원됐다.
- 이미지 스모크(호스트 네트워크, compose의 PostgreSQL·Kafka): migrator 이미지가 "적용할 스크립트 없음"으로 끝나고, EventWorker·Backend·Zone·Gateway 이미지가 차례로 준비되고, loadgen 이미지로 `looters.toml`을 돌렸다. PASS: 봇 6개 전원 입장, 처치 14, 획득 10, 정합성 오류 0, EventWorker가 이벤트를 발행했다.
- .NET 코드는 바뀌지 않았다.

## 남긴 것
- K3s 매니페스트(kustomize), 비밀 값 생성, 이미지 반입, 배포 스크립트는 38에서.
- C++ 빌드 기반 이미지(GHCR)와 CI를 그 안에서 돌리는 전환, CI에 release 빌드를 더할지는 39에서.
- EventWorker 이미지에는 librdkafka의 모든 플랫폼용 네이티브 라이브러리가 들어 있다(이미지 347 MB). 런타임 식별자를 정해 publish하면 줄일 수 있지만, 잠금 파일에 런타임별 항목이 생긴다. 크기가 문제가 될 때 정한다.

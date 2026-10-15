# 006 의존성 캐시가 그것을 만든 PR에서만 보여 다음 PR이 다시 2시간 빌드

- 상태: 해결 (05의 `ci: run on develop pushes ...` 커밋, 효과는 05 병합 뒤부터)
- 발견: [05 Zone 시뮬레이션](../features/05-zone-simulation.md) PR #5의 CI, 2026-10-15
- 영향: C++ 의존성(gRPC·protobuf·abseil) 빌드가 PR마다 약 2시간씩 반복된다

## 증상
PR #4에서 의존성 잡이 2시간 15분 걸려 캐시를 저장했고 그 뒤 실행들은 몇 분이었다. 그런데 다음 PR #5의 `C++ dependencies`가 다시 처음부터 빌드하기 시작했다. 캐시 키(`vcpkg-x64-linux-release-<vcpkg.json 해시>`)는 같다.

```
$ gh api repos/hstar0124/sa-server/actions/caches
vcpkg-x64-linux-release-95fbf057... refs/pull/4/merge 931MB
```

## 원인 분석
캐시 목록의 `ref`가 `refs/pull/4/merge`다. GitHub Actions 캐시는 **만든 ref에 묶인다.** 실행은 자기 ref, 기반 브랜치, 기본 브랜치의 캐시만 읽을 수 있다. PR #4가 만든 캐시는 PR #4만 읽을 수 있고, PR #5는 같은 키라도 접근할 수 없다. 기반 브랜치(`develop`)에는 캐시가 없었다. CI가 `main` 푸시와 PR에만 돌았기 때문이다.

## 원인
캐시를 PR 안에서만 만들었다. 공유되는 자리(`develop`)에서 만든 적이 없다.

## 해결
- CI가 `develop` 푸시에도 돈다. PR이 병합되면 `develop`에서 한 번 돌며 의존성 캐시를 `refs/heads/develop`에 저장하고, 이후 `develop`을 기반으로 한 모든 PR이 그것을 읽는다. `vcpkg.json`이 바뀌지 않는 한 그 실행도 캐시 적중이라 몇 분이다.
- 손으로 캐시를 데울 수 있게 `workflow_dispatch`도 둔다.
- 이 변경은 `develop`에 들어간 뒤부터 효과가 있다. PR #5는 진행 중이던 2시간짜리 실행을 50분쯤에서 취소하고 이 커밋을 더해 다시 돌렸다. 그냥 두면 #5가 끝난 뒤 #6도 같은 빌드를 반복해야 했기 때문이다(취소로 50분을 잃고 한 번의 2시간을 아꼈다). #5 병합 직후의 `develop` 실행이 공유 캐시를 만들고, 다음 PR은 그 실행이 끝난 뒤 연다.

대안: vcpkg 바이너리 캐시를 GitHub Packages(NuGet 피드)에 두면 ref 범위가 없다. 토큰 권한과 피드 관리가 생겨 지금은 택하지 않았다. PR이 많아지고 `vcpkg.json`이 자주 바뀌면 다시 본다.

## 재발 방지
- CI 캐시를 쓰는 잡을 추가할 때 "누가 저장하고 누가 읽는가(ref 범위)"를 피처 문서의 검증 절에 적는다.

# 작업 중 만난 문제

문제 하나에 문서 하나. 증상·원인 분석·해결·재발 방지를 남긴다. 틀렸던 가설도 적는다. 보고서와 피처 문서는 여기를 링크하고 같은 내용을 되풀이하지 않는다.

| # | 상태 | 발견 | 증상 |
|---|---|---|---|
| [001](001-ci-clang-tidy-version-mismatch.md) | 해결 | 01 저장소 도구 | CI의 tidy 잡이 로컬에서 통과한 코드에 실패 |
| [002](002-cpp-tree-without-a-root-cmake-project.md) | 해결 | 04 common | C++ 컴포넌트가 루트 CMake 프로젝트 없이 커밋됨 |
| [003](003-ci-runner-out-of-disk-building-grpc.md) | 해결 | 04 common | CI 러너가 gRPC 의존성을 빌드하다 디스크가 가득 참 |
| [004](004-clang-tidy-rejects-cmake-module-scanning-flags.md) | 해결 | 04 common | CI의 clang-tidy가 CMake의 모듈 스캔 플래그를 거부 |
| [005](005-ci-ran-clang-tidy-18-despite-the-pin.md) | 해결 | 04 common | CI가 LLVM 21을 설치하고도 clang-tidy 18을 실행함 |
| [006](006-ci-dependency-cache-scoped-to-one-pull-request.md) | 해결 | 05 Zone 시뮬레이션 | 의존성 캐시가 그것을 만든 PR에서만 보여 다음 PR이 다시 빌드 |
| [007](007-bare-design-section-numbers-left-in-comments.md) | 해결 | 14 전투 시뮬레이션 | 지워진 설계 문서의 절 번호가 주석 다섯 곳에 숫자로만 남음 |
| [008](008-version-bump-invalidates-the-ci-dependency-cache.md) | 해결 | 17 CI 의존성 캐시 | 릴리스 버전 한 줄이 CI 의존성 캐시를 무효로 만든다 |
| [009](009-actions-minutes-exhausted-on-a-private-repo.md) | 해결 | 17 CI 의존성 캐시 | 비공개 저장소의 Actions 무료 시간이 바닥나 CI 잡이 시작되지 않음 |

## 형식

```
# NNN <증상을 한 줄로>
- 상태: 해결 (커밋) | 열림
- 발견: <피처 문서>, <날짜>
- 영향: <누가 무엇을 못 하게 되나>
## 증상        로그·수치·명령 출력 그대로, 재현 조건
## 원인 분석   무엇을 의심했고 무엇으로 확인했나
## 원인
## 해결        무엇을 바꿨나, 왜 그 방법인가, 대안
## 재발 방지   추가한 테스트·검사·규칙
```

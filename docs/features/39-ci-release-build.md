# 39 (6단계) CI release 빌드: 컨테이너가 싣는 최적화 빌드도 매번 빌드하고 테스트한다

## 배경과 목표
37에서 컨테이너 이미지를 만들다가, release 프리셋이 저장소를 만든 뒤로 한 번도 컴파일되지 않았다는 것을 찾았다([015](../issues/015-release-preset-never-compiled.md)). 개발과 CI가 모두 Debug 프리셋(dev, asan, tidy)만 썼기 때문이다. 37에서 원인(최적화 단계의 거짓 null 경고)은 고쳤다. 이 PR은 같은 일이 다시 생기지 않게, CI가 PR마다 release 빌드와 테스트를 돌리게 한다.

원래 계획은 C++ 빌드 기반 이미지를 GHCR에 올리고 CI를 그 안에서 돌리는 것이었다. 외부 레지스트리에 게시하지 않기로 해서 그 계획은 접었다. CI 환경의 흔들림은 지금처럼 버전 고정(CMake, LLVM)과 vcpkg 바이너리 캐시로 다룬다.

완료 조건: CI의 C++ 매트릭스에 release가 있고, 로컬에서 release 빌드와 테스트가 통과한다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| CI 환경 고정 | 빌드 기반 이미지(GHCR) / 지금 방식(버전 고정 + vcpkg 캐시) | 지금 방식. 외부 레지스트리에 이미지를 게시하지 않는다. 러너 이미지 변화로 생긴 문제는 이슈 001~009의 고정과 캐시로 막아 왔다 |
| release 검사 범위 | 빌드만 / 빌드와 테스트 | 빌드와 테스트. 최적화는 컴파일러가 증명하는 것과 정의되지 않은 동작의 결과를 바꾼다. 컨테이너가 싣는 것이 이 빌드다 |
| 넣는 방법 | 별도 잡 / 기존 매트릭스에 한 줄 | 매트릭스(`preset: [dev, release, asan, tidy]`). 툴체인 설치와 의존성 캐시 복원을 그대로 쓴다. ctest가 release에서 돌도록 테스트 프리셋을 더했다 |

## 구현
- `.github/workflows/ci.yml`: C++ 매트릭스에 `release`
- `CMakePresets.json`: 테스트 프리셋 `release`
- `CONTRIBUTING.md`: 필수 검사가 여섯 잡(C++ dependencies, dev, release, asan, tidy, .NET)
- 이슈 015의 재발 방지 갱신

## 만난 문제
- 없음.

## 검증
- 로컬: `cmake --preset release -DSA_BUILD_TESTS=ON`으로 전체를 빌드했다. 테스트 237개가 모두 통과했다(RelWithDebInfo, `-O2`).
- 워크플로 YAML 구문 확인. CI 결과는 이 PR의 실행으로 확인한다.

## 남긴 것
- 저장소 룰셋 `protect-main-branch`의 필수 검사에 `C++ (release)`를 더해야 한다. 저장소 설정이라 이 PR 밖에서 한다.
- 이 PR로 6단계가 끝난다. 머지 뒤 0.4.0을 낸다.

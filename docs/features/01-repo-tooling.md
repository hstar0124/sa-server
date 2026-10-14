# 01 (0단계) 저장소 도구: 같은 기준으로 빌드·검사·형식을 어디서나

## 배경과 목표
코드보다 먼저 "무엇이 통과인가"를 정한다. C++23(GCC 14)과 .NET 10 두 언어가 한 저장소에 있고, 경고는 에러이며, 정적 분석과 형식 검사가 로컬과 CI에서 같은 답을 내야 한다. 완료 조건: `cmake --preset {dev,asan,tidy}`와 `scripts/check-format.sh`가 로컬에서 돌고, 같은 것이 GitHub Actions에서 PR마다 돌며 네 잡이 녹색이다.

## 고민한 선택지와 결정
| 선택지 | 장점 | 단점 | 판단 |
|---|---|---|---|
| vcpkg manifest + `builtin-baseline` 고정 | 의존성 버전이 저장소에 적힘, CI 바이너리 캐시 | 첫 빌드가 길다(gRPC·protobuf) | 채택 |
| 시스템 패키지(apt/dnf)로 의존성 | 빠름 | 머신마다 버전이 다름 | 기각 |
| CMake 프리셋 dev/release/asan/tsan/tidy | 한 명령으로 같은 플래그 | 프리셋 JSON이 길다 | 채택. tidy는 별도 프리셋으로 분리해 평소 빌드를 느리게 하지 않음 |
| 경고를 에러로(`-Wall -Wextra -Wpedantic -Werror`, C# `TreatWarningsAsErrors` + analyzers `latest-all`) | 리뷰에서 걸릴 것을 컴파일러가 거름 | 외부 헤더 경고를 `-isystem`으로 막아야 함 | 채택 |
| clang-tidy 검사 묶음(bugprone, cert, cppcoreguidelines, modernize, performance, readability) | 규칙 1·2(숫자 리터럴, 강타입)를 기계가 지킴 | 오탐은 `NOLINT`로 사유와 함께 | 채택 |
| CI의 LLVM 버전: 러너 기본(18) vs 로컬과 같은 21 | 기본은 설치가 빠름 | 판정이 달라 로컬 통과가 CI 실패가 됨 | 21 고정 ([issues/001](../issues/001-ci-clang-tidy-version-mismatch.md)) |

## 구현
- `.clang-format`(Google 기반, 4칸, 100열), `.clang-tidy`, `.editorconfig`(C# 네이밍·분석기 심각도)
- `CMakePresets.json`, `cmake/CompilerWarnings.cmake`(`sa::warnings`), `cmake/SaTarget.cmake`(`sa_apply_policy`, `sa_add_test`), `cmake/Sanitizers.cmake`, `cmake/StaticAnalysis.cmake`
- `vcpkg.json`(baseline 고정), `scripts/format.sh`, `scripts/check-format.sh`
- `.github/workflows/ci.yml`: C++ dev/asan/tidy 매트릭스 + .NET. 각 잡이 자기 트리의 존재를 먼저 확인해(루트 `CMakeLists.txt`, `GameServer.slnx`) 아직 없는 단계에서는 아무 일 없이 통과한다. LLVM major는 `LLVM_MAJOR` 하나로.
- `CONTRIBUTING.md`: 브랜치 규칙과 PR 완료 체크리스트

## 만난 문제
- [001 CI의 tidy 잡이 로컬에서 통과한 코드에 실패](../issues/001-ci-clang-tidy-version-mismatch.md) — 러너의 clang-tidy 18 vs 로컬 21. CI를 21로 고정.

## 검증
- 이 PR의 CI 네 잡이 녹색(C++ 잡은 트리가 없어 probe에서 끝남, .NET 동일). 실제 코드가 들어오는 다음 PR부터 전체 단계가 돈다.
- 로컬: 같은 프리셋·스크립트가 이후 모든 피처의 완료 조건이다.

## 남긴 것
- CI 바이너리 캐시가 처음 채워지는 PR(03 contracts)은 gRPC 빌드로 30~40분 걸린다. 캐시가 차면 수 분.
- `tsan` 프리셋은 정의만 있고 CI에서 돌리지 않는다. 워커 스레드가 생기는 단계(8c)에서 다시 본다.

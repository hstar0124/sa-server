# 004 CI의 clang-tidy가 CMake의 모듈 스캔 플래그를 거부

- 상태: 해결 (04의 `build: turn off C++ module scanning ...` 커밋)
- 발견: [04 common](../features/04-cpp-common.md) PR #4의 두 번째 CI, 2026-10-14
- 영향: `C++ (tidy)`만 실패. dev·asan은 같은 코드로 통과

## 증상
의존성 캐시가 채워진 뒤 dev(3분)·asan(4분)은 통과하고 tidy가 3분 만에 실패했다.

```
error: unknown argument: '-fdeps-format=p1689r5' [clang-diagnostic-error]
error: unknown argument: '-fmodule-mapper=server/cpp/common/CMakeFiles/sa_common.dir/src/config_source.cpp.o.modmap'
error: unknown argument: '-fmodules-ts' [clang-diagnostic-error]
config_source.hpp:41:31: error: no template named 'expected' in namespace 'std'
```

로컬의 `--preset tidy`는 같은 커밋에서 경고 0이었다.

## 원인 분석
- 앞의 세 줄은 GCC 전용 C++20 모듈 플래그다. 우리는 모듈을 쓰지 않는데 컴파일 명령에 들어 있었다.
- `std::expected`가 없다는 오류는 그 결과로 봤다. 드라이버 인자 오류 뒤 clang이 `-std=c++23` 없이 파싱하면 C++17 기본값이라 `<expected>`가 비어 있다. 첫 오류를 없애면 함께 사라질 것으로 예상했고 그렇게 됐다.
- 로컬과 무엇이 다른지: 로컬 CMake는 3.31.8이고 `build/tidy/compile_commands.json`에 `-fmodules-ts`가 0회. CI 러너는 이미지에 들어 있는 CMake를 썼고(vcpkg는 자기용으로 4.4.3을 따로 받는다) 그 버전은 CXX 표준이 20 이상이면 모든 컴파일 명령에 모듈 스캔 플래그를 붙였다. 툴체인 버전이 로컬과 다르다는 점에서 [001](001-ci-clang-tidy-version-mismatch.md)과 같은 종류의 문제다.

## 원인
CMake의 C++ 모듈 스캔 기본값(`CMAKE_CXX_SCAN_FOR_MODULES`)이 켜진 채 GCC로 빌드하면, clang으로 다시 파싱하는 clang-tidy가 GCC 전용 플래그를 받는다. 그리고 CI의 CMake 버전이 고정되어 있지 않았다.

## 해결
1. 루트 `CMakeLists.txt`에 `set(CMAKE_CXX_SCAN_FOR_MODULES OFF)`. 모듈을 쓰지 않으니 스캔도 필요 없다. CMake 버전과 관계없이 문제를 없앤다.
2. CI의 CMake를 로컬과 같은 3.31.8로 고정(`CMAKE_VERSION`). 러너 이미지가 새 major로 넘어가도 빌드 도구가 바뀌지 않는다. 처음엔 `pipx install cmake==3.31.8`로 했는데 PyPI의 cmake 휠에 3.31.8이 없어(3.31.6 다음이 3.31.10) 세 잡이 설치 단계에서 실패했다. Kitware 공식 릴리스 압축 파일을 받아 쓰도록 바꿨다. 패키지 저장소마다 올라오는 버전이 다르다는 것도 고정할 때 확인할 항목이다.

## 재발 방지
- CONTRIBUTING.md의 "도구 버전"에 CMake를 추가했다. 로컬 LLVM·CMake 버전과 CI 변수를 같이 바꾼다.
- 의존성 캐시는 이 실패와 무관하게 이미 저장되어 있어 다시 빌드하지 않는다.

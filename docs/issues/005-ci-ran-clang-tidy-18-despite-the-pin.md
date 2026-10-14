# 005 CI가 LLVM 21을 설치하고도 clang-tidy 18을 실행함

- 상태: 해결 (04의 `ci: put LLVM 21 first on PATH ...` 커밋)
- 발견: [04 common](../features/04-cpp-common.md) PR #4의 네 번째 CI, 2026-10-15
- 영향: [001](001-ci-clang-tidy-version-mismatch.md)의 해결이 실제로는 적용된 적이 없었다. [004](004-clang-tidy-rejects-cmake-module-scanning-flags.md)의 원인 분석 일부가 틀렸다

## 증상
모듈 스캔을 끄고 CMake를 3.31.8로 고정한 뒤에도 tidy만 같은 오류로 실패했다. 모듈 플래그 오류는 사라졌고 이것이 남았다.

```
config_source.hpp:41:31: error: no template named 'expected' in namespace 'std' [clang-diagnostic-error]
```

## 원인 분석
- 004에서는 이 오류를 "모듈 플래그 오류의 여파"로 보았다. 모듈 플래그가 사라졌는데도 남았으니 그 가설은 틀렸다.
- Ubuntu 24.04 컨테이너에 같은 툴체인(g++-14, apt.llvm.org의 clang-tidy-21)을 깔고 `std::expected`를 쓰는 파일을 검사해 보니 통과했다. clang 21의 `__cpp_concepts`는 202002, libstdc++ 14는 그 값을 요구한다. 툴체인 자체는 문제가 없다.
- 그래서 CI 로그에서 실제로 실행된 도구를 다시 확인했다. 설치 단계 끝의 `clang-tidy --version`이 **`Ubuntu LLVM version 18.1.3`**을 찍고 있었다. clang-tidy-21은 설치되었지만(`Setting up clang-tidy-21 (1:21.1.8...)`), 러너 이미지의 `/usr/bin/clang-tidy`는 alternatives가 관리하지 않는 일반 링크라서 `update-alternatives --install`이 그것을 바꾸지 않았다(경고만 내고 넘어감). 
- clang 18의 `__cpp_concepts`는 201907이라 libstdc++ 14가 `std::expected`를 감춘다. 오류가 정확히 설명된다.

## 원인
CI 단계가 "설치"만 확인하고 "실행되는 도구의 버전"을 확인하지 않았다. 001의 수정은 처음부터 효과가 없었고, 그때까지는 C++ 코드가 없어 tidy가 실제로 돈 적이 없어서 드러나지 않았다.

## 해결
- `/usr/local/bin`(러너 PATH에서 `/usr/bin`보다 앞)에 `clang-tidy-21`, `clang-format-21`을 가리키는 링크를 둔다.
- 설치 단계가 두 도구의 `--version`을 검사해 major가 `LLVM_MAJOR`가 아니면 **즉시 실패**한다. 다음에 같은 일이 생기면 3분 뒤 tidy 오류가 아니라 설치 단계에서 이유와 함께 멈춘다.

## 재발 방지
- 버전을 고정하는 모든 도구에 "실행되는 버전" 검사를 둔다(CMake는 `cmake --version` 출력이 로그에 남는다).
- 가설을 고친 기록: 004의 "`std::expected` 오류는 모듈 플래그의 여파" 부분은 틀렸고, 이 문서가 바로잡는다.

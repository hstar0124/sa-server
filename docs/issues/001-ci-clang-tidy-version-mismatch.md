# 001 CI의 tidy 잡이 로컬에서 통과한 코드에 실패

- 상태: 해결 (`ci: pin LLVM 21 ...` 커밋, 실제 적용은 [005](005-ci-ran-clang-tidy-18-despite-the-pin.md)에서)
- 발견: [01 저장소 도구](../features/01-repo-tooling.md), 2026-10-14
- 영향: CI가 빨간 채로 병합이 이어지면 "tidy 통과"가 로컬 주장에 그친다

## 증상
이전 저장소에서 같은 CI 정의로 `main` 푸시 8회가 모두 실패했다. 네 잡 중 `.NET`, `C++ (dev)`, `C++ (asan)`은 통과하고 `C++ (tidy)`만 Build 단계에서 실패했다. 로컬에서는 `cmake --build --preset tidy`가 경고 0으로 끝나는 같은 커밋이었다.

## 원인 분석
GitHub 러너는 Ubuntu 24.04의 `clang-tidy-18`을 쓰고, 개발 머신은 LLVM 21.1이다. `.clang-tidy`는 `bugprone-*`, `cppcoreguidelines-*`, `modernize-*`, `performance-*`, `readability-*` 묶음을 `-warnings-as-errors`로 켜는데, 같은 이름의 검사라도 major 사이에 판정이 달라지는 것이 여럿이다(예: `misc-const-correctness`, `bugprone-unchecked-optional-access`의 흐름 분석, `modernize-use-*` 계열). 로컬에서 "경고 0"으로 맞춘 코드가 18에서는 경고를 내고, `-Werror` 취급이라 빌드가 실패한다. 로그는 저장소가 비공개라 인증 없이 읽을 수 없었고, 버전 차이 외에는 통과한 세 잡과 다른 변수가 없었다.

## 원인
정적 분석 도구의 버전이 로컬과 CI에서 다르다. 검사 집합이 같아도 판정이 같다는 보장은 같은 major 안에서만 있다.

## 해결
CI가 apt.llvm.org에서 LLVM 21(`clang-tidy-21`, `clang-format-21`)을 설치하고 `update-alternatives`로 기본 명령에 연결한다. 버전은 워크플로 상단의 `LLVM_MAJOR` 하나에서 정한다. 대안으로 `.clang-tidy`를 18에 맞추는 것은 로컬이 21이라 반대 방향의 같은 문제를 만들고, 검사 집합을 줄이는 것은 품질 기준을 내리는 일이라 택하지 않았다.

## 재발 방지
- CONTRIBUTING.md에 "로컬 LLVM major = CI `LLVM_MAJOR`"를 적고, 둘을 바꿀 때 같이 바꾼다.
- CI는 PR마다 돈다(이전 저장소는 `main` 푸시에만 돌아 실패가 뒤늦게 보였다). 브랜치 룰셋이 네 잡의 통과를 병합 조건으로 요구한다.

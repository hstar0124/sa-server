# 002 C++ 컴포넌트가 네 피처 동안 루트 CMake 프로젝트 없이 커밋됨

- 상태: 해결 (04의 `build: root CMake project ...` 커밋)
- 발견: [04 common](../features/04-cpp-common.md), 2026-10-14
- 영향: "각 커밋이 빌드되고 테스트가 통과한다"는 규칙이 04~08 구간에서 거짓이 된다. CI가 PR마다 돌면 그 구간의 PR은 전부 빨간색이다

## 증상
이전 저장소의 이력에서 루트 `CMakeLists.txt`는 09(loadgen 클라이언트)에서야 추가됐다. 04(common), 05(zone 시뮬레이션), 07(zone 서버), 08(gateway)의 커밋에는 컴포넌트별 `CMakeLists.txt`와 `CMakePresets.json`은 있지만, 프리셋이 가리키는 소스 루트에 프로젝트 파일이 없어 `cmake --preset dev`가 설정 단계에서 실패한다. 그 구간의 빌드는 개발 머신의 작업 트리에 있던 커밋되지 않은 루트 파일로 돌았던 것이다.

## 원인 분석
`git log --diff-filter=A -- CMakeLists.txt`가 09의 커밋 하나만 돌려줬다. 이름 변경도 아니었다(`--follow`도 같은 결과). 04 시점 트리(`git ls-tree`)에 루트 `CMakeLists.txt`가 없다. CI는 `main` 푸시에만 돌았고 첫 푸시가 09 이후라 드러나지 않았다.

## 원인
루트 프로젝트 파일을 작업 트리에서만 쓰고 커밋 범위에 넣지 않았다. "커밋이 빌드된다"를 작업 트리에서만 확인했다.

## 해결
04에서 contracts와 common만 포함하는 루트 `CMakeLists.txt`를 커밋한다. 이후 컴포넌트는 자기 `add_subdirectory` 줄을 같은 PR에서 추가한다. 이 저장소에서는 CI가 PR마다 전체 프리셋을 빌드하므로 같은 누락은 병합 전에 걸린다.

## 재발 방지
- CI가 PR마다 C++ dev/asan/tidy를 돌린다(01).
- 커밋 전 검증을 "작업 트리"가 아니라 "커밋된 트리"로 한다. 확인은 `git stash -u` 뒤 빌드, 또는 별도 worktree에서 한다. CONTRIBUTING.md에 적었다.

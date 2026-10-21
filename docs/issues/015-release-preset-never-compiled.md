# 015 release 프리셋이 한 번도 컴파일되지 않음

- 상태: 해결 (`-Wnull-dereference`를 Debug 빌드로 한정, 2026-10-21)
- 발견: 37 컨테이너 이미지를 만들며 release 프리셋으로 빌드하다, 2026-10-21
- 영향: 저장소를 만든 뒤로 `cmake --preset release` 빌드가 실패했다. 개발·CI·검증은 모두 Debug 프리셋(dev, asan, tidy)만 써서 아무도 몰랐다. 최적화된 바이너리로 잰 숫자도 아직 없다

## 증상
release 프리셋(`-O2`)으로 Zone·Gateway를 빌드하면 asio 헤더 안에서 경고가 나고, `-Werror` 때문에 빌드가 멈췄다.

```
build/vcpkg_installed/x64-linux/include/asio/io_context.hpp:967:27: error: potential null pointer dereference [-Werror=null-dereference]
cc1plus: all warnings being treated as errors
```

`lease_keeper.cpp`, `tick_runner.cpp` 등 `io_context`를 쓰는 파일마다 같은 오류가 났다.

## 원인 분석
- GCC의 `-Wnull-dereference`는 최적화 단계(경로 분석)에서 동작한다. `-O0`에서는 그 분석이 거의 돌지 않아 Debug 빌드에서는 조용하다.
- `-O2`에서는 asio의 `io_context` 코드가 우리 함수에 인라인된 뒤 분석되고, 포인터가 null이 아님을 증명하지 못한 경로를 보고한다. 실제로 null이 될 수 있는 경로는 아니다.
- vcpkg 헤더는 `-isystem`으로 들어와 보통 경고가 억제되지만, 인라인된 뒤 중간 단계에서 나는 경고는 시스템 헤더 억제를 벗어난다.
- CI의 C++ 잡은 dev·asan·tidy 셋이고, 로컬 검증 스크립트도 같다. release 프리셋은 `CMakePresets.json`에만 있었다.

## 원인
경고 정책은 처음부터 모든 구성에 같은 플래그를 걸었는데, 그중 하나는 최적화 수준에 따라 결과가 달라진다. 그 구성으로 빌드하는 곳이 없어서 실패가 드러나지 않았다.

## 해결
`-Wnull-dereference`를 `$<$<CONFIG:Debug>:...>`로 Debug 빌드에만 건다(`cmake/CompilerWarnings.cmake`). Debug 프리셋(dev, asan, tidy)은 이 분석을 그대로 받는다.

다른 방법도 검토했다.
- release에서 `-Wno-error=null-dereference`: 빌드 로그마다 같은 거짓 경고가 수십 줄 쌓여, 진짜 경고를 가린다.
- asio를 감싸는 `#pragma GCC diagnostic`: 인라인된 위치에서 나는 경고라 감싸는 자리가 마땅치 않고, `io_context`를 쓰는 파일마다 붙여야 한다.

## 재발 방지
- 컨테이너 이미지(`deploy/docker/cpp.Dockerfile`)가 release 프리셋으로 빌드한다. 이미지를 만들 때마다 release 빌드가 돈다.
- CI의 C++ 매트릭스에 release를 더했다(39). 최적화된 빌드에서 테스트까지 돈다. 룰셋의 필수 검사에도 넣는다.

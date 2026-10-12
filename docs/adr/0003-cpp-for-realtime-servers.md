# ADR-0003: Gateway·Zone 언어는 C++ (C++23, GCC 14), 툴체인 고정

- 상태: Accepted
- 날짜: 2026-10-12

## 맥락
실시간 서버(Gateway, Zone, loadgen) 언어로 C++과 Rust를 비교했다. Rust는 소유권 규칙을 컴파일러가 강제하고 tonic/prost로 gRPC·Protobuf 의존성이 가볍다. C++은 국내 게임 서버 채용 시장의 주류이며 작성자의 주력 언어다. 포트폴리오는 읽는 사람이 평가할 수 있는 언어로 써야 한다.

## 결정
- **C++**을 사용한다. 표준은 **C++23**(`-std=c++23`, GCC 14)으로 고정한다: `std::expected`, `std::byteswap`, `std::in_range` 등 라이브러리 기능을 쓰기 위해서다. 언어 기능은 C++20 범위(concepts, ranges, designated initializers)를 주로 사용한다. Rust는 채택하지 않는다.
- 툴체인: `gcc-toolset-14`(GCC 14.2), CMake ≥ 3.28 + Ninja, **vcpkg manifest mode**(`vcpkg.json`, `builtin-baseline` 고정), clang-format/clang-tidy 21.
- 의존성: gRPC, Protobuf, standalone Asio, spdlog, toml++, GoogleTest. 추가는 ADR 또는 PR 설명에 사유 기록.
- 컴파일 옵션: `-Wall -Wextra -Wpedantic -Werror`. CI에 ASan+UBSan 프리셋, TSan 프리셋을 둔다.
- "모던 C++을 제대로 쓴다"가 포트폴리오 메시지다: 소유권은 타입으로(`unique_ptr`, 값 의미론), raw `new/delete` 금지, `std::chrono`, `std::span`, `std::expected`, 강타입 ID.

## 결과
- gRPC C++ 빌드(vcpkg)는 초회 30~60분이 걸린다. 바이너리 캐시로 재사용한다.
- 메모리 안전은 리뷰·sanitizer·clang-tidy로 지킨다. CI 없는 코드는 신뢰하지 않는다.

## 대안
- Rust: 기술적으로 우수하나 작성자의 유창성과 목표 시장에 맞지 않음. 기각.
- 전체 C#: Zone tick 성능 측정이라는 핵심 주장을 약화. 기각.

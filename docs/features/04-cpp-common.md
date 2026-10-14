# 04 (0단계) C++ 공통 라이브러리: 강타입 ID, 프레이밍, 설정, HMAC, 인스턴스 ID, lease

## 배경과 목표
Gateway·Zone·봇이 공유할 바닥을 먼저 깐다. 길이 프리픽스 프레임 코덱과 그 위의 메시지 연결, TOML + 환경 변수 설정 읽기, 티켓 서명용 HMAC, 프로세스 인스턴스 ID, Backend lease 추적. 완료 조건: 각 부품에 단위 테스트가 있고, 루트 CMake 프로젝트로 전체가 한 번에 빌드되며, CI의 C++ 세 잡이 처음으로 실제 코드를 검사해 녹색이다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| ID 표현 | `std::uint64_t` 그대로 / 태그 템플릿 `StrongId<Tag>` | 강타입. 캐릭터 ID 자리에 계정 ID를 넘기면 컴파일 에러(규칙 2) |
| 프레이밍 | 메시지마다 소켓 코드 / 길이 프리픽스 코덱 하나 + `FramedConnection` | 하나. 모든 링크(클라이언트, Gateway↔Zone)가 같은 코드를 탄다. 한계값은 `protocol_limits.hpp`와 `framing.proto`가 같은 값 |
| 설정 | 코드 상수 / TOML 파일 + `SA_<PROC>__<SECTION>__<KEY>` 환경 변수 override, 시작 시 검증 | 후자(규칙 1). 설정 구조체 하나에 모든 튠값 |
| 서명 | 라이브러리 JWT / HMAC-SHA256 직접 | HMAC-SHA256(OpenSSL). 티켓은 작고 필드가 정해져 있어 JWT 파서가 불필요 |
| lease | 타이머 콜백 흩어두기 / `LeaseKeeper` 하나가 갱신·만료·상실 통지 | 하나. Zone과 Gateway가 같은 코드로 lease를 유지 |
| 루트 CMake | 컴포넌트별 파일만 / 루트 프로젝트 커밋 | 루트 프로젝트를 이 PR에 넣는다 ([issues/002](../issues/002-cpp-tree-without-a-root-cmake-project.md)) |

## 구현
- `server/cpp/common/include/sa/common/`: `strong_id.hpp`, `ids.hpp`, `instance_id.hpp`, `framing.hpp`, `framed_connection.hpp`, `config_source.hpp`, `hmac.hpp`, `lease.hpp`, `lease_keeper.hpp`, `logging.hpp`, `protocol_limits.hpp`, `wall_clock.hpp`, `geometry.hpp`, `grpc_call.hpp`, `bytes.hpp`
- `server/cpp/common/src/`와 `tests/`(strong_id, framing, config_source, hmac, instance_id, lease, bytes)
- 루트 `CMakeLists.txt`: 프리셋이 설정하는 프로젝트. contracts와 common을 포함하고, 이후 컴포넌트가 한 줄씩 더한다

## 만난 문제
- [002 C++ 컴포넌트가 루트 CMake 프로젝트 없이 커밋됨](../issues/002-cpp-tree-without-a-root-cmake-project.md) — 이전 이력의 04~08은 프리셋으로 빌드되지 않았다. 여기서는 루트 프로젝트를 같은 PR에 넣고, 커밋된 트리를 별도 worktree에서 빌드해 확인했다.

## 검증
- 별도 worktree에서 커밋된 트리로: `cmake --preset dev` 빌드 + 테스트 26개 통과, `--preset tidy` 경고 0(clang-tidy 21), `--preset asan` 26개 통과, clang-format 검사 통과.
- 이 PR부터 CI의 C++ 세 잡이 실제로 빌드한다. 첫 실행은 vcpkg 캐시가 비어 있어 gRPC·protobuf 빌드로 길다. 캐시가 차면 수 분.

## 남긴 것
- `grpc_call.hpp`(비동기 단항 호출 도우미)는 Zone 서버(07)가 처음 쓴다.
- `geometry.hpp`는 Zone 시뮬레이션(05)이 쓴다. 공통에 둔 이유는 봇 두뇌도 같은 벡터 타입을 쓰기 때문이다.

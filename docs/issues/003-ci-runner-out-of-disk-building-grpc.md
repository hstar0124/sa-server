# 003 CI 러너가 gRPC 의존성을 빌드하다 디스크가 가득 참

- 상태: 해결 (04의 `ci: build vcpkg dependencies once ...` 커밋)
- 발견: [04 common](../features/04-cpp-common.md) PR #4의 첫 CI, 2026-10-14
- 영향: C++ 코드가 처음 들어온 PR부터 C++ 세 잡이 50분 넘게 돌다 실패. 병합 조건을 만족할 수 없다

## 증상
`C++ (dev)` 51분, `C++ (tidy)` 55분 뒤 실패. 로그 끝:

```
/usr/bin/ar: unable to copy file 'libgrpc_unsecure.a'; reason: No space left on device
error: building grpc:x64-linux failed with: BUILD_FAILED
CMake Error: CMake was unable to find a build program corresponding to "Ninja".
##[error]Process completed with exit code 1.
```

우리 코드(common 라이브러리와 테스트)는 컴파일 단계에 들어가지도 못했다. 로컬에서는 vcpkg 바이너리 캐시가 이미 차 있어 이 비용이 보이지 않았다.

## 원인 분석
- 첫 줄이 직접 원인: `ar`가 쓰기에 실패. 마지막 Ninja 오류는 디스크가 찬 뒤 CMake가 임시 파일을 못 쓴 결과로 보고 따로 쫓지 않았다(ninja-build는 설치 단계에서 정상 설치됨).
- GitHub의 ubuntu-24.04 러너는 루트 파일시스템 여유가 약 14 GB다. 기본 트리플릿 `x64-linux`는 포트마다 debug와 release를 둘 다 빌드하고, gRPC·protobuf·abseil의 build tree(오브젝트 파일)가 그 두 벌을 동시에 들고 있다. gRPC 하나의 build tree가 수 GB다.
- 세 잡(dev·asan·tidy)이 같은 의존성을 각자 처음부터 빌드하고 있었다. 캐시 키는 같지만 셋 다 캐시가 비어 있는 상태로 동시에 시작했으니 같은 일을 세 번 했다.

## 원인
러너 디스크에 비해 의존성 빌드가 크다(두 벌 + build tree 보존), 그리고 같은 빌드를 세 잡이 병렬로 반복한다.

## 해결
1. **의존성 전용 잡(`C++ dependencies`)** 하나가 먼저 돌고 바이너리 캐시를 저장한다. C++ 세 잡은 `needs: deps`로 기다렸다가 캐시를 복원만 한다(`fail-on-cache-miss`). 의존성 빌드는 한 번.
2. **release 전용 트리플릿 `x64-linux-release`**를 CI에서 쓴다. 포트당 한 벌이라 시간과 디스크가 절반. 우리 코드는 여전히 각 프리셋의 모드(Debug, ASan)로 빌드되고 release 라이브러리와 링크된다(libstdc++ 디버그 모드를 쓰지 않으므로 ABI 문제가 없다).
3. **`--clean-after-build`**로 포트마다 빌드가 끝나면 build tree를 지운다.
4. 캐시가 없을 때만 러너의 안 쓰는 도구(.NET SDK, Android SDK, GHC, CodeQL, Boost, 약 30 GB)를 지운다.

대안: 자체 호스팅 러너(디스크 걱정 없음, 하지만 이 PC를 CI에 묶는다), 의존성을 미리 빌드한 컨테이너 이미지(가장 빠르지만 이미지 관리가 생긴다). 지금 규모에서는 위의 네 가지로 충분하다고 판단했다. 캐시가 찬 뒤 PR마다 C++ 잡은 수 분이다.

## 재발 방지
- 의존성 빌드는 `vcpkg.json`이 바뀔 때만 다시 돈다(캐시 키 = 트리플릿 + `vcpkg.json` 해시).
- 의존성을 추가하는 PR은 피처 문서의 "검증"에 CI 의존성 잡 시간을 적는다.

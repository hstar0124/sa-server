# 작업 규칙

## 브랜치
- `main`은 릴리스만. `develop`이 기본 브랜치. 둘 다 PR로만 바뀐다(룰셋: PR 필수, CI 네 잡 통과, merge commit만 허용).
- 피처는 `feature/<영역>-<주제>`, 수정은 `fix/<영역>-<증상>`, 릴리스는 `release/x.y.z`, 핫픽스는 `hotfix/x.y.z`. 이름은 소문자 kebab-case 명사구.
- 커밋은 Conventional Commits(`feat(zone): ...`, `fix(loadgen): ...`, `docs: ...`)에 본문(무엇을 왜). 컴포넌트별로 나누고, 각 커밋이 빌드·테스트를 통과한다. 공동 작업 표기(Co-Authored-By 등)는 넣지 않는다.

## PR 하나의 완료 절차
- [ ] 피처 문서 `docs/features/NN-slug.md`를 쓰고, 그 글을 PR 본문으로 붙인다. `docs/features/README.md` 표에 줄 추가.
- [ ] 작업 중 만난 문제는 `docs/issues/NNN-slug.md` + 색인 표. 보고서·피처 문서는 링크만.
- [ ] 설계 변경이면 `docs/adr/`에 ADR, "왜"가 길면 `docs/design-notes/`, 측정은 `docs/reports/`.
- [ ] C++: `cmake --build --preset dev && ctest --preset dev`, `cmake --build --preset tidy`, `cmake --build --preset asan && ctest --preset asan`, `scripts/check-format.sh`.
- [ ] C#: `dotnet build` 경고 0, 통합 테스트(Testcontainers) 통과, `dotnet format --verify-no-changes`.
- [ ] end-to-end: `scripts/e2e-local.sh <프로필>` PASS(3단계 이후).
- [ ] 단계 끝이면 `scripts/bench/capacity.sh` 50~2,000 재측정, 보고서에 비교(7단계 이후).
- [ ] 검증은 **커밋된 트리**로 한다: `git stash -u` 뒤 빌드하거나 별도 worktree(`git worktree add`)에서 빌드한다. 작업 트리에만 있는 파일에 기대지 않는다.
- [ ] README에 **이 PR이 만든 것**을 더한다(구조·흐름·데이터 기준의 해당 부분). 미래 단계의 내용이나 아직 하지 않은 측정은 적지 않는다. 측정이 있었으면 "성능과 개선"(부하별 리소스 표, 개선 사례: 증상 → 원인 → 조치 → 다시 잰 결과)과 `docs/reports/`를 갱신한다.
- [ ] 릴리스: `release/x.y.z` PR → `main` 병합 → 태그 `vx.y.z` → Release 노트(그 단계 피처 문서 요약) → `develop`에 다시 병합.

## 도구 버전
- GCC 14(gcc-toolset-14), vcpkg `builtin-baseline` 고정.
- CMake 3.31.8. CI는 `.github/workflows/ci.yml`의 `CMAKE_VERSION`으로 같은 버전을 설치한다.
- LLVM(clang-tidy·clang-format) major는 로컬과 CI(`.github/workflows/ci.yml`의 `LLVM_MAJOR`)가 같아야 한다. 지금은 21.
- .NET 10 SDK.

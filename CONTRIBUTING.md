# 작업 규칙

## 브랜치
- `main`은 릴리스만. `develop`이 기본 브랜치. 둘 다 PR로만 바뀐다. 룰셋 `protect-main-branch`가 두 브랜치에 PR 필수, CI 여섯 잡(C++ dependencies, dev, release, asan, tidy, .NET) 통과, merge commit만 허용, 삭제·강제 푸시 금지를 건다. 룰셋의 시행 상태는 Active여야 한다(이슈 010).
- 피처는 `feature/<영역>-<주제>`, 수정은 `fix/<영역>-<증상>`, 릴리스는 `release/x.y.z`, 핫픽스는 `hotfix/x.y.z`. 이름은 소문자 kebab-case 명사구.
- 커밋은 Conventional Commits(`feat(zone): ...`, `fix(loadgen): ...`, `docs: ...`)에 본문(무엇을 왜). 컴포넌트별로 나누고, 각 커밋이 빌드·테스트를 통과한다. 공동 작업 표기(Co-Authored-By 등)는 넣지 않는다.

## PR 하나의 완료 절차
- [ ] 피처 문서 `docs/features/NN-slug.md`를 쓰고, 그 글을 PR 본문으로 붙인다. `docs/features/README.md` 표에 줄 추가.
- [ ] 작업 중 만난 문제는 `docs/issues/NNN-slug.md` + 색인 표. 보고서·피처 문서는 링크만.
- [ ] 설계 변경이면 `docs/adr/`에 ADR, "왜"가 길면 `docs/design-notes/`, 측정은 `docs/reports/`.
- [ ] C++: `cmake --build --preset dev && ctest --preset dev`, `cmake --build --preset tidy`, `cmake --build --preset asan && ctest --preset asan`, `scripts/check-format.sh`.
- [ ] C#: `dotnet build` 경고 0, 통합 테스트(Testcontainers) 통과, `dotnet format --verify-no-changes`. 패키지를 더하거나 바꾸면 `dotnet restore server/dotnet/GameServer.slnx --force-evaluate`로 `packages.lock.json`을 갱신해 같은 커밋에 넣는다. CI는 `--locked-mode`로 복원하므로 잠금 파일이 낡으면 .NET 잡이 실패한다.
- [ ] end-to-end: `scripts/e2e-local.sh <프로필>` PASS(3단계 이후).
- [ ] 단계 끝이면 `scripts/bench/capacity.sh` 50~2,000 재측정, 보고서에 비교(7단계 이후).
- [ ] 검증은 **커밋된 트리**로 한다: `git stash -u` 뒤 빌드하거나 별도 worktree(`git worktree add`)에서 빌드한다. 작업 트리에만 있는 파일에 기대지 않는다.
- [ ] README에 **이 PR이 만든 것**을 더한다(구조·흐름·데이터 기준의 해당 부분). 미래 단계의 내용이나 아직 하지 않은 측정은 적지 않는다. 측정이 있었으면 "성능과 개선"(부하별 리소스 표, 개선 사례: 증상 → 원인 → 조치 → 다시 잰 결과)과 `docs/reports/`를 갱신한다.
- [ ] 릴리스: `release/x.y.z`에서 바꾸는 것은 버전(루트 `CMakeLists.txt`의 `project(... VERSION ...)` 한 곳)과 피처 색인의 릴리스 칸뿐이다. `vcpkg.json`에는 제품 버전을 두지 않는다(이슈 008). `release/x.y.z` PR → `main` 병합 → 태그 `vx.y.z` → Release 노트(그 단계 피처 문서 요약) → `main`을 머리로 `develop`에 되돌리는 PR. 이 PR을 병합한 뒤 "Delete branch"를 누르지 않는다. 태그는 만들기 전에 로컬에 같은 이름이 없는지 보고, 생성과 푸시를 `git tag -a vx.y.z <커밋> -m ... && git push origin vx.y.z`로 묶는다(이슈 014). 마지막에 원격 `main`의 머리가 태그 커밋과 같은지 본다(이슈 010).

## 도구 버전
- GCC 14(gcc-toolset-14), vcpkg `builtin-baseline` 고정.
- CMake 3.31.8. CI는 `.github/workflows/ci.yml`의 `CMAKE_VERSION`으로 같은 버전을 설치한다.
- LLVM(clang-tidy·clang-format) major는 로컬과 CI(`.github/workflows/ci.yml`의 `LLVM_MAJOR`)가 같아야 한다. 지금은 21.
- .NET 10 SDK.
- CI 시간: 공개 저장소라 GitHub 기본 러너에 시간 한도가 없다. 비공개로 돌리면 계정의 월 무료 시간(무료 계정 2,000분)이 다시 적용된다(이슈 009).

# 011 로컬 스크립트가 Backend에 엉뚱한 game-data 경로를 넘겨 기동 실패

- 상태: 해결 (22의 `fix(scripts): resolve the content directory from the repository root` 커밋)
- 발견: [22 Backend 경제 모듈](../features/22-backend-economy.md), 2026-10-19
- 영향: `scripts/e2e-local.sh`와 `scripts/dev-stack.sh`로 스택을 띄우면 Backend가 시작하자마자 죽는다. end-to-end와 브라우저 뷰어를 쓸 수 없다

## 증상
22의 커밋들을 옮긴 뒤 PR 전에 end-to-end를 돌리자 바로 실패했다. Backend 로그:

```
Unhandled exception. System.IO.InvalidDataException: game-data: content file not found:
/workspace/sa-server-wt2/server/dotnet/GameBackend/game-data/version.toml
```

## 원인 분석
- 경로가 저장소 루트의 `game-data`가 아니라 `server/dotnet/GameBackend/game-data`였다.
- 스크립트는 서브셸 안에서 먼저 `cd server/dotnet/GameBackend`를 하고, 그 뒤 환경 변수로 `Economy__ContentDir="$PWD/game-data"`를 넘긴다. `$PWD`는 그 시점에 이미 Backend 디렉터리다.
- 이전 이력에도 같은 줄이 있었다. 이전 이력은 27분 뒤 다음 피처 브랜치 안에서 `ROOT="$PWD"`로 고쳤다. 그러니 이전 이력의 이 피처는 스크립트가 깨진 채 병합됐다. 통합 테스트는 하네스가 경로를 직접 넘겨 통과했기 때문에 드러나지 않았다.

## 원인
`cd` 뒤에 `$PWD`를 쓴 경로 계산. Backend의 통합 테스트는 이 스크립트를 거치지 않아서, 스크립트 경로는 end-to-end를 돌려야만 검증된다.

## 해결
두 스크립트가 시작할 때 저장소 루트를 `ROOT="$PWD"`로 잡고 `Economy__ContentDir="$ROOT/game-data"`를 넘긴다. 이전 이력의 수정 커밋을 이 PR로 당겨 왔다. end-to-end 두 프로필이 통과하고, 대화형 스택도 Backend가 정상으로 뜬다.

## 재발 방지
- PR마다 커밋된 트리로 end-to-end를 돌리는 것이 이미 체크리스트에 있다. 이번에 그 단계가 이 결함을 잡았다.
- 이전 이력을 옮길 때 "그 피처의 커밋"만 보지 않고, 같은 파일을 고친 바로 다음 수정 커밋이 있는지 확인한다(`git log -G'<바뀐 줄>' -- <파일>`). 15의 겹친 테스트도 같은 이유였다.

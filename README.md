# sa-server

MMORPG 서버 포트폴리오. `sa`는 Server Architecture의 약자로, 설계 문서(`docs/Server_Architecture.md`)에서 출발해 Gateway·Zone(C++23)과 Backend·EventWorker(.NET 10)를 단계별로 만들며 각 단계의 결정(ADR), 이유(설계 노트), 측정(보고서), 작업 서사(피처 문서)를 함께 남긴다.

진행은 gitflow로 한다. 모든 변경은 `feature/*` 브랜치에서 PR로 `develop`에 들어가고, 단계가 끝나면 `release/x.y.z` PR로 `main`에 병합해 태그를 단다. 피처마다 `docs/features/`에 문서가 하나 있고 그 글이 PR 본문이다. 작업 중 만난 문제는 `docs/issues/`에 남긴다.

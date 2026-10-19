# 피처 문서

피처 브랜치 하나에 문서 하나. 이 글이 그 피처 PR의 본문이기도 하다(먼저 쓰고 PR에 붙인다). 번호는 병합 순서다.

| # | 피처 | 단계 | 릴리스 |
|---|---|---|---|
| [01](01-repo-tooling.md) | 저장소 도구: 스타일·정적 분석 정책, CMake 프리셋, vcpkg, CI | 0 | 0.1.0 |
| [02](02-design-docs.md) | 설계 문서 v1.0, ADR-0001~0008, 설계 노트 01~05, 작업 규칙 | 0 | 0.1.0 |
| [03](03-contracts.md) | Protobuf 계약: game / backend / events / internal 네 패키지 | 0 | 0.1.0 |
| [04](04-cpp-common.md) | C++ 공통: 강타입 ID, 프레이밍, 설정, HMAC, 인스턴스 ID, lease, 루트 CMake | 0 | 0.1.0 |
| [05](05-zone-simulation.md) | Zone 시뮬레이션: 이동·충돌·격자 AOI, game-data 로더, Wanderer 두뇌 | 2 | 0.1.0 |
| [06](06-backend.md) | GameBackend: 인증·캐릭터·WorldDirectory, 마이그레이션 0001~0003, PostgreSQL 통합 테스트 | 1 | 0.1.0 |
| [07](07-zone-server.md) | Zone 서버 호스트: 틱 러너, Gateway 링크, 세션 표, lease, 프로세스 안 봇 | 2 | 0.1.0 |
| [08](08-gateway.md) | Gateway: 티켓 검증, 세션 상태 기계, Zone 링크, 디렉터리 클라이언트 | 3 | 0.1.0 |
| [09](09-docs-architecture-into-readme.md) | 설계 문서를 README로 흡수: 현재 방향만 남기고 원문 제거 | – | 0.1.0 |
| [10](10-loadgen-client.md) | 네트워크 봇 클라이언트와 end-to-end: 로그인부터 입장까지 한 명령으로 | 3 | 0.1.0 |
| [11](11-gateway-websocket-transport.md) | WebSocket 전송: 브라우저가 같은 메시지로 Gateway에 붙는다 | 3b | |
| [12](12-web-viewer.md) | 브라우저 뷰어: 같은 프로토콜로 접속해 동기화를 눈으로 본다 | 3b | |
| [13](13-zone-combat-content.md) | 전투 콘텐츠: 클래스·스킬·몬스터를 데이터로 | 4 | |
| [14](14-zone-combat-sim.md) | 전투 시뮬레이션: 스킬·투사체·피해·사망·몬스터 AI | 4 | |
| [15](15-zone-combat-host.md) | 전투 호스트 연결: 스킬 요청을 받고 결과·피해·체력을 보낸다 | 4 | |
| [16](16-loadgen-fighter.md) | 전투 봇: 몬스터를 사냥하는 Fighter와 전투 end-to-end | 4 | |
| [17](17-ci-cache-miss-on-version-bump.md) | CI 의존성 캐시: 버전을 올려도 의존성을 다시 빌드하지 않는다 | – | |

## 형식

```
# NN <단계> <피처 제목: 결과 중심 한 문장>
## 배경과 목표        왜 지금 이것인가, 완료 조건
## 고민한 선택지와 결정  표(선택지·장점·단점·판단). 설계 변경이면 ADR 링크
## 구현              어느 모듈에 어떤 타입·테이블·메시지·설정 키가 생겼나
## 만난 문제          docs/issues/ 링크와 한 줄씩
## 검증              테스트 수(전후), ASan, tidy, e2e, 측정(보고서 링크)
## 남긴 것           다음으로 넘긴 항목, 보류한 결정
```

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
| [11](11-gateway-websocket-transport.md) | WebSocket 전송: 브라우저가 같은 메시지로 Gateway에 붙는다 | 3b | 0.2.0 |
| [12](12-web-viewer.md) | 브라우저 뷰어: 같은 프로토콜로 접속해 동기화를 눈으로 본다 | 3b | 0.2.0 |
| [13](13-zone-combat-content.md) | 전투 콘텐츠: 클래스·스킬·몬스터를 데이터로 | 4 | 0.2.0 |
| [14](14-zone-combat-sim.md) | 전투 시뮬레이션: 스킬·투사체·피해·사망·몬스터 AI | 4 | 0.2.0 |
| [15](15-zone-combat-host.md) | 전투 호스트 연결: 스킬 요청을 받고 결과·피해·체력을 보낸다 | 4 | 0.2.0 |
| [16](16-loadgen-fighter.md) | 전투 봇: 몬스터를 사냥하는 Fighter와 전투 end-to-end | 4 | 0.2.0 |
| [17](17-ci-cache-miss-on-version-bump.md) | CI 의존성 캐시: 버전을 올려도 의존성을 다시 빌드하지 않는다 | – | 0.2.0 |
| [18](18-repo-main-branch-restored.md) | 저장소 보호: 지워진 main을 되살리고 룰셋을 다시 시행한다 | – | |
| [19](19-docs-self-contained.md) | 설계 노트 자기완결화: 노트 하나만 읽어도 이해되게 | – | |
| [20](20-readme-diagrams.md) | README 다이어그램: 지금까지 만든 것을 그림으로 | – | |
| [21](21-economy-schema.md) | 경제 스키마: 아이템·지갑·영수증·요청 결과를 제약으로 | 5 | |
| [22](22-backend-economy.md) | Backend 경제 모듈: 처치 보상과 획득을 한 트랜잭션으로 | 5 | |
| [23](23-zone-drops-pickup.md) | Zone 드롭과 획득: 굴리고 예약하고, 확정은 Backend가 | 5 | |
| [24](24-zone-economy-review-fixes.md) | 23의 리뷰 수정: 획득과 처치 정산 보강 | 5 | |
| [25](25-verification-harness.md) | 검증 장치 수정: 통과가 실제로 무언가를 증명하게 | – | |
| [26](26-backend-review-fixes.md) | Backend 리뷰 수정: Zone 인스턴스 하나, 재시도 순서, 빠진 정합성 테스트 | – | |
| [27](27-zone-review-fixes.md) | Zone 리뷰 수정: 유한한 입력, 빠짐없는 델타, 상한 있는 큐, 막히지 않는 몬스터 | – | |
| [28](28-gateway-review-fixes.md) | Gateway 리뷰 수정: 남지 않는 세션, 해제되지 않는 연결, 끝이 있는 기다림 | – | |
| [29](29-cpp-rule-cleanup-errors-config.md) | C++ 규칙 정리 1: 오류는 타입으로, 설정은 엄격하게, 종료는 끝까지 | – | |
| [30](30-cpp-rule-cleanup-units-constants.md) | C++ 규칙 정리 2: 시간은 chrono로, ID는 타입으로, 수치는 데이터로 | – | |
| [31](31-backend-rule-cleanup.md) | Backend 규칙 정리: 타입 있는 ID, 결과 타입, 모듈 경계, DB 제약 | – | |
| [32](32-loadgen-looter.md) | 획득 봇: 떨어진 아이템을 줍는 Looter와 획득 end-to-end | 5 | |

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

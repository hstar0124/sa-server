# 10 (3단계) 네트워크 봇 클라이언트와 end-to-end: 로그인부터 입장까지 한 명령으로

## 배경과 목표
지금까지 Backend, Zone, Gateway를 각자 단위·통합 테스트로 검증했지만 셋이 실제로 맞물리는지는 아직 아무도 확인하지 않았다. 첫 클라이언트는 사람이 조작하는 게임이 아니라 시드 기반 봇이다(ADR-0006). 봇은 부하 생성기이면서 **검증기**다. 완료 조건: 봇이 가입 → 로그인 → 캐릭터 생성 → 입장 티켓 → Gateway TCP → Zone 입장 → 이동까지 하고, 받은 스냅샷이 서로 모순되지 않으며, 한 명령(`scripts/e2e-local.sh`)으로 전체 스택을 띄워 종료 코드로 판정한다. 이것으로 3단계(입장 경로)가 끝난다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 첫 클라이언트 | 게임 클라이언트 / 브라우저 / 봇 | 봇. 반복·대량·자동 판정이 된다. 브라우저 뷰어는 3b에서 |
| 봇의 두뇌 | 클라이언트에 이동 로직 / 05의 `sa::bot::Wanderer` 재사용 | 재사용. 프로세스 안 봇과 네트워크 봇이 같은 두뇌라 결과를 비교할 수 있다 |
| 판정 | 연결 성공만 / 월드 모델이 모순을 셈 | 모순을 셈. 모르는 엔티티의 갱신, 이미 보이는 엔티티의 재등장, 자기 자신이 시야에 등장, 처리된 입력 순번의 역행. 하나라도 있으면 실패 |
| 계정 준비 | 매번 새 계정 / 시드에서 계정 이름을 만들고 이미 있으면 로그인 | 시드에서 결정적으로. 같은 프로필을 다시 돌려도 계정이 쌓이지 않는다(가입이 409면 로그인) |
| 실행 환경 | 전부 컨테이너 / PostgreSQL만 compose, 나머지는 호스트 프로세스 | 후자. 개발 루프가 빠르고 로그를 바로 본다. K3s 배포는 6단계 |
| 끝난 뒤 검사 | 봇 결과만 / 디렉터리에 열린 세션이 0인지도 | 둘 다. Gateway가 끊긴 세션을 Backend에 알리는지까지 확인한다 |

## 구현
- `tools/loadgen/client/src/`: `backend_api`(cpr로 HTTP), `bot_session`(TCP 프레임 연결, Hello → 입력 루프 → ping), `world_model`(관심 집합 재구성과 모순 검사), `profile`(TOML 프로필), `report`(집계와 PASS 조건), `main`
- `tools/loadgen/client/profiles/smoke.toml`: 봇 5개, 10 s, 서로를 본 봇 2개 이상
- `tools/loadgen/client/tests/`: world_model, profile, report
- `scripts/e2e-local.sh`: compose PostgreSQL → 마이그레이션 → Backend → Zone → Gateway → loadgen → 열린 세션 검사. 서명 키는 실행마다 새로 만든다
- 루트 CMake에 `cpr`, `nlohmann_json` 패키지와 `tools/loadgen/client`. 두 패키지는 01부터 `vcpkg.json`에 있었으므로 CI 의존성 캐시는 그대로 맞는다
- README: 구조 표의 loadgen, "검증"의 end-to-end, 빌드 절의 실행 방법

## 만난 문제
- 없음. (이전 이력 그대로 가져와 처음부터 통과했다.)

## 검증
- 커밋된 트리를 worktree에서: dev 빌드 + 테스트 103개(08까지 96 + 7), tidy 경고 0, ASan 103개, clang-format, .NET Debug 빌드 통과.
- end-to-end(이 저장소용 별도 DB `sa_replay`, 새로 마이그레이션):

```
bots=5 provisioned=5 connected=5 welcomed=5 saw_another_player=4 (required 2) failed=0
snapshots=500 inputs_sent=980 max_visible=1 integrity_errors=0
ping_rtt_ms p50=0 max=38
RESULT: PASS
live sessions after run: 0
```

## 남긴 것
- CI는 아직 end-to-end를 돌리지 않는다(서비스 컨테이너와 프로세스 기동이 필요). 6단계에서 이미지가 생기면 붙인다.
- 이 PR이 병합되면 첫 릴리스 0.1.0(0~3단계)을 낸다.

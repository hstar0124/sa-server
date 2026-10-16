# 007 지워진 설계 문서의 절 번호가 주석 다섯 곳에 숫자로만 남음

- 상태: 해결 (14의 `refactor: drop bare design-document section numbers left in comments` 커밋)
- 발견: [14 전투 시뮬레이션](../features/14-zone-combat-sim.md), 2026-10-16
- 영향: 런타임 영향은 없다. 읽는 사람이 `(14.3)` 같은 주석을 따라갈 문서가 없다. 설계 문서는 09에서 README로 접어 넣고 지웠다

## 증상
14의 전투 코드를 옮기면서 주석의 설계 문서 참조를 README 제목으로 바꾸는 치환을 돌렸다. 치환 뒤에도 새 코드에 `(8.1 step 4)`, `(8.1: lifecycle guard)`, `(7.2)`, `// 7.2 ValidateAndApplyCommands`가 남아 있었다. 같은 형태를 develop 전체에서 찾자 이미 병합된 파일 다섯 곳에서도 나왔다.

```
server/cpp/common/include/sa/common/ids.hpp:18  ... for a character (section 14.3 fencing).
contracts/backend/world_directory.proto:17   ... ends the zone's authority (14.3).
deploy/compose/config/gateway.toml:15        ... a flood is kicked (6.3).
server/cpp/zone/server/src/main.cpp:193      ... before the io_context is stopped (16.3).
server/cpp/zone/server/src/zone_host.hpp:37  ... holds its Backend lease (14.3).
```

## 원인 분석
- 새 코드에 남은 세 곳은 이번에 충돌이 난 파일이 아니었다. 충돌 해결이 아니라 치환 방식을 의심하고 develop 전체를 같은 형태로 검색했다.
- 치환 규칙을 다시 읽었다. 모든 규칙이 `design document`, `설계 문서`, `Server_Architecture.md` 같은 표지어를 앞에 요구한다. 표지어 없이 괄호 속 숫자만 있는 참조는 대상 파일 목록(`git grep`)에도 들지 않았다.
- 표지어 없는 숫자를 그대로 치환 대상으로 넣을 수는 없었다. `TimeSpan.FromSeconds(1.2)`처럼 코드의 숫자와 구별되지 않는다.

## 원인
치환 대상을 표지어로만 골랐다. 이전 이력의 주석 일부는 같은 문단에서 이미 설계 문서를 언급했다는 이유로 표지어 없이 절 번호만 적었다. 09에서 치환 결과를 검토할 때도 "치환된 줄"만 보고 "치환되지 않은 참조"는 찾지 않았다.

## 해결
- 다섯 곳은 숫자를 지웠다. proto 파일은 맨 위 주석이 이미 README "소유권과 lease"를 가리키고, 나머지는 주석 문장만으로 뜻이 선다. 참조를 덧붙이면 100자 줄 제한도 넘는다. `ids.hpp`는 "fencing"의 뜻을 문장으로 풀었다.
- 14의 새 코드는 README "전투" 절을 가리키게 하거나 숫자를 지웠다.

## 재발 방지
- 치환 스크립트에 검토 단계를 더했다. 주석 안에서 `(N.N)`, `(N.N step K)`, `(N.N: 메모)`, `(N.N steps K-L)`, `section N.N`, 줄 머리의 `N.N 단계명`을 찾아 `REVIEW 파일:줄`로 출력한다. 주석 표지(`//`, `#`) 뒤만 보므로 코드의 숫자는 걸리지 않는다. 정리한 브랜치에서는 아무것도 내지 않는다.
- 검토 패턴 자체도 한 번에 맞히지 못했다. 처음 패턴은 `(8.1: lifecycle guard)`와 `(section 14.3 fencing)`을 놓쳤고, 이전 저장소의 주석 전체를 훑어 형태를 모은 뒤 넓혔다. 앞으로 옮길 이력에서 나올 `(14.2 steps 5-8)`도 이때 찾았다.
- 이전 이력을 옮기는 피처마다 치환 뒤 이 출력이 비어 있는지 확인하고 커밋한다.

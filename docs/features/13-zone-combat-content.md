# 13 (4단계) 전투 콘텐츠: 클래스·스킬·몬스터를 데이터로

## 배경과 목표
4단계는 전투다. 플레이어가 스킬로 몬스터와 싸우고, 몬스터는 추적하고 공격하고 돌아간다. 이 PR은 그 첫 조각으로 시뮬레이션이 읽을 **콘텐츠 정의**만 들인다. 스킬·몬스터 수치를 코드에 적지 않는다는 규칙을 지키려면 시전·AI 코드보다 데이터 모델과 검증이 먼저 있어야 한다. 완료 조건: 저장소에 실린 game-data가 로더 검증을 통과하고, 잘못된 참조는 로드 단계에서 거절된다. Zone 서버가 이 데이터를 실제로 쓰는 것은 다음 PR들(시전·AI, 호스트 연결)이다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 스킬 표현 | 스킬마다 코드(상속·스크립트) / 데이터 + 판정 종류 몇 개 | 데이터. 판정은 `targeted`(대상 엔티티)와 `projectile`(방향) 두 종류로 시작하고 수치는 TOML. 새 판정이 필요할 때만 코드에 종류를 더한다 |
| 파일 구성 | 한 파일 / 종류별 파일 | 종류별(classes, skills, monsters)과 `version.toml`. 맵마다 다른 몬스터 스폰은 맵 파일에 |
| 시간 단위 | 틱 수 / 밀리초 | 밀리초(`std::chrono::milliseconds`). 틱으로 바꾸는 것은 틱 속도를 아는 Zone이 한다. 틱 속도를 바꿔도 데이터는 그대로다 |
| 검증 시점 | 쓰는 순간 / 로드할 때 묶음으로 | 로드할 때. 묶음 검증(`ContentBundle::Validate`)과 맵 교차 검증(`MapData::ValidateAgainst`). 틱 안에서 "없는 스킬"을 만날 일이 없게 한다 |
| 콘텐츠 버전 | 없음 / 파일 해시 / 사람이 올리는 식별자 | 사람이 올리는 식별자(`2026.10.12-1`). 로그와 기록에 남기기 쉽고, 주석·형식 변경처럼 결과가 안 바뀌는 수정은 버전을 올리지 않아도 된다 |
| 전투 공통 규칙 | 새 파일 / `movement.toml`의 `[combat]` 표 | `[combat]` 표. 몬스터 판단 주기와 플레이어 부활 시간 두 값뿐이라 파일을 나누지 않았다 |
| ID | 원시 정수 / 강타입 | `SkillId`, `ClassId`, `MonsterTemplateId`를 `StrongId`로 |

## 구현
- game-data: `classes/classes.toml`(전사·궁수·마법사), `skills/skills.toml`(strike, heavy_blow, arrow, fireball), `monsters/monsters.toml`(slime, wolf), `maps/zone-1.toml`의 몬스터 스폰 네 곳, `version.toml`, `rules/movement.toml`의 `[combat]`
- `zone/sim`: `content.hpp/.cpp`(정의 타입과 묶음 검증), `map_data`의 `MonsterSpawn`과 `ValidateAgainst`
- `zone/data`: `LoadContent`·`ParseContent`, `LoadCombatRules`·`ParseCombatRules`
- 테스트 3개: 실린 묶음이 유효하고 맵과 맞는다, 없는 스킬 참조와 투사체 필드 누락을 거절한다, 모르는 몬스터 템플릿 스폰을 거절한다
- README: "전투" 절(콘텐츠 데이터와 검증), 저장소 구성
- 이전 이력의 설계 문서 절 참조는 README 제목으로 바꿨다. 자동 치환이 `movement.toml` 주석의 "몬스터마다 판단 시점을 엇갈린다"까지 지워서 손으로 되살렸다

## 만난 문제
- 없음.

## 검증
- C++: dev 114개 통과(12보다 3개 늘었다), clang-tidy 경고 0, ASan·UBSan 114개 통과, 형식 검사 통과.
- end-to-end: 봇 5개 전원 입장, 다른 플레이어를 본 봇 4개, 정합성 오류 0, 실행 뒤 남은 세션 0. Zone 서버가 맵 파일을 읽는 경로는 이번에 몬스터 스폰을 더 파싱하므로 실제 프로세스로도 확인했다.

## 남긴 것
- 시전 검증·투사체·몬스터 AI(시뮬레이션)와 Zone 호스트 연결은 다음 PR에서 한다.
- 콘텐츠 버전은 아직 아무도 기록하지 않는다. 처치 보상을 Backend가 기록할 때(5단계) 근거로 쓴다.

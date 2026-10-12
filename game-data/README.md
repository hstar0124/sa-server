# game-data

맵·규칙·클래스·스킬·몬스터 정의. 코드에 수치를 적지 않고 여기서 읽는다(CLAUDE.md 규칙 1).

| 경로 | 내용 | 읽는 쪽 |
|---|---|---|
| `version.toml` | 콘텐츠 버전. game-data가 바뀌면 올린다 | Zone(로그), Backend(5단계 보상 근거) |
| `maps/zone-N.toml` | 맵 크기, 격자 셀, 장애물, 플레이어 스폰, 몬스터 스폰 | zone_server → `sa::zone::data::LoadMap` |
| `rules/movement.toml` | 이동 속도, AOI 반경, 입력 예산, 전투 공통 규칙 | zone_server → `LoadMovementRules`, `LoadCombatRules` |
| `classes/classes.toml` | 클래스별 체력·마나·보유 스킬 | zone_server → `LoadContent` |
| `skills/skills.toml` | 스킬 종류·비용·쿨타임·시전 시간·사거리·피해 | zone_server → `LoadContent` |
| `monsters/monsters.toml` | 몬스터 체력·속도·어그로·리시·공격 | zone_server → `LoadContent` |

Zone과 Backend는 같은 콘텐츠 버전을 참조한다.

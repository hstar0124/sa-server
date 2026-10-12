# game-data

맵·규칙·(이후) 스킬·아이템·몬스터 정의. 코드에 수치를 적지 않고 여기서 읽는다(CLAUDE.md 규칙 1).

| 경로 | 내용 | 읽는 쪽 |
|---|---|---|
| `maps/zone-N.toml` | 맵 크기, 격자 셀, 장애물, 스폰 지점 | zone_server → `sa::zone::data::LoadMap` |
| `rules/movement.toml` | 이동 속도, AOI 반경(진입/이탈), 입력 예산 | zone_server → `sa::zone::data::LoadMovementRules` |

Zone과 Backend는 같은 콘텐츠 버전을 참조한다(설계 문서 8.1절). 버전 식별자는 4단계(스킬 데이터)에서 도입한다.

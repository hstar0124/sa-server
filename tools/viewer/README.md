# tools/viewer

브라우저용 2D Zone 뷰어이자 최소 클라이언트. Gateway의 WebSocket 포트에 붙어 네이티브 클라이언트와 **같은 Protobuf 계약**을 쓴다. `.proto`는 저장소에서 직접 읽으므로 메시지 정의를 복제하지 않는다.

```bash
scripts/dev-stack.sh up        # PostgreSQL(compose) + Backend + Zone + Gateway
scripts/viewer.sh              # http://localhost:8080/tools/viewer/ 를 띄운다
scripts/dev-stack.sh down
```

보여주는 것 (설계 노트 02):
- **보간 버퍼 슬라이더**: 다른 플레이어를 몇 ms 과거로 그릴지. 0이면 스냅샷마다 점프가 보인다.
- **인위적 유실 슬라이더**: 수신한 스냅샷의 위치 표본을 그 확률로 버린다. 버퍼가 유실을 어디까지 숨기는지 눈으로 확인한다. 스냅샷은 델타라서 통째로 버리면 서버와 상관없는 유령 엔티티가 생기므로, 엔티티의 생성·삭제와 체력은 항상 반영한다.
- **외삽 토글**: 데이터가 늦을 때 마지막 속도로 밀어 그릴지, 멈춰 둘지.
- 색: 노랑 = 나(서버 권위 위치), 초록 = 다른 플레이어(보간 중), 빨강 = 몬스터, 보라 사각형 = 바닥 아이템, 주황 = 외삽 중, 회색 = 정지(데이터 없음). 막대는 체력.
- 전투: Space는 사거리 안의 가장 가까운 몬스터에게 기본 공격, E는 강타. 결과와 피해는 HUD 로그에 찍힌다.
- 획득: 몬스터가 죽으면 보라 사각형(바닥 아이템)이 생긴다. 가까이 가서 F를 누르면 획득 요청이 Backend까지 가고, 결과(대기 → 확정)와 인벤토리·골드가 HUD에 반영된다. 획득 거리는 `game-data/rules/economy.toml`의 `[pickup] range_m`을 읽는다.

Backend는 뷰어 origin을 CORS로 허용해야 한다. `scripts/dev-stack.sh`가 `Cors__AllowedOrigins__0=http://localhost:8080`을 넣는다.

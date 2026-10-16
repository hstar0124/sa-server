# database

`migrations/NNNN_description.sql` — 순서대로 한 번만 적용되는 전진 마이그레이션. 롤백 스크립트는 두지 않는다 (ADR-0007).

규칙:
- 모든 캐릭터 관련 테이블과 인덱스에 `world_id`.
- 재화는 정수 최소 단위. `CHECK (balance >= 0)`.
- 멱등성 키와 지급 근거는 `UNIQUE`. 사전 SELECT로 중복을 판정하지 않는다.
- 조건부 UPDATE(`WHERE ... AND version = :v`)로 경합을 해결한다.

적용: `dotnet run --project server/dotnet/Migrator -- --connection "<conn>" --scripts database/migrations`

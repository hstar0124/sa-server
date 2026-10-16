-- Forward-only migration applied by server/dotnet/Migrator (DbUp). See ADR-0007.
-- Game tables land in stage 1; this establishes the schema and the uuid helper used for
-- idempotency keys and event ids.
CREATE SCHEMA IF NOT EXISTS game;

COMMENT ON SCHEMA game IS 'Authoritative persistent state: accounts, characters, assets, ledger, outbox.';

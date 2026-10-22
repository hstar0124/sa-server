-- Seeds :count bench characters in world 1 with random kill counts, so the ranking queries run
-- against a realistically sized statistics table. Idempotent per name prefix: a rerun adds only
-- what is missing. Removed again by scripts/bench/ranking.sh clean.
--   psql -v count=100000 -f scripts/bench/seed-rankings.sql
BEGIN;
INSERT INTO game.accounts (username, password_hash)
SELECT 'bench_' || lpad(i::text, 7, '0'), 'bench'
FROM generate_series(1, :count) AS i
ON CONFLICT (username) DO NOTHING;

INSERT INTO game.characters (world_id, account_id, name, class_id)
SELECT 1, a.account_id, 'Bench' || substring(a.username from 7), 1
FROM game.accounts a
WHERE a.username LIKE 'bench\_%' AND NOT EXISTS (
    SELECT 1 FROM game.characters c WHERE c.account_id = a.account_id AND c.world_id = 1);

INSERT INTO game.character_projections (world_id, character_id, kills, items_acquired, gold_earned, last_event_seq)
SELECT 1, c.character_id, floor(random() * 5000)::bigint, 0, 0, 0
FROM game.characters c
JOIN game.accounts a ON a.account_id = c.account_id
WHERE a.username LIKE 'bench\_%'
ON CONFLICT (world_id, character_id) DO NOTHING;
COMMIT;
ANALYZE game.character_projections;

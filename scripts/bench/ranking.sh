#!/usr/bin/env bash
# Measures the kill-ranking endpoints against the local stack: the top-20 page, and the rank of the
# worst-placed bench character, the most expensive "where am I" there is. One line per case;
# docs/reports/ keeps the results.
#
#   scripts/bench/ranking.sh run <characters> [clients] [duration_sec]
#   scripts/bench/ranking.sh clean      remove the bench accounts, characters and statistics
#
# Requires the local stack (scripts/dev-stack.sh up) and a dotnet build of tools/httpbench.
set -euo pipefail
cd "$(dirname "$0")/../.."
BACKEND="${SA_BACKEND_URL:-http://localhost:5100}"
BENCH=tools/httpbench/bin/Debug/net10.0/httpbench
WORLD=1  # seed-rankings.sql seeds world 1
set -a; . deploy/compose/.env; set +a
sql() { docker exec -i sa-postgres psql -v ON_ERROR_STOP=1 -tA -U "$POSTGRES_USER" -d "$POSTGRES_DB" "$@"; }

run() {
  local count="${1:?number of bench characters}" clients="${2:-16}" duration="${3:-10}"
  sql -v count="$count" -f - < scripts/bench/seed-rankings.sql >/dev/null
  local total worst user token
  total=$(sql -c "SELECT count(*) FROM game.character_projections WHERE world_id = $WORLD")
  worst=$(sql -c "SELECT character_id FROM game.character_projections WHERE world_id = $WORLD ORDER BY kills, character_id DESC LIMIT 1")

  user="benchuser$(date +%s | tail -c 6)"
  curl -sf -X POST "$BACKEND/api/v1/auth/register" -H 'content-type: application/json' \
    -d "{\"username\":\"$user\",\"password\":\"bench-password-2026\"}" >/dev/null
  token=$(curl -sf -X POST "$BACKEND/api/v1/auth/login" -H 'content-type: application/json' \
    -d "{\"username\":\"$user\",\"password\":\"bench-password-2026\"}" | python3 -c 'import json,sys;print(json.load(sys.stdin)["accessToken"])')

  echo "statistics rows in world $WORLD: $total; worst-ranked character: $worst; clients=$clients duration=${duration}s"
  "$BENCH" --url "$BACKEND/api/v1/worlds/$WORLD/rankings/kills?top=20" --bearer "$token" \
    --clients "$clients" --duration-sec "$duration" --label "top-20 (n=$total)"
  "$BENCH" --url "$BACKEND/api/v1/worlds/$WORLD/characters/$worst/rank" --bearer "$token" \
    --clients "$clients" --duration-sec "$duration" --label "rank of worst (n=$total)"
}

clean() {
  sql <<'SQL'
BEGIN;
DELETE FROM game.character_projections p USING game.characters c, game.accounts a
 WHERE p.world_id = c.world_id AND p.character_id = c.character_id AND c.account_id = a.account_id AND a.username LIKE 'bench\_%';
DELETE FROM game.characters c USING game.accounts a WHERE c.account_id = a.account_id AND a.username LIKE 'bench\_%';
DELETE FROM game.accounts WHERE username LIKE 'bench\_%' OR username LIKE 'benchuser%';
COMMIT;
ANALYZE game.character_projections;
SQL
  echo "bench data removed"
}

case "${1:-}" in
  run) shift; run "$@" ;;
  clean) clean ;;
  *) echo "usage: $0 run <characters> [clients] [duration_sec] | clean"; exit 2 ;;
esac

#!/usr/bin/env bash
# Runs the whole stack locally (PostgreSQL in compose, everything else as host processes) and
# drives it with the loadgen smoke profile. Exit code is the loadgen verdict.
#
#   scripts/e2e-local.sh [profile.toml]
#
# Requires: a dev build (cmake --preset dev / dotnet build), docker compose, deploy/compose/.env.
set -euo pipefail
cd "$(dirname "$0")/.."

PROFILE="${1:-tools/loadgen/client/profiles/smoke.toml}"
BUILD=build/dev
RUN_DIR="${SA_E2E_RUN_DIR:-/tmp/sa-e2e}"
mkdir -p "$RUN_DIR"

set -a; . deploy/compose/.env; set +a
CONN="Host=localhost;Port=5432;Database=$POSTGRES_DB;Username=$POSTGRES_USER;Password=$POSTGRES_PASSWORD"
TICKET_KEY="${SA_TICKET_KEY:-$(openssl rand -base64 32)}"
JWT_KEY="${SA_JWT_KEY:-$(openssl rand -base64 32)}"

pids=()
# Orderly shutdown: gateway first (it ends sessions at the Backend), then zone, then backend.
cleanup() {
  for pid in "${pids[@]:-}"; do kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT

docker compose -f deploy/compose/docker-compose.yml up -d --wait >/dev/null
dotnet run --project server/dotnet/Migrator --no-build -- --connection "$CONN" --scripts database/migrations >"$RUN_DIR/migrator.log" 2>&1

(
  cd server/dotnet/GameBackend
  Database__ConnectionString="$CONN" Auth__SigningKey="$JWT_KEY" WorldDirectory__TicketSigningKey="$TICKET_KEY" \
    Auth__CredentialAttemptsPerMinutePerAddress=6000 \
    exec bin/Debug/net10.0/GameBackend
) >"$RUN_DIR/backend.log" 2>&1 &
backend_pid=$!
for _ in $(seq 1 60); do curl -sf localhost:5100/health/ready >/dev/null 2>&1 && break; sleep 0.5; done
curl -sf localhost:5100/health/ready >/dev/null || { echo "backend not ready"; cat "$RUN_DIR/backend.log"; exit 1; }

"$BUILD/server/cpp/zone/server/zone_server" --config deploy/compose/config/zone-a.toml >"$RUN_DIR/zone-a.log" 2>&1 &
zone_pid=$!
SA_GATEWAY__TICKET__SIGNING_KEY_BASE64="$TICKET_KEY" \
  "$BUILD/server/cpp/gateway/gateway" --config deploy/compose/config/gateway.toml >"$RUN_DIR/gateway.log" 2>&1 &
gateway_pid=$!
pids=("$gateway_pid" "$zone_pid" "$backend_pid")
sleep 1.5  # leases acquired, zone link ready

set +e
"$BUILD/tools/loadgen/client/loadgen" --profile "$PROFILE"
verdict=$?
set -e

# Bots have closed their sockets; give the gateway a moment to detach and end their sessions,
# then check the directory agrees that nothing is left alive.
sleep 1
live=$(docker exec sa-postgres psql -tA -U "$POSTGRES_USER" -d "$POSTGRES_DB" \
  -c "SELECT count(*) FROM game.character_sessions WHERE ended_at IS NULL")
echo "live sessions after run: $live"
if [ "$live" != "0" ]; then echo "FAIL: sessions left open in the directory"; verdict=1; fi
echo "logs: $RUN_DIR"
exit $verdict

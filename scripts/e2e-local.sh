#!/usr/bin/env bash
# Runs the whole stack locally (PostgreSQL and Kafka in compose, everything else as host processes) and
# drives it with the loadgen smoke profile. Exit code is the verdict of the bots and of the checks
# that follow them in the database.
#
#   scripts/e2e-local.sh [profile.toml]
#
# Requires: a dev build (cmake --preset dev / dotnet build), docker compose, deploy/compose/.env.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

PROFILE="${1:-tools/loadgen/client/profiles/smoke.toml}"
BUILD=build/dev
RUN_DIR="${SA_E2E_RUN_DIR:-/tmp/sa-e2e}"
mkdir -p "$RUN_DIR"

set -a; . deploy/compose/.env; set +a
CONN="Host=localhost;Port=5432;Database=$POSTGRES_DB;Username=$POSTGRES_USER;Password=$POSTGRES_PASSWORD"
TICKET_KEY="${SA_TICKET_KEY:-$(openssl rand -base64 32)}"
JWT_KEY="${SA_JWT_KEY:-$(openssl rand -base64 32)}"

# Fixed so a run is reproducible end to end (ADR-0006): the bots already use the profile's seed.
ZONE_SEED="${SA_E2E_ZONE_SEED:-1}"
STACK_PORTS=(5100 5101 7777 7778 7801)

psql_value() { docker exec sa-postgres psql -tA -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c "$1"; }
. scripts/lib/e2e.sh

port_open() { ss -ltn "sport = :$1" | grep -q LISTEN; }

# Another stack (scripts/dev-stack.sh, an earlier run) on these ports would answer instead of
# ours and silently be what the bots test.
for port in "${STACK_PORTS[@]}"; do
  if port_open "$port"; then echo "port $port is in use; stop the other stack first (scripts/dev-stack.sh down)"; exit 1; fi
done

pids=()
# Orderly shutdown in reverse start order: gateway first (it ends sessions at the Backend), then
# zone, then backend, then the EventWorker. Each pid is recorded as soon as it starts, so a failed
# start still cleans up.
cleanup() {
  for pid in "${pids[@]:-}"; do kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT

docker compose -f deploy/compose/docker-compose.yml up -d --wait >/dev/null
dotnet run --project server/dotnet/Migrator --no-build -- --connection "$CONN" --scripts database/migrations >"$RUN_DIR/migrator.log" 2>&1

# Started first and stopped last, so it publishes and applies what the run leaves behind.
(
  cd server/dotnet/EventWorker
  Database__ConnectionString="$CONN" exec bin/Debug/net10.0/EventWorker
) >"$RUN_DIR/eventworker.log" 2>&1 &
pids=("$!" "${pids[@]}")
wait_for "the event publisher" grep -q "outbox publisher active" "$RUN_DIR/eventworker.log" \
  || { cat "$RUN_DIR/eventworker.log"; exit 1; }

(
  cd server/dotnet/GameBackend
  Database__ConnectionString="$CONN" Auth__SigningKey="$JWT_KEY" WorldDirectory__TicketSigningKey="$TICKET_KEY"  \
    GameData__ContentDir="$ROOT/game-data" \
    Auth__CredentialAttemptsPerMinutePerAddress=6000 \
    exec bin/Debug/net10.0/GameBackend
) >"$RUN_DIR/backend.log" 2>&1 &
pids=("$!" "${pids[@]}")
wait_for "the backend" curl -sf localhost:5100/health/ready >/dev/null 2>&1 \
  || { cat "$RUN_DIR/backend.log"; exit 1; }

SA_ZONE__ZONE__RNG_SEED="$ZONE_SEED" \
  "$BUILD/server/cpp/zone/server/zone_server" --config deploy/compose/config/zone-a.toml >"$RUN_DIR/zone-a.log" 2>&1 &
pids=("$!" "${pids[@]}")
wait_for "the zone lease" grep -q "zone lease acquired" "$RUN_DIR/zone-a.log" || { cat "$RUN_DIR/zone-a.log"; exit 1; }

SA_GATEWAY__TICKET__SIGNING_KEY_BASE64="$TICKET_KEY" \
  "$BUILD/server/cpp/gateway/gateway" --config deploy/compose/config/gateway.toml >"$RUN_DIR/gateway.log" 2>&1 &
pids=("$!" "${pids[@]}")
wait_for "the gateway's zone link" grep -q "link ready" "$RUN_DIR/gateway.log" || { cat "$RUN_DIR/gateway.log"; exit 1; }

run_started="$(psql_value "SELECT now()")"
set +e
"$BUILD/tools/loadgen/client/loadgen" --profile "$PROFILE"
verdict=$?
set -e

check_run "$run_started" || { verdict=1; echo "(see $RUN_DIR for process logs)"; }
echo "logs: $RUN_DIR"
exit $verdict

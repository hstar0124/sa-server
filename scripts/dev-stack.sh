#!/usr/bin/env bash
# Starts or stops the local stack for interactive use (viewer, manual testing):
# PostgreSQL in compose, then Backend, Zone A and Gateway as host processes.
#
#   scripts/dev-stack.sh up | down | status
#
# State lives in $SA_DEV_DIR (default /tmp/sa-dev): pids, logs and the generated keys, so the
# same keys are reused across restarts within one session.
set -euo pipefail
cd "$(dirname "$0")/.."

DEV_DIR="${SA_DEV_DIR:-/tmp/sa-dev}"
BUILD=build/dev
VIEWER_ORIGIN="${SA_VIEWER_ORIGIN:-http://localhost:8080}"
mkdir -p "$DEV_DIR"

load_env() {
  set -a; . deploy/compose/.env; set +a
  CONN="Host=localhost;Port=5432;Database=$POSTGRES_DB;Username=$POSTGRES_USER;Password=$POSTGRES_PASSWORD"
  [ -f "$DEV_DIR/ticket.key" ] || openssl rand -base64 32 > "$DEV_DIR/ticket.key"
  [ -f "$DEV_DIR/jwt.key" ] || openssl rand -base64 32 > "$DEV_DIR/jwt.key"
  TICKET_KEY=$(cat "$DEV_DIR/ticket.key"); JWT_KEY=$(cat "$DEV_DIR/jwt.key")
}

up() {
  load_env
  docker compose -f deploy/compose/docker-compose.yml up -d --wait >/dev/null
  dotnet run --project server/dotnet/Migrator --no-build -- --connection "$CONN" --scripts database/migrations >"$DEV_DIR/migrator.log" 2>&1
  (
    cd server/dotnet/GameBackend
    Database__ConnectionString="$CONN" Auth__SigningKey="$JWT_KEY" WorldDirectory__TicketSigningKey="$TICKET_KEY"  \
    Economy__ContentDir="$PWD/game-data" \
    Auth__CredentialAttemptsPerMinutePerAddress=6000 \
      Cors__AllowedOrigins__0="$VIEWER_ORIGIN" exec bin/Debug/net10.0/GameBackend
  ) >"$DEV_DIR/backend.log" 2>&1 &
  echo $! > "$DEV_DIR/backend.pid"
  for _ in $(seq 1 60); do curl -sf localhost:5100/health/ready >/dev/null 2>&1 && break; sleep 0.5; done
  curl -sf localhost:5100/health/ready >/dev/null || { echo "backend not ready"; tail -20 "$DEV_DIR/backend.log"; exit 1; }
  "$BUILD/server/cpp/zone/server/zone_server" --config deploy/compose/config/zone-a.toml >"$DEV_DIR/zone-a.log" 2>&1 &
  echo $! > "$DEV_DIR/zone-a.pid"
  SA_GATEWAY__TICKET__SIGNING_KEY_BASE64="$TICKET_KEY" \
    "$BUILD/server/cpp/gateway/gateway" --config deploy/compose/config/gateway.toml >"$DEV_DIR/gateway.log" 2>&1 &
  echo $! > "$DEV_DIR/gateway.pid"
  sleep 1.5
  echo "stack up. logs in $DEV_DIR. backend :5100/:5101, gateway tcp :7777 ws :7778, zone link :7801"
}

down() {
  # Gateway first so it ends sessions at the Backend, then zone, then backend.
  for name in gateway zone-a backend; do
    if [ -f "$DEV_DIR/$name.pid" ]; then
      pid=$(cat "$DEV_DIR/$name.pid")
      kill -TERM "$pid" 2>/dev/null || true
      for _ in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
      rm -f "$DEV_DIR/$name.pid"
    fi
  done
  echo "stack down (PostgreSQL left running in compose)"
}

status() {
  for name in backend zone-a gateway; do
    if [ -f "$DEV_DIR/$name.pid" ] && kill -0 "$(cat "$DEV_DIR/$name.pid")" 2>/dev/null; then echo "$name: running"; else echo "$name: stopped"; fi
  done
}

case "${1:-}" in
  up) up ;;
  down) down ;;
  status) status ;;
  *) echo "usage: $0 up|down|status"; exit 2 ;;
esac

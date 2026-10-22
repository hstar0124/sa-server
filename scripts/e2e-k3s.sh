#!/usr/bin/env bash
# Drives the stack deployed on K3s (scripts/k3s-stack.sh up) with the loadgen and checks the
# database afterwards, as scripts/e2e-local.sh does for the local stack. Exit code is the verdict.
#
#   scripts/e2e-k3s.sh [profile.toml]
#
# The bots run on the host against the Backend's NodePort; the Backend hands them the gateway's.
set -euo pipefail
cd "$(dirname "$0")/.."
export KUBECONFIG="${KUBECONFIG:-/etc/rancher/k3s/k3s.yaml}"

PROFILE="${1:-tools/loadgen/client/profiles/smoke.toml}"
BUILD=build/dev
BACKEND_NODE_PORT=30500  # deploy/k3s/game/backend.yaml, Service backend-public (as in k3s-stack.sh)

psql_value() {
  kubectl -n data exec statefulset/postgres -- sh -c 'psql -tA -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c "$1"' psql "$1"
}
redis_cli() { kubectl -n data exec statefulset/redis -- redis-cli "$@"; }
. scripts/lib/e2e.sh

ip=$(kubectl get nodes -o jsonpath='{.items[0].status.addresses[?(@.type=="InternalIP")].address}')
run_started="$(psql_value "SELECT now()")"
set +e
SA_LOADGEN__BACKEND__BASE_URL="http://$ip:$BACKEND_NODE_PORT" "$BUILD/tools/loadgen/client/loadgen" --profile "$PROFILE"
verdict=$?
set -e

check_run "$run_started" || { verdict=1; echo "(process logs: kubectl -n game logs deployment/<name>)"; }
exit $verdict

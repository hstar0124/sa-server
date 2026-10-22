#!/usr/bin/env bash
# Deploys the stack to the local single-node K3s, or takes it down again. compose stays the
# development loop (ADR-0002); K3s is for demonstrations and failure drills.
#
#   scripts/k3s-stack.sh import [tag]   copy sa/*:<tag> images (scripts/build-images.sh) into K3s
#   scripts/k3s-stack.sh up | down | status
#
# Requires a running K3s (deploy/k3s/README.md) and deploy/compose/.env for the PostgreSQL
# credentials. Signing keys are generated once and kept in $SA_K3S_DIR (default /tmp/sa-k3s), so a
# redeploy keeps issued tickets valid. A K3s installed with --data-dir needs K3S_DATA_DIR set to the
# same directory, or `k3s ctr` unpacks itself again under the default /var/lib/rancher/k3s.
set -euo pipefail
cd "$(dirname "$0")/.."
export KUBECONFIG="${KUBECONFIG:-/etc/rancher/k3s/k3s.yaml}"
K3S_DIR="${SA_K3S_DIR:-/tmp/sa-k3s}"
IMAGES=(zone gateway loadgen backend eventworker migrator)
ROLLOUT_TIMEOUT=180s
BACKEND_NODE_PORT=30500  # deploy/k3s/game/backend.yaml, Service backend-public
mkdir -p "$K3S_DIR"

kustomize() { kubectl kustomize --load-restrictor LoadRestrictionsNone "$1"; }

import() {
  local tag="${1:-dev}"
  for name in "${IMAGES[@]}"; do
    docker save "sa/$name:$tag" | k3s ctr images import - >/dev/null
    echo "imported sa/$name:$tag"
  done
}

secrets() {
  set -a; . deploy/compose/.env; set +a
  [ -f "$K3S_DIR/ticket.key" ] || openssl rand -base64 32 > "$K3S_DIR/ticket.key"
  [ -f "$K3S_DIR/jwt.key" ] || openssl rand -base64 32 > "$K3S_DIR/jwt.key"
  local conn="Host=postgres.data.svc;Port=5432;Database=$POSTGRES_DB;Username=$POSTGRES_USER;Password=$POSTGRES_PASSWORD"
  kubectl -n data create secret generic sa-postgres --from-literal=POSTGRES_DB="$POSTGRES_DB" \
    --from-literal=POSTGRES_USER="$POSTGRES_USER" --from-literal=POSTGRES_PASSWORD="$POSTGRES_PASSWORD" \
    --dry-run=client -o yaml | kubectl apply -f -
  kubectl -n game create secret generic sa-database --from-literal=connection-string="$conn" \
    --dry-run=client -o yaml | kubectl apply -f -
  kubectl -n game create secret generic sa-keys \
    --from-literal=jwt-signing-key="$(cat "$K3S_DIR/jwt.key")" \
    --from-literal=ticket-signing-key="$(cat "$K3S_DIR/ticket.key")" \
    --dry-run=client -o yaml | kubectl apply -f -
}

# A Ready pod has its port open; the game processes can serve only once they hold a Backend lease
# and the gateway has its zone link. scripts/e2e-local.sh waits for the same log lines.
wait_log() {  # deployment, pattern
  for _ in $(seq 1 120); do
    kubectl -n game logs "deployment/$1" 2>/dev/null | grep -q "$2" && return 0
    sleep 0.5
  done
  echo "timed out waiting for '$2' from $1"; kubectl -n game logs "deployment/$1" --tail=20; return 1
}

node_ip() { kubectl get nodes -o jsonpath='{.items[0].status.addresses[?(@.type=="InternalIP")].address}'; }

up() {
  kubectl apply -f deploy/k3s/namespaces.yaml
  secrets
  kustomize deploy/k3s/data | kubectl apply -f -
  kubectl -n data rollout status statefulset/postgres --timeout="$ROLLOUT_TIMEOUT"
  kubectl -n data rollout status statefulset/kafka --timeout="$ROLLOUT_TIMEOUT"
  kubectl -n data rollout status statefulset/redis --timeout="$ROLLOUT_TIMEOUT"

  kubectl -n game delete job migrator --ignore-not-found
  kustomize deploy/k3s/migrate | kubectl apply -f -
  kubectl -n game wait --for=condition=complete job/migrator --timeout="$ROLLOUT_TIMEOUT"

  # Clients learn the gateway address from the Backend: this node and the gateway NodePorts.
  local ip; ip=$(node_ip)
  kubectl -n game create configmap backend-env \
    --from-literal=WorldDirectory__GatewayHost="$ip" \
    --from-literal=WorldDirectory__GatewayPort=30777 \
    --from-literal=WorldDirectory__GatewayWebSocketPort=30778 \
    --from-literal=Auth__CredentialAttemptsPerMinutePerAddress=6000 \
    --dry-run=client -o yaml | kubectl apply -f -
  kustomize deploy/k3s/game | kubectl apply -f -
  for d in eventworker backend zone-a gateway; do
    kubectl -n game rollout status deployment/$d --timeout="$ROLLOUT_TIMEOUT"
  done
  wait_log zone-a "zone lease acquired"
  wait_log gateway "gateway lease acquired"
  wait_log gateway "link ready"
  # A rolled-out pod is not yet a reachable NodePort; clients start right after this script.
  for _ in $(seq 1 60); do curl -sf "http://$ip:$BACKEND_NODE_PORT/health/ready" >/dev/null && break; sleep 0.5; done
  curl -sf "http://$ip:$BACKEND_NODE_PORT/health/ready" >/dev/null || { echo "backend not reachable on $ip:$BACKEND_NODE_PORT"; exit 1; }
  echo "stack up on $ip: backend http :30500, gateway tcp :30777 ws :30778"
}

down() {
  kustomize deploy/k3s/game | kubectl delete --ignore-not-found -f -
  kubectl -n game delete job migrator --ignore-not-found
  kustomize deploy/k3s/data | kubectl delete --ignore-not-found -f -
  # Deleting a Deployment leaves its pods terminating; wait, so an `up` right after starts clean.
  kubectl -n game wait --for=delete pod --all --timeout="$ROLLOUT_TIMEOUT"
  kubectl -n data wait --for=delete pod --all --timeout="$ROLLOUT_TIMEOUT"
  echo "stack down (volumes kept: delete the PersistentVolumeClaims in namespace data to start clean)"
}

status() {
  kubectl -n data get statefulsets,pods
  kubectl -n game get deployments,pods,services
}

case "${1:-}" in
  import) import "${2:-dev}" ;;
  up) up ;;
  down) down ;;
  status) status ;;
  *) echo "usage: $0 import [tag] | up | down | status"; exit 2 ;;
esac

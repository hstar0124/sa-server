#!/usr/bin/env bash
# Capacity baseline: runs the whole local stack (release builds) with N mixed bots and prints, per
# process, the average CPU and peak memory over the steady window, the zones' tick health and the
# bots' verdict, as markdown for docs/reports/.
#
#   scripts/bench/capacity.sh <bots> [duration_sec]        e.g. 50, 100, 200, 500, 1000
#
# Requires the release builds: `cmake --preset release && cmake --build --preset release` and
# `dotnet build -c Release server/dotnet/GameServer.slnx`. A Debug number is not a capacity number.
set -euo pipefail
cd "$(dirname "$0")/../.."
BOTS="${1:?number of bots}"
DURATION="${2:-60}"
SETTLE_SEC=5      # after the bots are provisioned, before sampling: connections and first snapshots
TAIL_SEC=10       # the run's end (sessions closing) stays out of the window
export SA_BUILD="${SA_BUILD:-build/release}"
export SA_DOTNET_CONFIG="${SA_DOTNET_CONFIG:-Release}"
DEV_DIR="${SA_DEV_DIR:-/tmp/sa-dev}"
PROFILE="$DEV_DIR/capacity-$BOTS.toml"
LOADGEN_LOG="$DEV_DIR/capacity-loadgen.log"
mkdir -p "$DEV_DIR"

for binary in "$SA_BUILD/server/cpp/zone/server/zone_server" "$SA_BUILD/server/cpp/gateway/gateway" \
              "$SA_BUILD/tools/loadgen/client/loadgen" \
              "server/dotnet/GameBackend/bin/$SA_DOTNET_CONFIG/net10.0/GameBackend"; do
  [ -x "$binary" ] || { echo "missing $binary: build the release binaries first"; exit 1; }
done
if [ "$DURATION" -le $((SETTLE_SEC + TAIL_SEC)) ]; then echo "duration must exceed $((SETTLE_SEC + TAIL_SEC)) s"; exit 1; fi

# 60 % fighters, 20 % looters, the rest wanderers.
FIGHTERS=$((BOTS * 6 / 10)); LOOTERS=$((BOTS * 2 / 10)); WANDERERS=$((BOTS - FIGHTERS - LOOTERS))
sed -e "s/@WANDERERS@/$WANDERERS/; s/@FIGHTERS@/$FIGHTERS/; s/@LOOTERS@/$LOOTERS/; s/@DURATION@/$DURATION/" \
  scripts/bench/capacity-profile.toml > "$PROFILE"

scripts/dev-stack.sh down >/dev/null 2>&1 || true
scripts/dev-stack.sh up >"$DEV_DIR/capacity-stack.log" 2>&1 || { cat "$DEV_DIR/capacity-stack.log"; exit 1; }
loadgen_pid=""
cleanup() {
  [ -n "$loadgen_pid" ] && kill "$loadgen_pid" 2>/dev/null || true
  scripts/dev-stack.sh down >/dev/null 2>&1 || true
}
trap cleanup EXIT

"$SA_BUILD/tools/loadgen/client/loadgen" --profile "$PROFILE" >"$LOADGEN_LOG" 2>&1 &
loadgen_pid=$!

# Provisioning is sequential HTTP (password hashing per account); the clock starts after it.
until grep -q "provisioned" "$LOADGEN_LOG" 2>/dev/null; do
  kill -0 "$loadgen_pid" 2>/dev/null || { cat "$LOADGEN_LOG"; exit 1; }
  sleep 0.5
done
sleep "$SETTLE_SEC"

declare -A pids=([eventworker]=$(cat "$DEV_DIR/eventworker.pid") [backend]=$(cat "$DEV_DIR/backend.pid")
                 [zone-a]=$(cat "$DEV_DIR/zone-a.pid") [zone-b]=$(cat "$DEV_DIR/zone-b.pid")
                 [gateway]=$(cat "$DEV_DIR/gateway.pid") [loadgen]=$loadgen_pid)
cpu_ticks() { awk '{print $14 + $15}' "/proc/$1/stat"; }  # utime + stime
declare -A start_ticks
for name in "${!pids[@]}"; do start_ticks[$name]=$(cpu_ticks "${pids[$name]}"); done
declare -A first_stats_line deferred_before snapshots_before bytes_before
for zone in zone-a zone-b; do
  first_stats_line[$zone]=$(wc -l < "$DEV_DIR/$zone.log")
  last_line=$(grep 'snapshots_deferred=' "$DEV_DIR/$zone.log" | tail -1)
  deferred_before[$zone]=$(grep -o 'snapshots_deferred=[0-9]*' <<<"$last_line" | cut -d= -f2)
  snapshots_before[$zone]=$(grep -o ' snapshots=[0-9]*' <<<"$last_line" | cut -d= -f2)
  bytes_before[$zone]=$(grep -o 'snapshot_bytes=[0-9]*' <<<"$last_line" | cut -d= -f2)
done
start_ns=$(date +%s%N)
sleep $((DURATION - SETTLE_SEC - TAIL_SEC))
end_ns=$(date +%s%N)
clk=$(getconf CLK_TCK)
elapsed=$(awk -v s="$start_ns" -v e="$end_ns" 'BEGIN{printf "%.1f", (e-s)/1e9}')

echo "## bots=$BOTS (fighters=$FIGHTERS looters=$LOOTERS wanderers=$WANDERERS), steady window ${elapsed}s"
echo
echo "| process | avg CPU (cores) | peak RSS (MiB) |"
echo "|---|---:|---:|"
for name in zone-a zone-b gateway backend eventworker loadgen; do
  pid=${pids[$name]}
  [ -r "/proc/$pid/stat" ] || continue
  cpu=$(awk -v a="${start_ticks[$name]}" -v b="$(cpu_ticks "$pid")" -v c="$clk" -v t="$elapsed" \
        'BEGIN{printf "%.2f", (b-a)/c/t}')
  rss=$(awk '/VmHWM/{printf "%.0f", $2/1024}' "/proc/$pid/status")
  echo "| $name | $cpu | $rss |"
done
echo
docker stats --no-stream --format '| {{.Name}} | {{.CPUPerc}} | {{.MemUsage}} |' sa-postgres sa-kafka sa-redis
echo
# Tick health over the window, from the zones' own 10 s statistics lines logged after sampling
# began: ticks run and dropped, start lag, where the zone thread's time went (simulation step vs
# host delivery, then the simulation step by phase), snapshots deferred by backpressure, the
# deepest gateway link queue, the longest gap between frames from the gateway (against its
# silence timeout), and the snapshot rate and bytes sent to the gateways.
for zone in zone-a zone-b; do
  tail -n +"$((first_stats_line[$zone] + 1))" "$DEV_DIR/$zone.log" \
    | awk -v z="$zone" -v window="$elapsed" -v deferred_base="${deferred_before[$zone]:-0}" \
          -v snapshots_base="${snapshots_before[$zone]:-0}" -v bytes_base="${bytes_before[$zone]:-0}" '
    function field(name,   i) { for (i = 1; i <= NF; i++) if (index($i, name "=") == 1) return substr($i, length(name) + 2); return "" }
    /lag_us=/ { ran += field("ran"); dropped += field("dropped"); lag = field("lag_us") + 0; if (lag > lag_max) lag_max = lag
                lag_sum += lag; n++; players = field("players"); sim += field("sim_us"); host += field("host_us") }
    /snapshots_deferred=/ { deferred = field("snapshots_deferred") + 0
                            snapshots = field("snapshots") + 0; bytes = field("snapshot_bytes") + 0
                            q = field("link_queue_peak_bytes") + 0; if (q > queue_max) queue_max = q
                            g = field("gateway_gap_max_ms") + 0; if (g > gap_max) gap_max = g }
    /interest_us=/ { for (i = 1; i <= NF; i++) if ($i ~ /_us=/) { split($i, kv, "="); sub(/_us$/, "", kv[1]); phase[kv[1]] += kv[2] }
                     phases_seen = 1 }
    END { printf "%s: ticks=%d dropped=%d lag_us max=%d avg=%.0f players(last)=%d sim=%.2f host=%.2f (cores) snapshots_deferred=%d link_queue_peak=%.1f MiB gateway_gap_max=%d ms\n",
          z, ran, dropped, lag_max, (n ? lag_sum / n : 0), players, sim / 1e6 / window, host / 1e6 / window,
          (deferred > 0 ? deferred - deferred_base : 0), queue_max / 1048576, gap_max
          if (snapshots > snapshots_base) printf "%s: snapshots %.0f/s, %.2f MiB/s to the gateways, %.0f bytes each\n", z,
                                                 (snapshots - snapshots_base) / window, (bytes - bytes_base) / window / 1048576,
                                                 (bytes - bytes_base) / (snapshots - snapshots_base)
          if (phases_seen) { printf "%s: simulation phases (cores):", z
                             n_phases = split("commands casts players monsters projectiles upkeep interest", names, " ")
                             for (i = 1; i <= n_phases; i++) printf " %s=%.3f", names[i], phase[names[i]] / 1e6 / window
                             printf "\n" } }'
done
wait "$loadgen_pid" || true
loadgen_pid=""
grep -E 'bots=|snapshots=|casts |pickups |kicked|RESULT' "$LOADGEN_LOG"

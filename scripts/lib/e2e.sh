# Shared by scripts/e2e-local.sh and scripts/e2e-k3s.sh: reading the profile, waiting, and the
# database checks that follow a bot run. Sourced, not run. The caller defines psql_value "<sql>"
# (prints the result rows) and redis_cli <args> (runs redis-cli), and sets PROFILE before sourcing.

# Reads a scalar from the profile; enough for the keys the checks need.
profile_value() { sed -nE "s/^$1[[:space:]]*=[[:space:]]*\"?([^\"#]*)\"?.*/\1/p" "$PROFILE" | head -1 | tr -d ' '; }

wait_for() {  # description, command...
  local what="$1"; shift
  for _ in $(seq 1 60); do "$@" && return 0; sleep 0.5; done
  echo "timed out waiting for $what"; return 1
}

# The statistics consumer's name: CharacterStatsConsumer:GroupId in the EventWorker's appsettings.json.
STATS_CONSUMER=character-stats

# Checks what the database says about the run that started at $1. Prints each finding and returns
# non-zero when any check fails. What the bots saw is not the whole truth: issue 013 passed every
# bot-side check while no kill reached the database.
check_run() {
  local run_started="$1" failed=0
  local account_prefix min_kills min_pickups min_zone_changes min_settlements
  account_prefix="$(profile_value account_prefix)"
  min_kills="$(profile_value min_kills)"; min_kills="${min_kills:-0}"
  min_pickups="$(profile_value min_pickups)"; min_pickups="${min_pickups:-0}"
  min_zone_changes="$(profile_value min_zone_changes)"; min_zone_changes="${min_zone_changes:-0}"
  min_settlements="$(profile_value min_trade_settlements)"; min_settlements="${min_settlements:-0}"

  # Bots have closed their sockets; give the gateway a moment to detach and end their sessions,
  # then check the directory agrees. Only this run's sessions count: a crashed viewer or dev
  # session from before would otherwise fail every later run.
  sleep 1
  local live
  live=$(psql_value "SELECT count(*) FROM game.character_sessions s JOIN game.accounts a USING (account_id)
                     WHERE s.ended_at IS NULL AND s.started_at >= '$run_started'
                       AND a.username LIKE '${account_prefix}\_%'")
  echo "live sessions after run: $live"
  if [ "$live" != "0" ]; then echo "FAIL: sessions left open in the directory"; failed=1; fi

  local recorded acquired
  recorded=$(psql_value "SELECT count(*) FROM game.kill_receipts WHERE created_at >= '$run_started'")
  echo "kills recorded by the backend: $recorded (required $min_kills)"
  if [ "$recorded" -lt "$min_kills" ]; then echo "FAIL: kills were not recorded by the backend"; failed=1; fi
  acquired=$(psql_value "SELECT count(*) FROM game.economy_ledger WHERE kind = 'item_acquired' AND created_at >= '$run_started'")
  echo "items acquired in the ledger: $acquired (required $min_pickups)"
  if [ "$acquired" -lt "$min_pickups" ]; then echo "FAIL: pickups were not recorded by the backend"; failed=1; fi

  # A crossing the bots saw is a Welcome from another zone; the Backend must have moved the
  # ownership for each, and every character of the run must be owned by the zone it last entered,
  # under the generation of its last transfer.
  local transferred
  transferred=$(psql_value "SELECT count(*) FROM game.zone_transfers WHERE outcome = 'transferred' AND created_at >= '$run_started'")
  echo "zone transfers recorded by the backend: $transferred (required $min_zone_changes)"
  if [ "$transferred" -lt "$min_zone_changes" ]; then echo "FAIL: zone changes were not recorded by the backend"; failed=1; fi
  local stray
  stray=$(psql_value "SELECT count(*) FROM (
                        SELECT DISTINCT ON (t.world_id, t.character_id) t.world_id, t.character_id, t.to_zone_id, t.to_owner_epoch
                        FROM game.zone_transfers t WHERE t.outcome = 'transferred' AND t.created_at >= '$run_started'
                        ORDER BY t.world_id, t.character_id, t.created_at DESC) last
                      JOIN game.character_ownership o USING (world_id, character_id)
                      WHERE o.owner_epoch = last.to_owner_epoch AND o.zone_id <> last.to_zone_id")
  if [ "$stray" != "0" ]; then echo "FAIL: $stray characters are owned elsewhere than their last transfer put them"; failed=1; fi

  # A settlement the bots saw counts once per side; the Backend settles each trade once and writes
  # one TradeCompleted per participant. Every session has ended by now, and a session that ends
  # cancels its open trade, so none of this run's trades may still be open.
  local settled completed_events still_open
  settled=$(psql_value "SELECT count(*) FROM game.trades WHERE status = 'completed' AND created_at >= '$run_started'")
  echo "trades settled by the backend: $settled, $((settled * 2)) settlements (required $min_settlements)"
  if [ $((settled * 2)) -lt "$min_settlements" ]; then echo "FAIL: trades were not settled by the backend"; failed=1; fi
  completed_events=$(psql_value "SELECT count(*) FROM game.outbox_events WHERE event_type = 'TradeCompleted' AND occurred_at >= '$run_started'")
  if [ "$completed_events" != "$((settled * 2))" ]; then
    echo "FAIL: $completed_events TradeCompleted events for $settled settled trades (one per participant expected)"; failed=1
  fi
  still_open=$(psql_value "SELECT count(*) FROM game.trades WHERE status = 'open' AND created_at >= '$run_started'")
  if [ "$still_open" != "0" ]; then echo "FAIL: $still_open trades of this run are still open after every session ended"; failed=1; fi

  # Every event this run's transactions wrote must reach Kafka and be applied by the statistics
  # consumer, each once (its Inbox has one row per event). Both poll, so allow them a few intervals.
  CHECK_RUN_STARTED="$run_started"
  CHECK_WRITTEN=$(psql_value "SELECT count(*) FROM game.outbox_events WHERE occurred_at >= '$run_started'")
  if wait_for "the outbox to drain" outbox_drained && wait_for "the consumer to catch up" events_applied; then
    echo "events written, published and applied: $CHECK_WRITTEN"
  else
    echo "FAIL: events of this run were not all published and applied"; failed=1
  fi

  # The read model counts one settled trade per TradeCompleted of the character, whenever written.
  local trade_events projected_trades
  trade_events=$(psql_value "SELECT count(*) FROM game.outbox_events WHERE event_type = 'TradeCompleted'
                             AND (world_id, aggregate_id) IN (SELECT world_id, aggregate_id FROM game.outbox_events
                                                              WHERE event_type = 'TradeCompleted' AND occurred_at >= '$run_started')")
  projected_trades=$(psql_value "SELECT coalesce(sum(p.trades_completed), 0) FROM game.character_projections p
                                 WHERE (p.world_id, p.character_id) IN (SELECT world_id, aggregate_id FROM game.outbox_events
                                                                        WHERE event_type = 'TradeCompleted' AND occurred_at >= '$run_started')")
  if [ "$projected_trades" != "$trade_events" ]; then
    echo "FAIL: the read model counts $projected_trades settled trades for $trade_events TradeCompleted events"; failed=1
  fi

  # The Redis ranking is a copy of the statistics: for every character this run touched, the
  # sorted set holds the same kill total (server/dotnet/Shared/Ranking/RankingKeys.cs: the member is
  # long.MaxValue - id, 19 digits wide).
  if wait_for "the ranking to match the statistics" ranking_mirrors_statistics; then
    echo "ranking matches the statistics for this run's characters"
  else
    echo "FAIL: the Redis ranking differs from the statistics: $CHECK_RANKING_MISMATCH"; failed=1
  fi
  return $failed
}

ranking_mirrors_statistics() {
  local rows world character kills member score
  CHECK_RANKING_MISMATCH=""
  rows=$(psql_value "SELECT p.world_id || ' ' || p.character_id || ' ' || p.kills FROM game.character_projections p
                     WHERE (p.world_id, p.character_id) IN (SELECT DISTINCT world_id, aggregate_id FROM game.outbox_events
                                                            WHERE occurred_at >= '$CHECK_RUN_STARTED' AND aggregate_type = 'character')")
  while read -r world character kills; do
    [ -n "$world" ] || continue
    member=$(printf '%019d' $((9223372036854775807 - character)))
    score=$(redis_cli ZSCORE "ranking:kills:v1:$world" "$member")
    [ "$score" = "$kills" ] || CHECK_RANKING_MISMATCH+="character $character: statistics $kills, ranking ${score:-none}; "
  done <<< "$rows"
  [ -z "$CHECK_RANKING_MISMATCH" ]
}

outbox_drained() {
  [ "$(psql_value "SELECT count(*) FROM game.outbox_events WHERE occurred_at >= '$CHECK_RUN_STARTED' AND published_at IS NULL")" = 0 ]
}

events_applied() {
  [ "$(psql_value "SELECT count(*) FROM game.outbox_events o JOIN game.inbox_events i
                   ON i.event_id = o.event_id AND i.consumer_name = '$STATS_CONSUMER'
                   WHERE o.occurred_at >= '$CHECK_RUN_STARTED'")" = "$CHECK_WRITTEN" ]
}

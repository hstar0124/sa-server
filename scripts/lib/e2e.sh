# Shared by scripts/e2e-local.sh and scripts/e2e-k3s.sh: reading the profile, waiting, and the
# database checks that follow a bot run. Sourced, not run. The caller defines
# psql_value "<sql>" (prints one value) and sets PROFILE before sourcing.

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
  local account_prefix min_kills min_pickups
  account_prefix="$(profile_value account_prefix)"
  min_kills="$(profile_value min_kills)"; min_kills="${min_kills:-0}"
  min_pickups="$(profile_value min_pickups)"; min_pickups="${min_pickups:-0}"

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

  # Every event this run's transactions wrote must reach Kafka and be applied by the statistics
  # consumer, each once (its Inbox has one row per event). Both poll, so allow them a few intervals.
  CHECK_RUN_STARTED="$run_started"
  CHECK_WRITTEN=$(psql_value "SELECT count(*) FROM game.outbox_events WHERE occurred_at >= '$run_started'")
  if wait_for "the outbox to drain" outbox_drained && wait_for "the consumer to catch up" events_applied; then
    echo "events written, published and applied: $CHECK_WRITTEN"
  else
    echo "FAIL: events of this run were not all published and applied"; failed=1
  fi
  return $failed
}

outbox_drained() {
  [ "$(psql_value "SELECT count(*) FROM game.outbox_events WHERE occurred_at >= '$CHECK_RUN_STARTED' AND published_at IS NULL")" = 0 ]
}

events_applied() {
  [ "$(psql_value "SELECT count(*) FROM game.outbox_events o JOIN game.inbox_events i
                   ON i.event_id = o.event_id AND i.consumer_name = '$STATS_CONSUMER'
                   WHERE o.occurred_at >= '$CHECK_RUN_STARTED'")" = "$CHECK_WRITTEN" ]
}

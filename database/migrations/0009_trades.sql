-- Player-to-player trades (ADR-0013). The negotiation lives here, not in a zone's memory, and the
-- rules that must never break are constraints: one open trade per character, one open trade per
-- item, two distinct parties, no negative gold, an acceptance names a version that existed.

CREATE TABLE game.trades (
    trade_id                       bytea       PRIMARY KEY,
    world_id                       bigint      NOT NULL,
    initiator_id                   bigint      NOT NULL,
    counterparty_id                bigint      NOT NULL,
    status                         text        NOT NULL,
    version                        integer     NOT NULL DEFAULT 0,  -- grows on every offer change
    initiator_gold                 bigint      NOT NULL DEFAULT 0,
    counterparty_gold              bigint      NOT NULL DEFAULT 0,
    initiator_accepted_version     integer,                         -- NULL: not accepted
    counterparty_accepted_version  integer,
    expires_at                     timestamptz NOT NULL,            -- refreshed on every change
    created_at                     timestamptz NOT NULL DEFAULT now(),
    updated_at                     timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT trades_initiator_fkey
        FOREIGN KEY (world_id, initiator_id) REFERENCES game.characters (world_id, character_id),
    CONSTRAINT trades_counterparty_fkey
        FOREIGN KEY (world_id, counterparty_id) REFERENCES game.characters (world_id, character_id),
    CONSTRAINT trades_id_len CHECK (octet_length(trade_id) = 16),
    CONSTRAINT trades_status CHECK (status IN ('open', 'completed', 'cancelled')),
    CONSTRAINT trades_distinct_parties CHECK (initiator_id <> counterparty_id),
    CONSTRAINT trades_version_non_negative CHECK (version >= 0),
    CONSTRAINT trades_gold_non_negative CHECK (initiator_gold >= 0 AND counterparty_gold >= 0),
    -- An acceptance is of a version the trade has had; a cleared one is NULL.
    CONSTRAINT trades_accepted_known_version CHECK (
        (initiator_accepted_version IS NULL
            OR initiator_accepted_version BETWEEN 0 AND version)
        AND (counterparty_accepted_version IS NULL
            OR counterparty_accepted_version BETWEEN 0 AND version)),
    CONSTRAINT trades_lifetime CHECK (expires_at > created_at)
);

-- A character is in at most one open trade: the row exists only while the trade is open.
CREATE TABLE game.trade_participants (
    world_id      bigint NOT NULL,
    character_id  bigint NOT NULL,
    trade_id      bytea  NOT NULL REFERENCES game.trades (trade_id),
    PRIMARY KEY (world_id, character_id),
    CONSTRAINT trade_participants_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id)
);

CREATE INDEX trade_participants_trade_idx ON game.trade_participants (trade_id);

-- An item sits in at most one open trade. The row exists only while the trade is open, so the
-- same item can be traded again later. Offering an item elsewhere while it is here fails on the
-- primary key, whatever the application checked first.
CREATE TABLE game.trade_items (
    item_instance_id  bytea  PRIMARY KEY REFERENCES game.item_instances (item_instance_id),
    trade_id          bytea  NOT NULL REFERENCES game.trades (trade_id),
    world_id          bigint NOT NULL,
    offered_by        bigint NOT NULL,
    CONSTRAINT trade_items_offerer_fkey
        FOREIGN KEY (world_id, offered_by) REFERENCES game.characters (world_id, character_id)
);

CREATE INDEX trade_items_trade_idx ON game.trade_items (trade_id);

-- Trade calls are idempotent like pickups, and their gold and item movements are ledgered.
ALTER TABLE game.command_results
    DROP CONSTRAINT command_results_scope,
    ADD CONSTRAINT command_results_scope CHECK (scope IN ('pickup', 'trade'));

ALTER TABLE game.economy_ledger
    DROP CONSTRAINT economy_ledger_reason,
    ADD CONSTRAINT economy_ledger_reason CHECK (reason IN ('kill', 'pickup', 'trade'));

-- The read model counts settled trades (one TradeCompleted per participant).
ALTER TABLE game.character_projections
    ADD COLUMN trades_completed bigint NOT NULL DEFAULT 0,
    DROP CONSTRAINT character_projections_non_negative,
    ADD CONSTRAINT character_projections_non_negative CHECK (
        kills >= 0 AND items_acquired >= 0 AND gold_earned >= 0 AND trades_completed >= 0
        AND last_event_seq >= 0);

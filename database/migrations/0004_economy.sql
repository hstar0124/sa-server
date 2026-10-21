-- Persistent assets (see README "자산과 멱등성"). The asset invariants are constraints here,
-- not only checks in code: one owner per item instance, no negative balances, one grant
-- per kill, one effect per request id.

-- Individual items. An item is either on the ground in a zone or in exactly one inventory slot.
CREATE TABLE game.item_instances (
    item_instance_id    bytea       PRIMARY KEY,
    world_id            bigint      NOT NULL REFERENCES game.worlds (world_id),
    item_template_id    integer     NOT NULL,
    location            text        NOT NULL,
    owner_character_id  bigint      REFERENCES game.characters (character_id),
    slot_no             integer,
    zone_id             integer,
    pos_x               real,
    pos_y               real,
    origin_kill_id      bytea,
    version             integer     NOT NULL DEFAULT 1,
    created_at          timestamptz NOT NULL DEFAULT now(),
    updated_at          timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT item_instances_id_len CHECK (octet_length(item_instance_id) = 16),
    CONSTRAINT item_instances_location CHECK (location IN ('ground', 'inventory')),
    -- Where it is decides which columns must be present.
    CONSTRAINT item_instances_placement CHECK (
        (location = 'inventory' AND owner_character_id IS NOT NULL AND slot_no IS NOT NULL AND zone_id IS NULL)
     OR (location = 'ground'    AND owner_character_id IS NULL     AND slot_no IS NULL     AND zone_id IS NOT NULL
                                 AND pos_x IS NOT NULL AND pos_y IS NOT NULL))
);

-- One item per slot per character.
CREATE UNIQUE INDEX item_instances_one_per_slot
    ON game.item_instances (world_id, owner_character_id, slot_no)
    WHERE location = 'inventory';

CREATE INDEX item_instances_owner_idx ON game.item_instances (world_id, owner_character_id) WHERE location = 'inventory';
CREATE INDEX item_instances_ground_idx ON game.item_instances (world_id, zone_id) WHERE location = 'ground';

-- Capacity and a version that advances on every change; clients compare versions.
CREATE TABLE game.inventories (
    world_id      bigint  NOT NULL REFERENCES game.worlds (world_id),
    character_id  bigint  NOT NULL REFERENCES game.characters (character_id),
    capacity      integer NOT NULL,
    version       integer NOT NULL DEFAULT 1,
    PRIMARY KEY (world_id, character_id),
    CONSTRAINT inventories_capacity_positive CHECK (capacity > 0)
);

-- Integer balances in the smallest unit; never negative (see README "자산과 멱등성").
CREATE TABLE game.wallets (
    world_id      bigint  NOT NULL REFERENCES game.worlds (world_id),
    character_id  bigint  NOT NULL REFERENCES game.characters (character_id),
    currency      text    NOT NULL,
    balance       bigint  NOT NULL DEFAULT 0,
    version       integer NOT NULL DEFAULT 1,
    PRIMARY KEY (world_id, character_id, currency),
    CONSTRAINT wallets_balance_non_negative CHECK (balance >= 0)
);

-- Append-only record of every grant, debit and transfer.
CREATE TABLE game.economy_ledger (
    entry_id          bigint      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    world_id          bigint      NOT NULL REFERENCES game.worlds (world_id),
    character_id      bigint      NOT NULL REFERENCES game.characters (character_id),
    kind              text        NOT NULL,
    item_instance_id  bytea,
    currency          text,
    amount            bigint,
    reason            text        NOT NULL,
    request_id        bytea,
    created_at        timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT economy_ledger_kind CHECK (kind IN ('item_acquired', 'currency_credit', 'currency_debit', 'item_transferred'))
);

CREATE INDEX economy_ledger_character_idx ON game.economy_ledger (world_id, character_id, entry_id);

-- One receipt per monster lifecycle: the grant basis is unique regardless of request ids.
CREATE TABLE game.kill_receipts (
    kill_id              bytea       PRIMARY KEY,
    world_id             bigint      NOT NULL REFERENCES game.worlds (world_id),
    zone_instance_id     bytea       NOT NULL REFERENCES game.zone_leases (zone_instance_id),
    owner_epoch          bigint      NOT NULL,
    killer_character_id  bigint      NOT NULL REFERENCES game.characters (character_id),
    monster_template_id  integer     NOT NULL,
    content_version      text        NOT NULL,
    gold_granted         bigint      NOT NULL,
    created_at           timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT kill_receipts_id_len CHECK (octet_length(kill_id) = 16)
);

-- Idempotency: a request row is claimed with INSERT ... ON CONFLICT; the loser reads the
-- winner's stored outcome. The hash detects a different body under the same id.
CREATE TABLE game.command_results (
    world_id      bigint      NOT NULL REFERENCES game.worlds (world_id),
    scope         text        NOT NULL,
    request_id    bytea       NOT NULL,
    request_hash  bytea       NOT NULL,
    outcome       text        NOT NULL,
    payload       jsonb       NOT NULL DEFAULT '{}'::jsonb,
    created_at    timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (world_id, scope, request_id),
    CONSTRAINT command_results_request_len CHECK (octet_length(request_id) = 16)
);

-- Transactional outbox. Rows are written in the same transaction as the business change; stage 6
-- publishes them.
CREATE TABLE game.aggregate_sequences (
    world_id        bigint NOT NULL,
    aggregate_type  text   NOT NULL,
    aggregate_id    bigint NOT NULL,
    next_seq        bigint NOT NULL DEFAULT 1,
    PRIMARY KEY (world_id, aggregate_type, aggregate_id)
);

CREATE TABLE game.outbox_events (
    event_id        bytea       PRIMARY KEY,
    world_id        bigint      NOT NULL,
    aggregate_type  text        NOT NULL,
    aggregate_id    bigint      NOT NULL,
    aggregate_seq   bigint      NOT NULL,
    event_type      text        NOT NULL,
    schema_version  integer     NOT NULL,
    request_id      bytea,
    payload         bytea       NOT NULL,
    occurred_at     timestamptz NOT NULL DEFAULT now(),
    published_at    timestamptz,
    CONSTRAINT outbox_events_id_len CHECK (octet_length(event_id) = 16),
    CONSTRAINT outbox_events_seq_unique UNIQUE (world_id, aggregate_type, aggregate_id, aggregate_seq)
);

CREATE INDEX outbox_events_unpublished_idx ON game.outbox_events (occurred_at) WHERE published_at IS NULL;

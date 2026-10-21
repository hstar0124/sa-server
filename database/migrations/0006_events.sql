-- The consumer side of the event pipeline. Outbox rows are written with the business change (0004);
-- a consumer applies each published event once and keeps read models derived from them.

-- Every event a consumer has applied. The primary key is the fence: an event delivered again (the
-- publisher crashed after Kafka acknowledged it, or the consumer crashed before committing its
-- offset) conflicts here, and its change is skipped.
CREATE TABLE game.inbox_events (
    consumer_name  text        NOT NULL,
    event_id       bytea       NOT NULL,
    processed_at   timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (consumer_name, event_id),
    CONSTRAINT inbox_events_id_len CHECK (octet_length(event_id) = 16),
    CONSTRAINT inbox_events_consumer_format CHECK (consumer_name ~ '^[a-z][a-z0-9-]*$')
);

-- Per-character statistics derived from confirmed events. Rebuildable: delete the rows and the
-- consumer's inbox entries, then replay the topic. A row names the character's own world.
CREATE TABLE game.character_projections (
    world_id        bigint      NOT NULL,
    character_id    bigint      NOT NULL,
    kills           bigint      NOT NULL DEFAULT 0,
    items_acquired  bigint      NOT NULL DEFAULT 0,
    gold_earned     bigint      NOT NULL DEFAULT 0,
    last_event_seq  bigint      NOT NULL DEFAULT 0,  -- highest aggregate_seq applied
    updated_at      timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (world_id, character_id),
    CONSTRAINT character_projections_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    CONSTRAINT character_projections_non_negative
        CHECK (kills >= 0 AND items_acquired >= 0 AND gold_earned >= 0 AND last_event_seq >= 0)
);

-- Zone transfers and checkpoints (ADR-0010). Ownership already lives in character_ownership; these
-- add where the character last stood and the record of each hand-over the Backend decided.

-- Last saved position. The owning zone writes it periodically, and the Backend writes the arrival
-- point on a transfer. Saves compare owner_epoch and checkpoint_version, so the previous owner's
-- late save can never overwrite the current position.
CREATE TABLE game.character_checkpoints (
    world_id            bigint      NOT NULL,
    character_id        bigint      NOT NULL,
    zone_id             integer     NOT NULL,
    pos_x               real        NOT NULL,
    pos_y               real        NOT NULL,
    owner_epoch         bigint      NOT NULL,
    checkpoint_version  bigint      NOT NULL,
    saved_at            timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (world_id, character_id),
    CONSTRAINT character_checkpoints_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    CONSTRAINT character_checkpoints_zone_positive CHECK (zone_id > 0),
    CONSTRAINT character_checkpoints_epoch_positive CHECK (owner_epoch > 0),
    CONSTRAINT character_checkpoints_version_non_negative CHECK (checkpoint_version >= 0),
    -- PostgreSQL holds NaN equal to itself, so a self-comparison would let it through; name it.
    CONSTRAINT character_checkpoints_position_finite CHECK (
        pos_x NOT IN ('NaN'::real, 'Infinity'::real, '-Infinity'::real)
        AND pos_y NOT IN ('NaN'::real, 'Infinity'::real, '-Infinity'::real))
);

-- One row per transfer the Backend decided. The primary key makes a retried transfer_id return
-- the first decision instead of moving the character twice. Only a transfer that happened names
-- its destination.
CREATE TABLE game.zone_transfers (
    transfer_id            bytea       PRIMARY KEY,
    world_id               bigint      NOT NULL,
    character_id           bigint      NOT NULL,
    from_zone_instance_id  bytea       NOT NULL,
    from_owner_epoch       bigint      NOT NULL,
    to_zone_id             integer     NOT NULL,
    to_zone_instance_id    bytea,
    to_owner_epoch         bigint,
    outcome                text        NOT NULL,
    created_at             timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT zone_transfers_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    CONSTRAINT zone_transfers_id_len CHECK (octet_length(transfer_id) = 16),
    CONSTRAINT zone_transfers_from_instance_len CHECK (octet_length(from_zone_instance_id) = 16),
    CONSTRAINT zone_transfers_from_epoch_positive CHECK (from_owner_epoch > 0),
    CONSTRAINT zone_transfers_to_zone_positive CHECK (to_zone_id > 0),
    CONSTRAINT zone_transfers_outcome CHECK (outcome IN ('transferred', 'stale_authority', 'no_zone_available', 'same_zone')),
    CONSTRAINT zone_transfers_result CHECK (
        (outcome = 'transferred' AND to_zone_instance_id IS NOT NULL AND octet_length(to_zone_instance_id) = 16
            AND to_owner_epoch IS NOT NULL AND to_owner_epoch > from_owner_epoch)
     OR (outcome <> 'transferred' AND to_zone_instance_id IS NULL AND to_owner_epoch IS NULL))
);

CREATE INDEX zone_transfers_character_idx ON game.zone_transfers (world_id, character_id, created_at);

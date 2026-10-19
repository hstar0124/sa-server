-- World directory (see README "소유권과 lease", ADR-0005): process leases, entry tickets, character
-- ownership and sessions. The constraints are the fence: a ticket row can be inserted once, a
-- character can have one live session, and ownership always names exactly one zone.

-- A zone process announces itself and keeps the lease fresh. Ownership is granted only to zones
-- whose lease has not expired. A restart is a new instance id, never a reused one.
CREATE TABLE game.zone_leases (
    zone_instance_id  bytea       PRIMARY KEY,
    world_id          bigint      NOT NULL REFERENCES game.worlds (world_id),
    zone_id           integer     NOT NULL,
    first_seen_at     timestamptz NOT NULL DEFAULT now(),
    lease_expires_at  timestamptz NOT NULL,
    CONSTRAINT zone_leases_instance_len CHECK (octet_length(zone_instance_id) = 16),
    CONSTRAINT zone_leases_zone_positive CHECK (zone_id > 0)
);

CREATE INDEX zone_leases_world_zone_idx ON game.zone_leases (world_id, zone_id, lease_expires_at);

-- Gateways hold leases too; sessions belong to a gateway lease and die with it.
CREATE TABLE game.gateway_leases (
    gateway_instance_id  bytea       PRIMARY KEY,
    first_seen_at        timestamptz NOT NULL DEFAULT now(),
    lease_expires_at     timestamptz NOT NULL,
    CONSTRAINT gateway_leases_instance_len CHECK (octet_length(gateway_instance_id) = 16)
);

-- One row per issued ticket; redemption is a conditional UPDATE, so a second redeem sees
-- redeemed_at already set and loses (no SELECT-then-decide).
CREATE TABLE game.entry_tickets (
    ticket_id     bytea       PRIMARY KEY,
    account_id    bigint      NOT NULL REFERENCES game.accounts (account_id),
    character_id  bigint      NOT NULL REFERENCES game.characters (character_id),
    world_id      bigint      NOT NULL REFERENCES game.worlds (world_id),
    issued_at     timestamptz NOT NULL DEFAULT now(),
    expires_at    timestamptz NOT NULL,
    redeemed_at   timestamptz,
    CONSTRAINT entry_tickets_id_len CHECK (octet_length(ticket_id) = 16)
);

CREATE INDEX entry_tickets_expires_idx ON game.entry_tickets (expires_at);

-- Which zone may simulate the character right now, and the fencing counter (owner_epoch).
CREATE TABLE game.character_ownership (
    world_id          bigint   NOT NULL REFERENCES game.worlds (world_id),
    character_id      bigint   NOT NULL REFERENCES game.characters (character_id),
    zone_id           integer  NOT NULL,
    zone_instance_id  bytea    NOT NULL REFERENCES game.zone_leases (zone_instance_id),
    owner_epoch       bigint   NOT NULL,
    state             text     NOT NULL,
    updated_at        timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (world_id, character_id),
    CONSTRAINT character_ownership_epoch_positive CHECK (owner_epoch > 0),
    CONSTRAINT character_ownership_state CHECK (state IN ('active', 'transferring', 'released'))
);

-- Gateway sessions. A character has at most one live session (partial unique index).
CREATE TABLE game.character_sessions (
    gateway_session_id   bytea       PRIMARY KEY,
    gateway_instance_id  bytea       NOT NULL REFERENCES game.gateway_leases (gateway_instance_id),
    world_id             bigint      NOT NULL REFERENCES game.worlds (world_id),
    character_id         bigint      NOT NULL REFERENCES game.characters (character_id),
    account_id           bigint      NOT NULL REFERENCES game.accounts (account_id),
    owner_epoch          bigint      NOT NULL,
    started_at           timestamptz NOT NULL DEFAULT now(),
    ended_at             timestamptz,
    end_reason           text,
    CONSTRAINT character_sessions_id_len CHECK (octet_length(gateway_session_id) = 16)
);

CREATE UNIQUE INDEX character_sessions_one_live_per_character
    ON game.character_sessions (character_id)
    WHERE ended_at IS NULL;

CREATE INDEX character_sessions_gateway_idx ON game.character_sessions (gateway_instance_id) WHERE ended_at IS NULL;

-- Second world: lets tests and later stages exercise "no zone serves this world" and World-local
-- economies without touching world 1.
INSERT INTO game.worlds (world_id, name) VALUES (2, 'beta');

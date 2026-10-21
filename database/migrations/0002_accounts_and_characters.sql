-- Accounts, worlds and characters (see README "데이터의 기준"). Integrity rules live here as constraints,
-- not only in application code (CLAUDE.md rule 7).

CREATE TABLE game.accounts (
    account_id     bigint      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    username       text        NOT NULL,
    password_hash  text        NOT NULL,
    created_at     timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT accounts_username_unique UNIQUE (username),
    CONSTRAINT accounts_username_format CHECK (username ~ '^[a-z0-9_]{3,24}$')
);

COMMENT ON COLUMN game.accounts.password_hash IS 'ASP.NET Core Identity PasswordHasher v3 format (PBKDF2-HMAC-SHA512).';

-- A world is the boundary of one economy. Rows are seeded, not created by players.
CREATE TABLE game.worlds (
    world_id   bigint PRIMARY KEY,
    name       text   NOT NULL,
    CONSTRAINT worlds_name_unique UNIQUE (name),
    CONSTRAINT worlds_id_positive CHECK (world_id > 0)
);

INSERT INTO game.worlds (world_id, name) VALUES (1, 'alpha');

CREATE TABLE game.characters (
    character_id  bigint      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    world_id      bigint      NOT NULL REFERENCES game.worlds (world_id),
    account_id    bigint      NOT NULL REFERENCES game.accounts (account_id),
    name          text        NOT NULL,
    class_id      integer     NOT NULL,
    created_at    timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT characters_name_format CHECK (name ~ '^[A-Za-z0-9가-힣]{2,16}$')
);

COMMENT ON COLUMN game.characters.class_id IS 'Refers to game-data class definitions; validated by the Backend against the content version.';

-- Character names are unique per world, case-insensitively.
CREATE UNIQUE INDEX characters_name_unique ON game.characters (world_id, lower(name));

-- Listing an account's characters in a world is the hot read of the character-select screen.
CREATE INDEX characters_account_world_idx ON game.characters (account_id, world_id);

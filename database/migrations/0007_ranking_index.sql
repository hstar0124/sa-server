-- The kill ranking reads the character statistics read model. Top-N is an index scan with LIMIT; a
-- character's rank counts the index range ahead of it, so its cost grows with the rank itself.
CREATE INDEX character_projections_kills_idx
    ON game.character_projections (world_id, kills DESC, character_id);

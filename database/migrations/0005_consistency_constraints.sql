-- Rules the Backend already keeps in code, now kept by the database too, so a bug or a hand-written
-- statement cannot break them (see README "데이터의 기준"). Forward-only like every migration.

-- A character's world and account are fixed when it is created. Every row that names a character
-- together with a world or an account must agree with the character itself.
ALTER TABLE game.characters
    ADD CONSTRAINT characters_world_character_unique UNIQUE (world_id, character_id),
    ADD CONSTRAINT characters_character_account_unique UNIQUE (character_id, account_id),
    ADD CONSTRAINT characters_class_positive CHECK (class_id > 0);

ALTER TABLE game.entry_tickets
    ADD CONSTRAINT entry_tickets_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT entry_tickets_character_account_fkey
        FOREIGN KEY (character_id, account_id) REFERENCES game.characters (character_id, account_id),
    ADD CONSTRAINT entry_tickets_lifetime CHECK (expires_at > issued_at),
    ADD CONSTRAINT entry_tickets_redeemed_after_issue CHECK (redeemed_at IS NULL OR redeemed_at >= issued_at);

-- The owning zone instance is a lease of the same world and zone id the ownership row names.
ALTER TABLE game.zone_leases
    ADD CONSTRAINT zone_leases_identity_unique UNIQUE (zone_instance_id, world_id, zone_id);

ALTER TABLE game.character_ownership
    ADD CONSTRAINT character_ownership_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT character_ownership_zone_lease_fkey
        FOREIGN KEY (zone_instance_id, world_id, zone_id) REFERENCES game.zone_leases (zone_instance_id, world_id, zone_id),
    ADD CONSTRAINT character_ownership_zone_positive CHECK (zone_id > 0);

-- A session ends with a reason, and only after it started.
ALTER TABLE game.character_sessions
    ADD CONSTRAINT character_sessions_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT character_sessions_character_account_fkey
        FOREIGN KEY (character_id, account_id) REFERENCES game.characters (character_id, account_id),
    ADD CONSTRAINT character_sessions_epoch_positive CHECK (owner_epoch > 0),
    ADD CONSTRAINT character_sessions_end CHECK ((ended_at IS NULL) = (end_reason IS NULL)),
    ADD CONSTRAINT character_sessions_end_after_start CHECK (ended_at IS NULL OR ended_at >= started_at);

-- Assets. An inventory nobody has changed yet is version 0; the code has always started there.
ALTER TABLE game.inventories
    ALTER COLUMN version SET DEFAULT 0,
    ADD CONSTRAINT inventories_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT inventories_version_non_negative CHECK (version >= 0);

ALTER TABLE game.wallets
    ADD CONSTRAINT wallets_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT wallets_currency_format CHECK (currency ~ '^[a-z_]+$'),
    ADD CONSTRAINT wallets_version_positive CHECK (version > 0);

ALTER TABLE game.item_instances
    ADD CONSTRAINT item_instances_world_owner_fkey
        FOREIGN KEY (world_id, owner_character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT item_instances_origin_kill_fkey
        FOREIGN KEY (origin_kill_id) REFERENCES game.kill_receipts (kill_id),
    ADD CONSTRAINT item_instances_template_positive CHECK (item_template_id > 0),
    ADD CONSTRAINT item_instances_slot_non_negative CHECK (slot_no IS NULL OR slot_no >= 0),
    ADD CONSTRAINT item_instances_zone_positive CHECK (zone_id IS NULL OR zone_id > 0),
    ADD CONSTRAINT item_instances_version_positive CHECK (version > 0);

-- A ledger row is a currency movement or an item movement, never half of each. Every column the
-- branch needs is tested with IS NOT NULL: a comparison with NULL is unknown, and CHECK lets
-- unknown through.
ALTER TABLE game.economy_ledger
    ADD CONSTRAINT economy_ledger_world_character_fkey
        FOREIGN KEY (world_id, character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT economy_ledger_shape CHECK (
        (kind IN ('currency_credit', 'currency_debit')
            AND currency IS NOT NULL AND amount IS NOT NULL AND amount > 0 AND item_instance_id IS NULL)
     OR (kind IN ('item_acquired', 'item_transferred')
            AND item_instance_id IS NOT NULL AND currency IS NULL AND amount IS NULL)),
    ADD CONSTRAINT economy_ledger_reason CHECK (reason IN ('kill', 'pickup'));

ALTER TABLE game.kill_receipts
    ADD CONSTRAINT kill_receipts_world_killer_fkey
        FOREIGN KEY (world_id, killer_character_id) REFERENCES game.characters (world_id, character_id),
    ADD CONSTRAINT kill_receipts_epoch_positive CHECK (owner_epoch > 0),
    ADD CONSTRAINT kill_receipts_gold_non_negative CHECK (gold_granted >= 0),
    ADD CONSTRAINT kill_receipts_template_positive CHECK (monster_template_id > 0),
    ADD CONSTRAINT kill_receipts_content_version_present CHECK (content_version <> '');

-- Idempotency records: the scopes that exist, and a SHA-256 body hash.
ALTER TABLE game.command_results
    ADD CONSTRAINT command_results_scope CHECK (scope IN ('pickup')),
    ADD CONSTRAINT command_results_hash_len CHECK (octet_length(request_hash) = 32);

ALTER TABLE game.aggregate_sequences
    ADD CONSTRAINT aggregate_sequences_world_fkey FOREIGN KEY (world_id) REFERENCES game.worlds (world_id),
    ADD CONSTRAINT aggregate_sequences_next_positive CHECK (next_seq > 0);

ALTER TABLE game.outbox_events
    ADD CONSTRAINT outbox_events_world_fkey FOREIGN KEY (world_id) REFERENCES game.worlds (world_id),
    ADD CONSTRAINT outbox_events_seq_positive CHECK (aggregate_seq > 0),
    ADD CONSTRAINT outbox_events_schema_positive CHECK (schema_version > 0);

using System.Data;
using System.Security.Cryptography;
using System.Text.Json;
using Dapper;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Economy;

/// <summary>Which zone process claims to act for a character, and under which ownership generation (design document 14.3).</summary>
internal sealed record ZoneAuthority(InstanceId ZoneInstanceId, long OwnerEpoch);

internal sealed record DropRoll(int ItemTemplateId, float X, float Y);

internal sealed record RecordKillCommand(
    KillId KillId,
    WorldId WorldId,
    CharacterId Killer,
    ZoneAuthority Authority,
    int MonsterTemplateId,
    string ContentVersion,
    IReadOnlyList<DropRoll> Drops);

internal enum KillOutcome
{
    Recorded,
    AlreadyRecorded,
    StaleAuthority,
    ContentVersionMismatch,
    UnknownTemplate,
}

internal sealed record DroppedItem(ItemInstanceId Id, int ItemTemplateId, float X, float Y);

internal sealed record KillResult(KillOutcome Outcome, IReadOnlyList<DroppedItem> Items, long GoldGranted, long GoldBalance)
{
    public static KillResult Refused(KillOutcome outcome) => new(outcome, [], 0, 0);
}

internal sealed record PickupCommand(RequestId RequestId, WorldId WorldId, CharacterId CharacterId, ZoneAuthority Authority, ItemInstanceId ItemInstanceId)
{
    /// <summary>Digest of everything but the request id: a retry matches it, a different command under the same id does not.</summary>
    public byte[] BodyHash()
    {
        Span<byte> body = stackalloc byte[sizeof(long) * 2 + OpaqueId.Length];
        BitConverter.TryWriteBytes(body, WorldId.Value);
        BitConverter.TryWriteBytes(body[sizeof(long)..], CharacterId.Value);
        ItemInstanceId.Bytes.CopyTo(body[(sizeof(long) * 2)..]);
        return SHA256.HashData(body);
    }
}

internal enum PickupOutcome
{
    Acquired,
    AlreadyTaken,
    InventoryFull,
    StaleAuthority,
    RequestMismatch,
    UnknownItem,
}

internal sealed record InventoryItem(ItemInstanceId Id, int ItemTemplateId, int SlotNo);

internal sealed record PickupResult(PickupOutcome Outcome, InventoryItem? Item, int InventoryVersion)
{
    public static PickupResult Refused(PickupOutcome outcome) => new(outcome, null, 0);
}

internal sealed record WalletEntry(string Currency, long Balance);

internal sealed record CharacterAssets(IReadOnlyList<InventoryItem> Items, IReadOnlyList<WalletEntry> Wallet, int InventoryCapacity, int InventoryVersion);

/// <summary>
/// Persistent assets (design document 9-11). Each public method is one transaction; the decisions
/// are made by conditional statements and constraints inside it, so two zones, two characters or
/// two retries of the same request can never both win.
/// </summary>
internal sealed class EconomyRepository(NpgsqlDataSource dataSource, ContentCatalog content)
{
    private const string PickupScope = "pickup";
    private const string PendingOutcome = "pending";
    private const int FirstSlot = 0;

    // ---- authority ------------------------------------------------------------------------

    // FOR SHARE: a concurrent redeem (which takes FOR UPDATE on the same row) waits until this grant
    // has committed, so a hand-over to another zone can never interleave with a grant under the old one.
    private const string OwnershipSql = """
        SELECT zone_id AS "ZoneId", zone_instance_id AS "ZoneInstanceId", owner_epoch AS "OwnerEpoch", state AS "State"
        FROM game.character_ownership
        WHERE world_id = @WorldId AND character_id = @CharacterId
        FOR SHARE
        """;

    /// <summary>The zone id the character is simulated in, or null when the caller is not its current owner.</summary>
    private static async Task<int?> OwnedZoneAsync(NpgsqlConnection connection, NpgsqlTransaction tx, WorldId worldId, CharacterId characterId, ZoneAuthority authority, CancellationToken ct)
    {
        OwnershipRow? row = await connection.QuerySingleOrDefaultAsync<OwnershipRow>(Cmd(OwnershipSql, new { WorldId = worldId, CharacterId = characterId }, tx, ct));
        bool holds = row is { State: "active" } && row.OwnerEpoch == authority.OwnerEpoch && authority.ZoneInstanceId.SameAs(row.ZoneInstanceId);
        return holds ? row!.ZoneId : null;
    }

    // ---- kills ----------------------------------------------------------------------------

    private const string InsertReceiptSql = """
        INSERT INTO game.kill_receipts
            (kill_id, world_id, zone_instance_id, owner_epoch, killer_character_id, monster_template_id, content_version, gold_granted)
        VALUES (@KillId, @WorldId, @ZoneInstanceId, @OwnerEpoch, @Killer, @MonsterTemplateId, @ContentVersion, @Gold)
        ON CONFLICT (kill_id) DO NOTHING
        """;

    private const string InsertGroundItemSql = """
        INSERT INTO game.item_instances (item_instance_id, world_id, item_template_id, location, zone_id, pos_x, pos_y, origin_kill_id)
        VALUES (@Id, @WorldId, @ItemTemplateId, 'ground', @ZoneId, @X, @Y, @KillId)
        """;

    private const string GroundItemsOfKillSql = """
        SELECT item_instance_id AS "Id", item_template_id AS "ItemTemplateId", pos_x AS "X", pos_y AS "Y"
        FROM game.item_instances
        WHERE origin_kill_id = @KillId AND location = 'ground'
        ORDER BY created_at, item_instance_id
        """;

    private const string CreditWalletSql = """
        INSERT INTO game.wallets (world_id, character_id, currency, balance)
        VALUES (@WorldId, @CharacterId, @Currency, @Amount)
        ON CONFLICT (world_id, character_id, currency) DO UPDATE
            SET balance = game.wallets.balance + EXCLUDED.balance,
                version = game.wallets.version + 1
        RETURNING balance
        """;

    private const string BalanceSql = """
        SELECT balance FROM game.wallets WHERE world_id = @WorldId AND character_id = @CharacterId AND currency = @Currency
        """;

    private const string LedgerSql = """
        INSERT INTO game.economy_ledger (world_id, character_id, kind, item_instance_id, currency, amount, reason, request_id)
        VALUES (@WorldId, @CharacterId, @Kind, @ItemInstanceId, @Currency, @Amount, @Reason, @RequestId)
        """;

    public async Task<KillResult> RecordKillAsync(RecordKillCommand command, CancellationToken ct)
    {
        if (!string.Equals(command.ContentVersion, content.Version, StringComparison.Ordinal))
        {
            return KillResult.Refused(KillOutcome.ContentVersionMismatch);
        }

        // The zone rolls, the Backend checks the roll against the same table: a zone cannot invent drops.
        DropTable? table = content.DropTableFor(command.MonsterTemplateId);
        if (table is null || command.Drops.Any(drop => !table.Allows(drop.ItemTemplateId)))
        {
            return KillResult.Refused(KillOutcome.UnknownTemplate);
        }

        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        await using NpgsqlTransaction tx = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, ct);

        int? zoneId = await OwnedZoneAsync(connection, tx, command.WorldId, command.Killer, command.Authority, ct);
        if (zoneId is null)
        {
            return KillResult.Refused(KillOutcome.StaleAuthority);
        }

        var killScope = new { KillId = command.KillId.Bytes, command.WorldId, CharacterId = command.Killer, Currency = content.PrimaryCurrency };
        int inserted = await connection.ExecuteAsync(Cmd(InsertReceiptSql, new
        {
            KillId = command.KillId.Bytes,
            command.WorldId,
            ZoneInstanceId = command.Authority.ZoneInstanceId.Bytes,
            command.Authority.OwnerEpoch,
            command.Killer,
            command.MonsterTemplateId,
            command.ContentVersion,
            table.Gold,
        }, tx, ct));
        if (inserted == 0)
        {
            // Same kill seen before (a retry after a lost response): echo what is still on the ground, grant nothing.
            IReadOnlyList<DroppedItem> existing = (await connection.QueryAsync<DroppedItemRow>(Cmd(GroundItemsOfKillSql, killScope, tx, ct)))
                .Select(r => r.ToItem()).ToList();
            long balance = await connection.ExecuteScalarAsync<long?>(Cmd(BalanceSql, killScope, tx, ct)) ?? 0;
            await tx.CommitAsync(ct);
            return new KillResult(KillOutcome.AlreadyRecorded, existing, 0, balance);
        }

        List<DroppedItem> items = command.Drops.Select(drop => new DroppedItem(ItemInstanceId.New(), drop.ItemTemplateId, drop.X, drop.Y)).ToList();
        foreach (DroppedItem item in items)
        {
            await connection.ExecuteAsync(Cmd(InsertGroundItemSql, new
            {
                Id = item.Id.Bytes,
                command.WorldId,
                item.ItemTemplateId,
                ZoneId = zoneId.Value,
                item.X,
                item.Y,
                KillId = command.KillId.Bytes,
            }, tx, ct));
        }

        long newBalance = await connection.ExecuteScalarAsync<long>(Cmd(CreditWalletSql, new { command.WorldId, CharacterId = command.Killer, Currency = content.PrimaryCurrency, Amount = table.Gold }, tx, ct));
        if (table.Gold > 0)
        {
            await connection.ExecuteAsync(Cmd(LedgerSql, new
            {
                command.WorldId,
                CharacterId = command.Killer,
                Kind = "currency_credit",
                ItemInstanceId = (byte[]?)null,
                Currency = content.PrimaryCurrency,
                Amount = (long?)table.Gold,
                Reason = "kill",
                RequestId = command.KillId.Bytes,
            }, tx, ct));
        }

        await tx.CommitAsync(ct);
        return new KillResult(KillOutcome.Recorded, items, table.Gold, newBalance);
    }

    // ---- pickups --------------------------------------------------------------------------

    // Claiming the request id is the first write: a concurrent duplicate blocks on the conflict until
    // this transaction ends, then reads the stored outcome instead of acting again.
    private const string ClaimRequestSql = """
        INSERT INTO game.command_results (world_id, scope, request_id, request_hash, outcome)
        VALUES (@WorldId, @Scope, @RequestId, @RequestHash, @Outcome)
        ON CONFLICT (world_id, scope, request_id) DO NOTHING
        """;

    private const string StoredRequestSql = """
        SELECT request_hash AS "RequestHash", outcome AS "Outcome", payload::text AS "Payload"
        FROM game.command_results
        WHERE world_id = @WorldId AND scope = @Scope AND request_id = @RequestId
        """;

    private const string StoreOutcomeSql = """
        UPDATE game.command_results
        SET outcome = @Outcome, payload = CAST(@Payload AS jsonb)
        WHERE world_id = @WorldId AND scope = @Scope AND request_id = @RequestId
        """;

    private const string EnsureInventorySql = """
        INSERT INTO game.inventories (world_id, character_id, capacity, version)
        VALUES (@WorldId, @CharacterId, @Capacity, 0)
        ON CONFLICT (world_id, character_id) DO NOTHING
        """;

    // FOR UPDATE serialises pickups of one character, which makes the free-slot choice below safe;
    // the partial unique index item_instances_one_per_slot guards it even if that reasoning is ever wrong.
    private const string LockInventorySql = """
        SELECT capacity AS "Capacity", version AS "Version"
        FROM game.inventories WHERE world_id = @WorldId AND character_id = @CharacterId
        FOR UPDATE
        """;

    private const string OccupiedSlotsSql = """
        SELECT slot_no FROM game.item_instances
        WHERE world_id = @WorldId AND owner_character_id = @CharacterId AND location = 'inventory'
        ORDER BY slot_no
        """;

    // Conditional UPDATE (10.2): only an item still on the ground of the owning zone moves. Of two
    // characters reaching for the same item, the second sees zero rows.
    private const string TakeItemSql = """
        UPDATE game.item_instances
        SET location = 'inventory', owner_character_id = @CharacterId, slot_no = @SlotNo,
            zone_id = NULL, pos_x = NULL, pos_y = NULL, version = version + 1, updated_at = now()
        WHERE item_instance_id = @ItemInstanceId AND world_id = @WorldId AND location = 'ground' AND zone_id = @ZoneId
        RETURNING item_template_id
        """;

    private const string ExplainItemSql = """
        SELECT location AS "Location", zone_id AS "ZoneId"
        FROM game.item_instances WHERE item_instance_id = @ItemInstanceId AND world_id = @WorldId
        """;

    private const string BumpInventorySql = """
        UPDATE game.inventories SET version = version + 1
        WHERE world_id = @WorldId AND character_id = @CharacterId
        RETURNING version
        """;

    public async Task<PickupResult> PickupAsync(PickupCommand command, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        await using NpgsqlTransaction tx = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, ct);

        int? zoneId = await OwnedZoneAsync(connection, tx, command.WorldId, command.CharacterId, command.Authority, ct);
        if (zoneId is null)
        {
            return PickupResult.Refused(PickupOutcome.StaleAuthority);
        }

        var requestKey = new RequestKey(command.WorldId, PickupScope, command.RequestId.Bytes);
        byte[] hash = command.BodyHash();
        int claimed = await connection.ExecuteAsync(Cmd(ClaimRequestSql, new { requestKey.WorldId, requestKey.Scope, requestKey.RequestId, RequestHash = hash, Outcome = PendingOutcome }, tx, ct));
        if (claimed == 0)
        {
            StoredRequestRow stored = await connection.QuerySingleAsync<StoredRequestRow>(Cmd(StoredRequestSql, requestKey, tx, ct));
            await tx.CommitAsync(ct);
            return stored.RequestHash.AsSpan().SequenceEqual(hash)
                ? StoredPickup.Replay(stored, command.ItemInstanceId)
                : PickupResult.Refused(PickupOutcome.RequestMismatch);
        }

        var inventoryKey = new { command.WorldId, command.CharacterId };
        await connection.ExecuteAsync(Cmd(EnsureInventorySql, new { command.WorldId, command.CharacterId, Capacity = content.InventoryCapacity }, tx, ct));
        InventoryRow inventory = await connection.QuerySingleAsync<InventoryRow>(Cmd(LockInventorySql, inventoryKey, tx, ct));
        List<int> occupied = (await connection.QueryAsync<int>(Cmd(OccupiedSlotsSql, inventoryKey, tx, ct))).ToList();
        if (occupied.Count >= inventory.Capacity)
        {
            return await ConcludeAsync(connection, tx, requestKey, new PickupResult(PickupOutcome.InventoryFull, null, inventory.Version), ct);
        }

        int slot = FirstFreeSlot(occupied);
        int? templateId = await connection.ExecuteScalarAsync<int?>(Cmd(TakeItemSql, new
        {
            ItemInstanceId = command.ItemInstanceId.Bytes,
            command.WorldId,
            command.CharacterId,
            SlotNo = slot,
            ZoneId = zoneId.Value,
        }, tx, ct));
        if (templateId is null)
        {
            ItemWhereabouts? whereabouts = await connection.QuerySingleOrDefaultAsync<ItemWhereabouts>(Cmd(ExplainItemSql, new { ItemInstanceId = command.ItemInstanceId.Bytes, command.WorldId }, tx, ct));
            // Moved into an inventory: somebody committed first. Unknown, or lying in another zone: this zone never saw it.
            PickupOutcome outcome = whereabouts is { Location: "inventory" } ? PickupOutcome.AlreadyTaken : PickupOutcome.UnknownItem;
            return await ConcludeAsync(connection, tx, requestKey, new PickupResult(outcome, null, inventory.Version), ct);
        }

        int version = await connection.ExecuteScalarAsync<int>(Cmd(BumpInventorySql, inventoryKey, tx, ct));
        await connection.ExecuteAsync(Cmd(LedgerSql, new
        {
            command.WorldId,
            command.CharacterId,
            Kind = "item_acquired",
            ItemInstanceId = command.ItemInstanceId.Bytes,
            Currency = (string?)null,
            Amount = (long?)null,
            Reason = "pickup",
            RequestId = command.RequestId.Bytes,
        }, tx, ct));
        var acquired = new PickupResult(PickupOutcome.Acquired, new InventoryItem(command.ItemInstanceId, templateId.Value, slot), version);
        return await ConcludeAsync(connection, tx, requestKey, acquired, ct);
    }

    /// <summary>Stores the outcome under the claimed request id and commits; the stored row is what a retry gets back.</summary>
    private static async Task<PickupResult> ConcludeAsync(NpgsqlConnection connection, NpgsqlTransaction tx, RequestKey requestKey, PickupResult result, CancellationToken ct)
    {
        await connection.ExecuteAsync(Cmd(StoreOutcomeSql, new
        {
            Outcome = result.Outcome.ToString(),
            Payload = StoredPickup.Serialize(result),
            requestKey.WorldId,
            requestKey.Scope,
            requestKey.RequestId,
        }, tx, ct));
        await tx.CommitAsync(ct);
        return result;
    }

    private static int FirstFreeSlot(IReadOnlyList<int> occupiedAscending)
    {
        int slot = FirstSlot;
        foreach (int taken in occupiedAscending)
        {
            if (taken != slot)
            {
                break;
            }

            slot++;
        }

        return slot;
    }

    // ---- assets ---------------------------------------------------------------------------

    private const string InventoryItemsSql = """
        SELECT item_instance_id AS "Id", item_template_id AS "ItemTemplateId", slot_no AS "SlotNo"
        FROM game.item_instances
        WHERE world_id = @WorldId AND owner_character_id = @CharacterId AND location = 'inventory'
        ORDER BY slot_no
        """;

    private const string WalletSql = """
        SELECT currency AS "Currency", balance AS "Balance" FROM game.wallets
        WHERE world_id = @WorldId AND character_id = @CharacterId ORDER BY currency
        """;

    private const string InventorySql = """
        SELECT capacity AS "Capacity", version AS "Version" FROM game.inventories WHERE world_id = @WorldId AND character_id = @CharacterId
        """;

    public async Task<CharacterAssets> GetAssetsAsync(WorldId worldId, CharacterId characterId, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        var key = new { WorldId = worldId, CharacterId = characterId };
        List<InventoryItem> items = (await connection.QueryAsync<InventoryItemRow>(new CommandDefinition(InventoryItemsSql, key, cancellationToken: ct))).Select(r => r.ToItem()).ToList();
        List<WalletEntry> wallet = (await connection.QueryAsync<WalletEntry>(new CommandDefinition(WalletSql, key, cancellationToken: ct))).ToList();
        InventoryRow inventory = await connection.QuerySingleOrDefaultAsync<InventoryRow>(new CommandDefinition(InventorySql, key, cancellationToken: ct))
                                 ?? new InventoryRow(content.InventoryCapacity, 0);
        return new CharacterAssets(items, wallet, inventory.Capacity, inventory.Version);
    }

    private static CommandDefinition Cmd(string sql, object parameters, NpgsqlTransaction tx, CancellationToken ct)
        => new(sql, parameters, tx, cancellationToken: ct);

    private sealed record RequestKey(WorldId WorldId, string Scope, byte[] RequestId);

    private sealed record OwnershipRow(int ZoneId, byte[] ZoneInstanceId, long OwnerEpoch, string State);

    private sealed record ItemWhereabouts(string Location, int? ZoneId);

    private sealed record DroppedItemRow(byte[] Id, int ItemTemplateId, float X, float Y)
    {
        public DroppedItem ToItem() => new(new ItemInstanceId(Id), ItemTemplateId, X, Y);
    }

    private sealed record InventoryItemRow(byte[] Id, int ItemTemplateId, int SlotNo)
    {
        public InventoryItem ToItem() => new(new ItemInstanceId(Id), ItemTemplateId, SlotNo);
    }

    private sealed record InventoryRow(int Capacity, int Version);

    private sealed record StoredRequestRow(byte[] RequestHash, string Outcome, string Payload);

    /// <summary>The jsonb payload of a stored pickup: enough to rebuild the response for a retry.</summary>
    private sealed record StoredPickup(int? ItemTemplateId, int? SlotNo, int InventoryVersion)
    {
        private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

        public static string Serialize(PickupResult result)
            => JsonSerializer.Serialize(new StoredPickup(result.Item?.ItemTemplateId, result.Item?.SlotNo, result.InventoryVersion), Json);

        public static PickupResult Replay(StoredRequestRow stored, ItemInstanceId itemId)
        {
            var outcome = Enum.Parse<PickupOutcome>(stored.Outcome);
            StoredPickup payload = JsonSerializer.Deserialize<StoredPickup>(stored.Payload, Json)
                                   ?? throw new InvalidDataException("stored pickup payload is null");
            InventoryItem? item = payload is { ItemTemplateId: { } template, SlotNo: { } slot } ? new InventoryItem(itemId, template, slot) : null;
            return new PickupResult(outcome, item, payload.InventoryVersion);
        }
    }
}

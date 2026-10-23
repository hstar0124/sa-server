using Dapper;
using Google.Protobuf;
using Sa.Events.V1;
using Sa.GameBackend.Common;
using Sa.GameBackend.Infrastructure.GameData;
using Sa.GameBackend.Infrastructure.Outbox;

namespace Sa.GameBackend.Modules.Trading;

/// <summary>
/// The exchange itself, run inside the acceptance that completes the pair (ADR-0013). Every step
/// writes into the caller's transaction under a savepoint the caller holds: a refusal anywhere rolls
/// all of it back, and the trade stays open with its acceptances cleared.
/// </summary>
internal static class TradeSettlement
{
    private const int FirstSlot = 0;
    private const string TradeReason = "trade";

    // Matches the column default and the Economy module: an inventory nobody has changed yet.
    private const int InitialInventoryVersion = 0;

    private const string EnsureInventorySql = """
        INSERT INTO game.inventories (world_id, character_id, capacity, version)
        VALUES (@WorldId, @CharacterId, @Capacity, @InitialVersion)
        ON CONFLICT (world_id, character_id) DO NOTHING
        """;

    // The same lock a pickup takes: while the trade holds it, nothing else changes these slots.
    private const string LockInventorySql = """
        SELECT capacity FROM game.inventories WHERE world_id = @WorldId AND character_id = @CharacterId FOR UPDATE
        """;

    private const string OccupiedSlotsSql = """
        SELECT slot_no FROM game.item_instances
        WHERE world_id = @WorldId AND owner_character_id = @CharacterId AND location = 'inventory'
        ORDER BY slot_no
        """;

    // Conditional: the balance must cover the amount. The CHECK on the balance would refuse it too.
    private const string DebitSql = """
        UPDATE game.wallets
        SET balance = balance - @Amount, version = version + 1
        WHERE world_id = @WorldId AND character_id = @CharacterId AND currency = @Currency AND balance >= @Amount
        """;

    private const string CreditSql = """
        INSERT INTO game.wallets (world_id, character_id, currency, balance)
        VALUES (@WorldId, @CharacterId, @Currency, @Amount)
        ON CONFLICT (world_id, character_id, currency) DO UPDATE
            SET balance = game.wallets.balance + EXCLUDED.balance,
                version = game.wallets.version + 1
        """;

    // Conditional: only an item still in the giver's inventory changes owner.
    private const string HandOverItemSql = """
        UPDATE game.item_instances
        SET owner_character_id = @To, slot_no = @SlotNo, version = version + 1, updated_at = now()
        WHERE item_instance_id = @ItemInstanceId AND world_id = @WorldId AND owner_character_id = @From AND location = 'inventory'
        """;

    private const string SeatItemSql = """
        UPDATE game.item_instances SET slot_no = @SlotNo
        WHERE item_instance_id = @ItemInstanceId AND world_id = @WorldId AND owner_character_id = @Owner AND location = 'inventory'
        """;

    private const string BumpInventorySql = """
        UPDATE game.inventories SET version = version + 1
        WHERE world_id = @WorldId AND character_id = @CharacterId
        RETURNING version
        """;

    private const string LedgerSql = """
        INSERT INTO game.economy_ledger (world_id, character_id, kind, item_instance_id, currency, amount, reason, request_id)
        VALUES (@WorldId, @CharacterId, @Kind, @ItemInstanceId, @Currency, @Amount, @Reason, @RequestId)
        """;

    /// <summary>Settles the trade both sides accepted, or says why not. Closing the trade is left to the caller.</summary>
    public static async Task<TradeOutcome> SettleAsync(TradeTx t, TradeRow trade, AcceptTradeCommand command, ContentCatalog content)
    {
        // The caller's own ownership was checked (and is held) at the start of the call. The partner
        // must stand in the same zone instance, or the zone's proximity check meant nothing.
        CharacterId partner = trade.PartnerOf(command.CharacterId);
        CharacterOwner? partnerOwner = await CharacterOwners.ReadForShareAsync(t.Connection, t.Tx, t.WorldId, partner, t.Ct);
        if (partnerOwner is null || !partnerOwner.IsSimulatedBy(command.Authority.ZoneInstanceId))
        {
            return TradeOutcome.CounterpartyNotHere;
        }

        // Both inventories are locked in id order, whichever side is accepting, so two settlements
        // (or a settlement and a pickup) never wait on each other in a cycle.
        var sides = new List<Side>();
        foreach (CharacterId character in new[] { trade.Initiator, trade.Counterparty }.OrderBy(c => c.Value))
        {
            sides.Add(await LockSideAsync(t, character, trade.GoldOf(character), content));
        }

        IReadOnlyList<TradeItemRow> items = await TradeRecords.ItemsAsync(t, trade.Id);
        if (items.Any(i => i.Owner != i.OfferedBy || !string.Equals(i.Location, "inventory", StringComparison.Ordinal)))
        {
            return TradeOutcome.ItemUnavailable;
        }

        if (sides.Any(side => side.FreeSlotsAfterGiving(items).Count < side.Received(items).Count()))
        {
            return TradeOutcome.InventoryFull;
        }

        if (await MoveGoldAsync(t, trade, command, sides, content) is { } goldRefusal)
        {
            return goldRefusal;
        }

        if (await MoveItemsAsync(t, command, sides, items) is { } itemRefusal)
        {
            return itemRefusal;
        }

        foreach (Side side in sides)
        {
            int version = await t.Connection.ExecuteScalarAsync<int>(t.Cmd(BumpInventorySql, new { t.WorldId, side.CharacterId }));
            await OutboxWriter.AppendAsync(t.Connection, t.Tx, new OutboxEvent(t.WorldId, EventAggregate.Character(side.CharacterId),
                CompletedEvent(trade, side, items, version), command.RequestId.Bytes), t.Ct);
        }

        return TradeOutcome.Settled;
    }

    private static async Task<Side> LockSideAsync(TradeTx t, CharacterId character, long goldGiven, ContentCatalog content)
    {
        var key = new { t.WorldId, CharacterId = character };
        await t.Connection.ExecuteAsync(t.Cmd(EnsureInventorySql,
            new { t.WorldId, CharacterId = character, Capacity = content.InventoryCapacity, InitialVersion = InitialInventoryVersion }));
        int capacity = await t.Connection.ExecuteScalarAsync<int>(t.Cmd(LockInventorySql, key));
        List<int> occupied = (await t.Connection.QueryAsync<int>(t.Cmd(OccupiedSlotsSql, key))).ToList();
        return new Side(character, capacity, occupied, goldGiven);
    }

    /// <summary>
    /// Every payer's conditional debit first, then the credits, ledgered on both sides: each side's
    /// offer is covered by its own balance, never by the gold it is about to receive.
    /// </summary>
    private static async Task<TradeOutcome?> MoveGoldAsync(TradeTx t, TradeRow trade, AcceptTradeCommand command, IReadOnlyList<Side> sides, ContentCatalog content)
    {
        List<Side> payers = sides.Where(s => s.GoldGiven > 0).ToList();
        foreach (Side payer in payers)
        {
            var debit = new { t.WorldId, payer.CharacterId, Currency = content.PrimaryCurrency, Amount = payer.GoldGiven };
            if (await t.Connection.ExecuteAsync(t.Cmd(DebitSql, debit)) == 0)
            {
                return TradeOutcome.InsufficientGold;
            }

            await LedgerAsync(t, command, payer.CharacterId, "currency_debit", null, content.PrimaryCurrency, payer.GoldGiven);
        }

        foreach (Side payer in payers)
        {
            CharacterId payee = trade.PartnerOf(payer.CharacterId);
            await t.Connection.ExecuteAsync(t.Cmd(CreditSql, new { t.WorldId, CharacterId = payee, Currency = content.PrimaryCurrency, Amount = payer.GoldGiven }));
            await LedgerAsync(t, command, payee, "currency_credit", null, content.PrimaryCurrency, payer.GoldGiven);
        }

        return null;
    }

    /// <summary>
    /// Items change hands in two passes. First every item moves to its receiver under a parking slot
    /// at or beyond the receiver's capacity, which no seated item holds, so the one-item-per-slot index
    /// never sees two items in a slot while the slots being vacated still hold their old items. Then
    /// each receiver's items take its lowest free slots, counting the ones its own gifts just freed.
    /// </summary>
    private static async Task<TradeOutcome?> MoveItemsAsync(TradeTx t, AcceptTradeCommand command, IReadOnlyList<Side> sides, IReadOnlyList<TradeItemRow> items)
    {
        foreach (Side receiver in sides)
        {
            int parking = receiver.FirstParkingSlot;
            foreach (TradeItemRow item in receiver.Received(items))
            {
                int moved = await t.Connection.ExecuteAsync(t.Cmd(HandOverItemSql,
                    new { ItemInstanceId = item.Id, t.WorldId, From = item.OfferedBy, To = receiver.CharacterId, SlotNo = parking++ }));
                if (moved != 1)
                {
                    return TradeOutcome.ItemUnavailable;
                }

                await LedgerAsync(t, command, receiver.CharacterId, "item_transferred", item.Id, null, null);
            }
        }

        foreach (Side receiver in sides)
        {
            SortedSet<int> free = receiver.FreeSlotsAfterGiving(items);
            foreach (TradeItemRow item in receiver.Received(items))
            {
                int slot = free.Min;
                free.Remove(slot);
                await t.Connection.ExecuteAsync(t.Cmd(SeatItemSql, new { ItemInstanceId = item.Id, t.WorldId, Owner = receiver.CharacterId, SlotNo = slot }));
            }
        }

        return null;
    }

    private static async Task LedgerAsync(TradeTx t, AcceptTradeCommand command, CharacterId character, string kind, byte[]? item, string? currency, long? amount)
        => await t.Connection.ExecuteAsync(t.Cmd(LedgerSql, new
        {
            t.WorldId,
            CharacterId = character,
            Kind = kind,
            ItemInstanceId = item,
            Currency = currency,
            Amount = amount,
            Reason = TradeReason,
            RequestId = command.RequestId.Bytes,
        }));

    private static TradeCompleted CompletedEvent(TradeRow trade, Side side, IReadOnlyList<TradeItemRow> items, int inventoryVersion)
    {
        CharacterId partner = trade.PartnerOf(side.CharacterId);
        var completed = new TradeCompleted
        {
            TradeId = ByteString.CopyFrom(trade.IdBytes),
            CharacterId = (ulong)side.CharacterId.Value,
            CounterpartyId = (ulong)partner.Value,
            GoldGiven = (ulong)side.GoldGiven,
            GoldReceived = (ulong)trade.GoldOf(partner),
            InventoryVersion = (uint)inventoryVersion,
        };
        completed.ItemsGiven.AddRange(items.Where(i => i.OfferedBy == side.CharacterId).Select(ToRef));
        completed.ItemsReceived.AddRange(side.Received(items).Select(ToRef));
        return completed;
    }

    private static DroppedItemRef ToRef(TradeItemRow item) => new()
    {
        ItemInstanceId = ByteString.CopyFrom(item.Id),
        ItemTemplateId = (uint)item.ItemTemplateId.Value,
    };

    /// <summary>One party with its inventory locked: capacity, the slots it holds now, and the gold it gives.</summary>
    private sealed record Side(CharacterId CharacterId, int Capacity, IReadOnlyList<int> Occupied, long GoldGiven)
    {
        /// <summary>Beyond every valid slot and beyond anything the inventory holds, even if its capacity was lowered under it.</summary>
        public int FirstParkingSlot => Occupied.Count == 0 ? Capacity : Math.Max(Capacity, Occupied.Max() + 1);

        public IEnumerable<TradeItemRow> Received(IEnumerable<TradeItemRow> items) => items.Where(i => i.OfferedBy != CharacterId);

        /// <summary>The valid slots that are empty once this side's own offered items have left.</summary>
        public SortedSet<int> FreeSlotsAfterGiving(IEnumerable<TradeItemRow> items)
        {
            var given = items.Where(i => i.OfferedBy == CharacterId && i.SlotNo is not null).Select(i => i.SlotNo!.Value).ToHashSet();
            var free = new SortedSet<int>(Enumerable.Range(FirstSlot, Capacity));
            free.ExceptWith(Occupied.Where(slot => !given.Contains(slot)));
            return free;
        }
    }
}

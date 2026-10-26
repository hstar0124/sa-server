using Google.Protobuf;
using Sa.Backend.V1;
using Sa.Events.V1;
using Sa.GameBackend.IntegrationTests.Economy;
using Sa.GameBackend.IntegrationTests.Harness;
using static Sa.GameBackend.IntegrationTests.Trading.TradeWorld;

namespace Sa.GameBackend.IntegrationTests.Trading;

/// <summary>
/// tests/integration/README.md "거래 확정" and ADR-0013: both sides' assets are preserved, a trade
/// settles once, and a settlement that cannot complete leaves nothing behind.
/// </summary>
[Collection(SharedPostgres.Name)]
public sealed class TradeSettlementTests(PostgresFixture postgres) : IDisposable
{
    // Every test enters a few characters, each registering and logging in from the same address.
    private readonly BackendFactory _factory = new(postgres.ConnectionString, new BackendSettings(CredentialAttemptsPerMinute: 60));

    [Fact]
    public async Task Items_and_gold_change_hands_once_when_both_accept_the_same_version()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        IReadOnlyList<ByteString> aItems = await world.StockAsync(a, SlimeGel, RustySword);
        IReadOnlyList<ByteString> bItems = await world.StockAsync(b, SlimeGel);
        ulong aGold = await world.GoldOfAsync(a);
        ulong bGold = await world.GoldOfAsync(b);
        Assert.True(aGold > 0 && bGold > 0, "the slime's drop table grants gold");

        TradeResponse opened = await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId));
        Assert.Equal(TradeOutcome.Applied, opened.Outcome);
        ByteString tradeId = opened.Trade.TradeId;

        // A gives both items and some gold; B gives one item. Each offer bumps the version.
        TradeResponse aOffer = await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, aGold / 2, aItems[0], aItems[1]));
        TradeResponse bOffer = await world.Trade.OfferAsync(TradeClient.Offer(World, b, tradeId, 0, bItems[0]));
        Assert.Equal(TradeOutcome.Applied, aOffer.Outcome);
        Assert.Equal(TradeOutcome.Applied, bOffer.Outcome);
        Assert.Equal(2u, bOffer.Trade.Version);
        Assert.Equal(2, bOffer.Trade.Initiator.Items.Count);
        Assert.Single(bOffer.Trade.Counterparty.Items);

        TradeResponse aAccept = await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 2));
        Assert.Equal(TradeOutcome.Applied, aAccept.Outcome);
        Assert.True(aAccept.Trade.Initiator.Accepted);
        Assert.False(aAccept.Trade.Counterparty.Accepted);
        byte[] settlingRequest = EconomyClient.NewId();
        TradeResponse bAccept = await world.Trade.AcceptAsync(TradeClient.Accept(World, b, tradeId, 2, settlingRequest));
        Assert.Equal(TradeOutcome.Settled, bAccept.Outcome);
        Assert.Equal(TradeStatus.Completed, bAccept.Trade.Status);
        Assert.Equal(2, bAccept.Trade.Initiator.Items.Count);  // the settled trade reports what changed hands

        // Assets moved exactly once and are conserved; the received items sit in the lowest free slots.
        GetCharacterAssetsResponse aAssets = await world.AssetsAsync(a);
        GetCharacterAssetsResponse bAssets = await world.AssetsAsync(b);
        Assert.Equal(new[] { bItems[0] }, aAssets.Items.Select(i => i.ItemInstanceId));
        Assert.Equal(aItems.OrderBy(i => i.ToBase64()), bAssets.Items.Select(i => i.ItemInstanceId).OrderBy(i => i.ToBase64()));
        Assert.Equal(new uint[] { 0 }, aAssets.Items.Select(i => i.SlotNo));
        Assert.Equal(new uint[] { 0, 1 }, bAssets.Items.Select(i => i.SlotNo));
        Assert.Equal(aGold - (aGold / 2), await world.GoldOfAsync(a));
        Assert.Equal(bGold + (aGold / 2), await world.GoldOfAsync(b));

        // The row is closed and the locks are released.
        object trade = new { id = tradeId.ToByteArray() };
        Assert.Equal(0L, await Db.ScalarAsync<long>(postgres.ConnectionString, "SELECT count(*) FROM game.trade_participants WHERE trade_id = @id", trade));
        Assert.Equal(0L, await Db.ScalarAsync<long>(postgres.ConnectionString, "SELECT count(*) FROM game.trade_items WHERE trade_id = @id", trade));
        Assert.Equal("completed", await Db.ScalarAsync<string>(postgres.ConnectionString, "SELECT status FROM game.trades WHERE trade_id = @id", trade));

        // Ledger: three item transfers and one gold debit and credit, all under the settling request.
        IReadOnlyList<string> ledger = await Db.QueryAsync<string>(postgres.ConnectionString,
            "SELECT kind FROM game.economy_ledger WHERE request_id = @req AND reason = 'trade' ORDER BY kind", new { req = settlingRequest });
        Assert.Equal(["currency_credit", "currency_debit", "item_transferred", "item_transferred", "item_transferred"], ledger);

        // Each stream ends with its own TradeCompleted, numbered after the kill and pickups before it.
        Db.OutboxRow aEvent = (await Db.OutboxOfCharacterAsync(postgres.ConnectionString, World, a.CharacterId))[^1];
        Assert.Equal(TradeCompleted.Descriptor.Name, aEvent.EventType);
        Assert.Equal(4, aEvent.AggregateSeq);
        Assert.Equal(settlingRequest, aEvent.RequestId);
        TradeCompleted aCompleted = TradeCompleted.Parser.ParseFrom(aEvent.Payload);
        Assert.Equal((ulong)b.CharacterId, aCompleted.CounterpartyId);
        Assert.Equal(aGold / 2, aCompleted.GoldGiven);
        Assert.Equal(0ul, aCompleted.GoldReceived);
        Assert.Equal(2, aCompleted.ItemsGiven.Count);
        Assert.Equal(bItems[0], aCompleted.ItemsReceived.Single().ItemInstanceId);
        Assert.Equal(aAssets.InventoryVersion, aCompleted.InventoryVersion);
        Db.OutboxRow bEvent = (await Db.OutboxOfCharacterAsync(postgres.ConnectionString, World, b.CharacterId))[^1];
        Assert.Equal(TradeCompleted.Descriptor.Name, bEvent.EventType);
        Assert.Equal(aGold / 2, TradeCompleted.Parser.ParseFrom(bEvent.Payload).GoldReceived);

        // The settling request replayed returns the same answer and moves nothing again.
        TradeResponse replay = await world.Trade.AcceptAsync(TradeClient.Accept(World, b, tradeId, 2, settlingRequest));
        Assert.Equal(bAccept, replay);
        Assert.Equal(3, (await world.AssetsAsync(a)).Items.Count + (await world.AssetsAsync(b)).Items.Count);
        Assert.Equal(5L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.economy_ledger WHERE request_id = @req", new { req = settlingRequest }));
    }

    [Fact]
    public async Task Simultaneous_acceptances_settle_exactly_once()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        IReadOnlyList<ByteString> aItems = await world.StockAsync(a, SlimeGel);
        ByteString tradeId = (await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId))).Trade.TradeId;
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, aItems[0]))).Outcome);

        TradeResponse[] accepts = await Task.WhenAll(
            world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 1)),
            world.Trade.AcceptAsync(TradeClient.Accept(World, b, tradeId, 1)));

        Assert.Equal(1, accepts.Count(r => r.Outcome == TradeOutcome.Settled));
        Assert.Equal(1, accepts.Count(r => r.Outcome == TradeOutcome.Applied));
        Assert.Equal(1L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.item_instances WHERE item_instance_id = @id AND owner_character_id = @b",
            new { id = aItems[0].ToByteArray(), b = b.CharacterId }));
        Assert.Equal(2L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.outbox_events WHERE event_type = @type AND aggregate_id = ANY(@ids)",
            new { type = TradeCompleted.Descriptor.Name, ids = new[] { a.CharacterId, b.CharacterId } }));
    }

    [Fact]
    public async Task Two_full_inventories_swap_one_item_for_one_through_parking_slots()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        await world.FillInventoryAsync(a);
        await world.FillInventoryAsync(b);
        Sa.Backend.V1.InventoryItem aGives = (await world.AssetsAsync(a)).Items[3];
        Sa.Backend.V1.InventoryItem bGives = (await world.AssetsAsync(b)).Items[7];

        ByteString tradeId = (await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId))).Trade.TradeId;
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, aGives.ItemInstanceId))).Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, b, tradeId, 0, bGives.ItemInstanceId))).Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 2))).Outcome);
        Assert.Equal(TradeOutcome.Settled, (await world.Trade.AcceptAsync(TradeClient.Accept(World, b, tradeId, 2))).Outcome);

        // Still full and dense; each received item took the slot its own gift vacated, none stayed parked.
        GetCharacterAssetsResponse aAssets = await world.AssetsAsync(a);
        GetCharacterAssetsResponse bAssets = await world.AssetsAsync(b);
        IEnumerable<uint> allSlots = Enumerable.Range(0, TestContent.InventoryCapacity).Select(i => (uint)i);
        Assert.Equal(allSlots, aAssets.Items.Select(i => i.SlotNo));
        Assert.Equal(allSlots, bAssets.Items.Select(i => i.SlotNo));
        Assert.Equal(bGives.ItemInstanceId, aAssets.Items.Single(i => i.SlotNo == aGives.SlotNo).ItemInstanceId);
        Assert.Equal(aGives.ItemInstanceId, bAssets.Items.Single(i => i.SlotNo == bGives.SlotNo).ItemInstanceId);
        Assert.Equal(0L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.item_instances WHERE owner_character_id = ANY(@ids) AND slot_no >= @capacity",
            new { ids = new[] { a.CharacterId, b.CharacterId }, capacity = TestContent.InventoryCapacity }));
    }

    [Fact]
    public async Task A_settlement_refused_midway_moves_no_gold_and_no_item()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter first = await world.EnterAsync();
        EnteredCharacter second = await world.EnterAsync();
        Assert.True(first.CharacterId < second.CharacterId, "settlement debits the lower character id first");
        IReadOnlyList<ByteString> firstItems = await world.StockAsync(first, SlimeGel);
        await world.StockAsync(second);
        ulong firstGold = await world.GoldOfAsync(first);
        ulong secondGold = await world.GoldOfAsync(second);
        Assert.True(firstGold > 0 && secondGold > 0, "the slime's drop table grants gold");

        ByteString tradeId = (await world.Trade.OpenAsync(TradeClient.Open(World, first, second.CharacterId))).Trade.TradeId;
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, first, tradeId, firstGold, firstItems[0]))).Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, second, tradeId, secondGold))).Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.AcceptAsync(TradeClient.Accept(World, first, tradeId, 2))).Outcome);

        // The second payer's gold is gone by the time the trade settles: the first debit has already run.
        await Db.ExecuteAsync(postgres.ConnectionString,
            "UPDATE game.wallets SET balance = 0, version = version + 1 WHERE world_id = @w AND character_id = @c", new { w = World, c = second.CharacterId });
        TradeResponse refused = await world.Trade.AcceptAsync(TradeClient.Accept(World, second, tradeId, 2));

        Assert.Equal(TradeOutcome.InsufficientGold, refused.Outcome);
        Assert.Equal(TradeStatus.Open, refused.Trade.Status);
        Assert.False(refused.Trade.Initiator.Accepted || refused.Trade.Counterparty.Accepted, "a failed settlement clears both acceptances");
        Assert.Equal(firstGold, await world.GoldOfAsync(first));
        Assert.Equal(0ul, await world.GoldOfAsync(second));
        Assert.Equal(firstItems, (await world.AssetsAsync(first)).Items.Select(i => i.ItemInstanceId));
        Assert.Equal(0L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.economy_ledger WHERE reason = 'trade' AND character_id = ANY(@ids)",
            new { ids = new[] { first.CharacterId, second.CharacterId } }));
    }

    [Fact]
    public async Task Gold_and_capacity_are_checked_and_a_zone_that_lost_the_character_is_refused()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        IReadOnlyList<ByteString> aItems = await world.StockAsync(a, SlimeGel, RustySword);
        ByteString tradeId = (await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId))).Trade.TradeId;
        ulong aGold = await world.GoldOfAsync(a);

        Assert.Equal(TradeOutcome.InsufficientGold, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, aGold + 1))).Outcome);

        // B can hold one item; A offers two.
        await Db.ExecuteAsync(postgres.ConnectionString,
            "INSERT INTO game.inventories (world_id, character_id, capacity, version) VALUES (@w, @b, 1, 0) ON CONFLICT (world_id, character_id) DO UPDATE SET capacity = 1",
            new { w = World, b = b.CharacterId });
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, aItems[0], aItems[1]))).Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 1))).Outcome);
        TradeResponse full = await world.Trade.AcceptAsync(TradeClient.Accept(World, b, tradeId, 1));
        Assert.Equal(TradeOutcome.InventoryFull, full.Outcome);
        Assert.Equal(TradeStatus.Open, full.Trade.Status);
        Assert.False(full.Trade.Initiator.Accepted, "a failed settlement clears both acceptances");

        // An authority from another epoch is not this character's owner.
        AcceptTradeRequest staleAccept = TradeClient.Accept(World, a, tradeId, 1);
        staleAccept.Authority = new ZoneAuthority(a.Authority) { OwnerEpoch = a.Authority.OwnerEpoch + 1 };
        Assert.Equal(TradeOutcome.StaleAuthority, (await world.Trade.AcceptAsync(staleAccept)).Outcome);
    }

    [Fact]
    public async Task A_counterparty_that_moved_to_another_zone_cannot_settle_or_be_traded_with()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        EnteredCharacter c = await world.EnterAsync();
        IReadOnlyList<ByteString> aItems = await world.StockAsync(a, SlimeGel);
        ByteString tradeId = (await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId))).Trade.TradeId;
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, aItems[0]))).Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.AcceptAsync(TradeClient.Accept(World, b, tradeId, 1))).Outcome);

        // B walks through a portal: another zone instance owns B now.
        Assert.True((await world.Directory.RenewZoneAsync(DirectoryClient.NewInstanceId(), World, OtherZone)).Renewed);
        Assert.Equal(TransferOutcome.Transferred, (await world.Directory.TransferAsync(World, b.CharacterId, b.Authority, OtherZone)).Outcome);

        TradeResponse refused = await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 1));
        Assert.Equal(TradeOutcome.CounterpartyNotHere, refused.Outcome);
        Assert.False(refused.Trade.Counterparty.Accepted, "a failed settlement clears both acceptances");
        Assert.Equal(aItems, (await world.AssetsAsync(a)).Items.Select(i => i.ItemInstanceId));
        Assert.Equal(TradeOutcome.CounterpartyNotHere, (await world.Trade.OpenAsync(TradeClient.Open(World, c, b.CharacterId))).Outcome);
    }

    public void Dispose() => _factory.Dispose();
}

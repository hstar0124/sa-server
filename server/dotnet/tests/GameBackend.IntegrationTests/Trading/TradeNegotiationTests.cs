using Google.Protobuf;
using Sa.Backend.V1;
using Sa.GameBackend.IntegrationTests.Harness;
using static Sa.GameBackend.IntegrationTests.Trading.TradeWorld;

namespace Sa.GameBackend.IntegrationTests.Trading;

/// <summary>
/// ADR-0013 negotiation rules: one open trade per character and per item, a changed offer voids the
/// acceptances, every call is idempotent, and a trade nobody closed expires.
/// </summary>
[Collection(SharedPostgres.Name)]
public sealed class TradeNegotiationTests(PostgresFixture postgres) : IDisposable
{
    private const int OpenRaces = 5;

    // Every test enters a few characters (the open race ten), each registering and logging in from the same address.
    private readonly BackendFactory _factory = new(postgres.ConnectionString, new BackendSettings(CredentialAttemptsPerMinute: 60));

    [Fact]
    public async Task A_changed_offer_invalidates_acceptances_and_a_stale_version_is_refused()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        IReadOnlyList<ByteString> bItems = await world.StockAsync(b, SlimeGel);
        ByteString tradeId = (await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId))).Trade.TradeId;

        Assert.Equal(TradeOutcome.Applied, (await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 0))).Outcome);
        TradeResponse changed = await world.Trade.OfferAsync(TradeClient.Offer(World, b, tradeId, 0, bItems[0]));
        Assert.Equal(1u, changed.Trade.Version);
        Assert.False(changed.Trade.Initiator.Accepted, "B changed the deal after A agreed to the old one");

        Assert.Equal(TradeOutcome.VersionMismatch, (await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 0))).Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 1))).Outcome);
        Assert.Equal(TradeOutcome.Settled, (await world.Trade.AcceptAsync(TradeClient.Accept(World, b, tradeId, 1))).Outcome);
    }

    [Fact]
    public async Task One_open_trade_per_character_and_per_item()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        EnteredCharacter c = await world.EnterAsync();
        IReadOnlyList<ByteString> aItems = await world.StockAsync(a, SlimeGel);

        Assert.Equal(TradeOutcome.SelfTrade, (await world.Trade.OpenAsync(TradeClient.Open(World, a, a.CharacterId))).Outcome);
        TradeResponse opened = await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId));
        Assert.Equal(TradeOutcome.Applied, opened.Outcome);
        Assert.Equal(TradeOutcome.AlreadyTrading, (await world.Trade.OpenAsync(TradeClient.Open(World, c, a.CharacterId))).Outcome);
        Assert.Equal(0L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.trades WHERE initiator_id = @c", new { c = c.CharacterId }));

        // The same item twice in one offer trips the per-item key; B cannot offer what A owns.
        ByteString tradeId = opened.Trade.TradeId;
        Assert.Equal(TradeOutcome.ItemUnavailable, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, aItems[0], aItems[0]))).Outcome);
        Assert.Equal(TradeOutcome.ItemUnavailable, (await world.Trade.OfferAsync(TradeClient.Offer(World, b, tradeId, 0, aItems[0]))).Outcome);
        Assert.Equal(TradeOutcome.ItemUnavailable, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, ByteString.CopyFrom(EconomyClient.NewId())))).Outcome);
        TradeResponse offered = await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, aItems[0]));
        Assert.Equal(TradeOutcome.Applied, offered.Outcome);
        Assert.Equal(1u, offered.Trade.Version);  // refused offers changed nothing
        Assert.Equal(TradeOutcome.NotParticipant, (await world.Trade.OfferAsync(TradeClient.Offer(World, c, tradeId, 0))).Outcome);

        // While it sits in this trade, the item cannot go into another one.
        Assert.Equal(TradeOutcome.Cancelled, (await world.Trade.CancelAsync(TradeClient.Cancel(World, b, tradeId))).Outcome);
        ByteString other = (await world.Trade.OpenAsync(TradeClient.Open(World, a, c.CharacterId))).Trade.TradeId;
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, other, 0, aItems[0]))).Outcome);
    }

    [Fact]
    public async Task Cancelling_releases_everything_and_a_closed_trade_refuses_further_steps()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        IReadOnlyList<ByteString> aItems = await world.StockAsync(a, SlimeGel);
        ByteString tradeId = (await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId))).Trade.TradeId;
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, tradeId, 0, aItems[0]))).Outcome);

        // The zone's "session ended" cancel names no trade.
        TradeResponse cancelled = await world.Trade.CancelAsync(TradeClient.Cancel(World, b));
        Assert.Equal(TradeOutcome.Cancelled, cancelled.Outcome);
        Assert.Equal(TradeStatus.Cancelled, cancelled.Trade.Status);
        Assert.Equal(TradeOutcome.TradeClosed, (await world.Trade.AcceptAsync(TradeClient.Accept(World, a, tradeId, 1))).Outcome);
        Assert.Equal(TradeOutcome.NotParticipant, (await world.Trade.CancelAsync(TradeClient.Cancel(World, b))).Outcome);
        Assert.Equal(0L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.trade_items WHERE trade_id = @id", new { id = tradeId.ToByteArray() }));

        // The item is free again: a new trade can carry it, and A still owns it.
        TradeResponse reopened = await world.Trade.OpenAsync(TradeClient.Open(World, b, a.CharacterId));
        Assert.Equal(TradeOutcome.Applied, reopened.Outcome);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, reopened.Trade.TradeId, 0, aItems[0]))).Outcome);
        Assert.Single((await world.AssetsAsync(a)).Items);
    }

    [Fact]
    public async Task Two_characters_opening_with_each_other_at_once_get_one_trade_not_a_deadlock()
    {
        using var world = new TradeWorld(_factory);
        for (int round = 0; round < OpenRaces; round++)
        {
            EnteredCharacter a = await world.EnterAsync();
            EnteredCharacter b = await world.EnterAsync();
            TradeResponse[] opens = await Task.WhenAll(
                world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId)),
                world.Trade.OpenAsync(TradeClient.Open(World, b, a.CharacterId)));
            Assert.Equal(1, opens.Count(o => o.Outcome == TradeOutcome.Applied));
            Assert.Equal(1, opens.Count(o => o.Outcome == TradeOutcome.AlreadyTrading));
            Assert.Equal(1L, await Db.ScalarAsync<long>(postgres.ConnectionString,
                "SELECT count(*) FROM game.trades WHERE status = 'open' AND initiator_id = ANY(@ids)", new { ids = new[] { a.CharacterId, b.CharacterId } }));
        }
    }

    [Fact]
    public async Task A_retried_call_returns_the_stored_outcome_and_a_different_body_under_its_id_is_refused()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        EnteredCharacter c = await world.EnterAsync();
        byte[] requestId = EconomyClient.NewId();

        TradeResponse first = await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId, requestId));
        TradeResponse replay = await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId, requestId));
        Assert.Equal(first, replay);
        Assert.Equal(TradeOutcome.RequestMismatch, (await world.Trade.OpenAsync(TradeClient.Open(World, a, c.CharacterId, requestId))).Outcome);
        Assert.Equal(1L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.trades WHERE initiator_id = @a", new { a = a.CharacterId }));

        // An offer of 0 gold and an acceptance of version 0 carry the same numbers; they are still
        // different commands under one id.
        byte[] reused = EconomyClient.NewId();
        OfferTradeRequest offer = TradeClient.Offer(World, a, first.Trade.TradeId, 0);
        offer.RequestId = ByteString.CopyFrom(reused);
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(offer)).Outcome);
        Assert.Equal(TradeOutcome.RequestMismatch, (await world.Trade.AcceptAsync(TradeClient.Accept(World, a, first.Trade.TradeId, 0, reused))).Outcome);
    }

    [Fact]
    public async Task An_expired_trade_is_closed_by_the_next_call_that_meets_it()
    {
        using var world = new TradeWorld(_factory);
        EnteredCharacter a = await world.EnterAsync();
        EnteredCharacter b = await world.EnterAsync();
        EnteredCharacter c = await world.EnterAsync();
        IReadOnlyList<ByteString> aItems = await world.StockAsync(a, SlimeGel);
        ByteString stale = (await world.Trade.OpenAsync(TradeClient.Open(World, a, b.CharacterId))).Trade.TradeId;
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, stale, 0, aItems[0]))).Outcome);

        // The cancel never came: the zone died with the session. Nobody touched the trade since.
        await Db.ExecuteAsync(postgres.ConnectionString,
            "UPDATE game.trades SET created_at = now() - interval '1 hour', expires_at = now() - interval '1 minute' WHERE trade_id = @id",
            new { id = stale.ToByteArray() });

        // A new trade of A is not refused by the expired one; the expired one is closed and its item freed.
        TradeResponse fresh = await world.Trade.OpenAsync(TradeClient.Open(World, a, c.CharacterId));
        Assert.Equal(TradeOutcome.Applied, fresh.Outcome);
        Assert.Equal("cancelled", await Db.ScalarAsync<string>(postgres.ConnectionString,
            "SELECT status FROM game.trades WHERE trade_id = @id", new { id = stale.ToByteArray() }));
        Assert.Equal(TradeOutcome.Applied, (await world.Trade.OfferAsync(TradeClient.Offer(World, a, fresh.Trade.TradeId, 0, aItems[0]))).Outcome);
        Assert.Equal(TradeOutcome.TradeClosed, (await world.Trade.AcceptAsync(TradeClient.Accept(World, b, stale, 1))).Outcome);

        // A trade that expires while it is being used is closed by the call that finds it so.
        await Db.ExecuteAsync(postgres.ConnectionString,
            "UPDATE game.trades SET created_at = now() - interval '1 hour', expires_at = now() - interval '1 minute' WHERE trade_id = @id",
            new { id = fresh.Trade.TradeId.ToByteArray() });
        TradeResponse expired = await world.Trade.AcceptAsync(TradeClient.Accept(World, c, fresh.Trade.TradeId, 1));
        Assert.Equal(TradeOutcome.TradeClosed, expired.Outcome);
        Assert.Equal(TradeStatus.Cancelled, expired.Trade.Status);
        Assert.Equal(0L, await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.trade_participants WHERE character_id = ANY(@ids)", new { ids = new[] { a.CharacterId, b.CharacterId, c.CharacterId } }));
    }

    public void Dispose() => _factory.Dispose();
}

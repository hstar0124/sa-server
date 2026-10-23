using System.Data;
using Dapper;
using Microsoft.Extensions.Options;
using Npgsql;
using Sa.GameBackend.Common;
using Sa.GameBackend.Infrastructure.GameData;
using Sa.GameBackend.Modules.Economy;

namespace Sa.GameBackend.Modules.Trading;

/// <summary>
/// Player-to-player trades (ADR-0013): the negotiation is rows, the rules are constraints and
/// conditional statements. Every public method is one transaction that is idempotent on its request
/// id, like a pickup; the acceptance that completes the pair settles the trade in the same transaction.
/// </summary>
internal sealed class TradingRepository(NpgsqlDataSource dataSource, ContentCatalog content, IOptions<TradingOptions> options)
{
    private const string TradeScope = "trade";

    private const string InsertTradeSql = """
        INSERT INTO game.trades (trade_id, world_id, initiator_id, counterparty_id, status, expires_at)
        VALUES (@TradeId, @WorldId, @Initiator, @Counterparty, 'open', now() + @Expiry)
        """;

    // A character already in an open trade has a row: the primary key refuses the second seat.
    private const string SeatParticipantSql = """
        INSERT INTO game.trade_participants (world_id, character_id, trade_id)
        VALUES (@WorldId, @CharacterId, @TradeId)
        ON CONFLICT (world_id, character_id) DO NOTHING
        """;

    private const string ClearOfferSql = "DELETE FROM game.trade_items WHERE trade_id = @TradeId AND offered_by = @CharacterId";

    // Only an item in the offerer's inventory is offered, and only while no other open trade holds it
    // (the primary key of trade_items). Settlement checks the owner again under the inventory locks.
    private const string OfferItemSql = """
        INSERT INTO game.trade_items (item_instance_id, trade_id, world_id, offered_by)
        SELECT item_instance_id, @TradeId, world_id, owner_character_id
        FROM game.item_instances
        WHERE item_instance_id = @ItemInstanceId AND world_id = @WorldId AND owner_character_id = @CharacterId AND location = 'inventory'
        ON CONFLICT (item_instance_id) DO NOTHING
        """;

    private const string BalanceSql = """
        SELECT balance FROM game.wallets WHERE world_id = @WorldId AND character_id = @CharacterId AND currency = @Currency
        """;

    // Any change of the offer is a new version, and an acceptance of the old one no longer counts.
    private const string UpdateOfferSql = """
        UPDATE game.trades
        SET initiator_gold = CASE WHEN initiator_id = @CharacterId THEN @Gold ELSE initiator_gold END,
            counterparty_gold = CASE WHEN counterparty_id = @CharacterId THEN @Gold ELSE counterparty_gold END,
            version = version + 1,
            initiator_accepted_version = NULL,
            counterparty_accepted_version = NULL,
            expires_at = now() + @Expiry,
            updated_at = now()
        WHERE trade_id = @TradeId
        """;

    private const string AcceptSql = """
        UPDATE game.trades
        SET initiator_accepted_version = CASE WHEN initiator_id = @CharacterId THEN @Version ELSE initiator_accepted_version END,
            counterparty_accepted_version = CASE WHEN counterparty_id = @CharacterId THEN @Version ELSE counterparty_accepted_version END,
            expires_at = now() + @Expiry,
            updated_at = now()
        WHERE trade_id = @TradeId
        """;

    private const string ClearAcceptancesSql = """
        UPDATE game.trades SET initiator_accepted_version = NULL, counterparty_accepted_version = NULL, updated_at = now()
        WHERE trade_id = @TradeId
        """;

    private const string OpenSavepoint = "open_trade";
    private const string OfferSavepoint = "offer_trade";
    private const string SettleSavepoint = "settle_trade";

    private TimeSpan Expiry => options.Value.Expiry;

    public Task<TradeResult> OpenAsync(OpenTradeCommand command, CancellationToken ct) => RunAsync(command, t => OpenStepAsync(t, command), ct);

    public Task<TradeResult> OfferAsync(OfferTradeCommand command, CancellationToken ct) => RunAsync(command, t => OfferStepAsync(t, command), ct);

    public Task<TradeResult> AcceptAsync(AcceptTradeCommand command, CancellationToken ct) => RunAsync(command, t => AcceptStepAsync(t, command), ct);

    public Task<TradeResult> CancelAsync(CancelTradeCommand command, CancellationToken ct) => RunAsync(command, t => CancelStepAsync(t, command), ct);

    /// <summary>
    /// The frame every call shares, in the order a pickup uses: a decided request is answered from its
    /// stored outcome before authority is checked (it was decided under the authority of its time);
    /// then the caller must own the character; then the request id is claimed, and a concurrent
    /// duplicate waits here for the first to end. The step's result is stored with its effect.
    /// </summary>
    private async Task<TradeResult> RunAsync(TradeCommand command, Func<TradeTx, Task<TradeResult>> step, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        await using NpgsqlTransaction tx = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, ct);
        var key = new RequestKey(command.WorldId, TradeScope, command.RequestId.Bytes);
        byte[] hash = command.BodyHash();

        StoredCommand? earlier = await CommandResults.ReadAsync(connection, tx, key, ct);
        if (earlier is { IsDecided: true })
        {
            await tx.CommitAsync(ct);
            return earlier.Matches(hash) ? StoredTrade.Replay(earlier) : TradeResult.Refused(TradeOutcome.RequestMismatch);
        }

        CharacterOwner? owner = await CharacterOwners.ReadForShareAsync(connection, tx, command.WorldId, command.CharacterId, ct);
        if (owner is null || !owner.IsHeldBy(command.Authority))
        {
            return TradeResult.Refused(TradeOutcome.StaleAuthority);
        }

        if (await CommandResults.ClaimAsync(connection, tx, key, hash, ct) is { } stored)
        {
            await tx.CommitAsync(ct);
            return stored.Matches(hash) ? StoredTrade.Replay(stored) : TradeResult.Refused(TradeOutcome.RequestMismatch);
        }

        TradeResult result = await step(new TradeTx(connection, tx, command.WorldId, ct));
        await CommandResults.StoreAsync(connection, tx, key, result.Outcome.ToString(), StoredTrade.Serialize(result), ct);
        await tx.CommitAsync(ct);
        return result;
    }

    private async Task<TradeResult> OpenStepAsync(TradeTx t, OpenTradeCommand command)
    {
        if (command.Counterparty == command.CharacterId)
        {
            return TradeResult.Refused(TradeOutcome.SelfTrade);
        }

        CharacterOwner? counterparty = await CharacterOwners.ReadForShareAsync(t.Connection, t.Tx, t.WorldId, command.Counterparty, t.Ct);
        if (counterparty is null || !counterparty.IsSimulatedBy(command.Authority.ZoneInstanceId))
        {
            return TradeResult.Refused(TradeOutcome.CounterpartyNotHere);
        }

        // Seat the two in id order: two simultaneous opens (A to B, B to A) then wait on the same row
        // instead of each waiting on the other's, which PostgreSQL would resolve as a deadlock.
        CharacterId[] seats = [.. new[] { command.CharacterId, command.Counterparty }.OrderBy(c => c.Value)];
        await TradeRecords.CloseExpiredOfAsync(t, seats);
        TradeId tradeId = TradeId.New();
        await t.Tx.SaveAsync(OpenSavepoint, t.Ct);
        await t.Connection.ExecuteAsync(t.Cmd(InsertTradeSql,
            new { TradeId = tradeId.Bytes, t.WorldId, Initiator = command.CharacterId, command.Counterparty, Expiry }));
        int seated = 0;
        foreach (CharacterId character in seats)
        {
            seated += await t.Connection.ExecuteAsync(t.Cmd(SeatParticipantSql, new { t.WorldId, CharacterId = character, TradeId = tradeId.Bytes }));
        }

        if (seated != seats.Length)
        {
            await t.Tx.RollbackAsync(OpenSavepoint, t.Ct);  // one of them is in an open trade already; no trade row is left behind
            return TradeResult.Refused(TradeOutcome.AlreadyTrading);
        }

        return new TradeResult(TradeOutcome.Applied, await TradeRecords.ViewAsync(t, tradeId));
    }

    private async Task<TradeResult> OfferStepAsync(TradeTx t, OfferTradeCommand command)
    {
        if (await OpenTradeOfCallerAsync(t, command.Trade, command.CharacterId) is { Refusal: { } refusal })
        {
            return refusal;
        }

        var balanceKey = new { t.WorldId, command.CharacterId, Currency = content.PrimaryCurrency };
        long balance = await t.Connection.ExecuteScalarAsync<long?>(t.Cmd(BalanceSql, balanceKey)) ?? 0;
        if (command.Gold > balance)
        {
            return new TradeResult(TradeOutcome.InsufficientGold, await TradeRecords.ViewAsync(t, command.Trade));
        }

        await t.Tx.SaveAsync(OfferSavepoint, t.Ct);
        await t.Connection.ExecuteAsync(t.Cmd(ClearOfferSql, new { TradeId = command.Trade.Bytes, command.CharacterId }));
        foreach (ItemInstanceId item in command.Items)
        {
            int offered = await t.Connection.ExecuteAsync(t.Cmd(OfferItemSql,
                new { TradeId = command.Trade.Bytes, ItemInstanceId = item.Bytes, t.WorldId, command.CharacterId }));
            if (offered == 0)
            {
                // Not the offerer's, sitting in another open trade, or named twice: the earlier offer stands.
                await t.Tx.RollbackAsync(OfferSavepoint, t.Ct);
                return new TradeResult(TradeOutcome.ItemUnavailable, await TradeRecords.ViewAsync(t, command.Trade));
            }
        }

        await t.Connection.ExecuteAsync(t.Cmd(UpdateOfferSql, new { TradeId = command.Trade.Bytes, command.CharacterId, command.Gold, Expiry }));
        return new TradeResult(TradeOutcome.Applied, await TradeRecords.ViewAsync(t, command.Trade));
    }

    private async Task<TradeResult> AcceptStepAsync(TradeTx t, AcceptTradeCommand command)
    {
        (TradeRow? trade, TradeResult? refusal) = await OpenTradeOfCallerAsync(t, command.Trade, command.CharacterId);
        if (refusal is not null)
        {
            return refusal;
        }

        if (trade!.Version != command.Version)
        {
            return new TradeResult(TradeOutcome.VersionMismatch, await TradeRecords.ViewAsync(t, command.Trade));
        }

        await t.Connection.ExecuteAsync(t.Cmd(AcceptSql, new { TradeId = command.Trade.Bytes, command.CharacterId, command.Version, Expiry }));
        if (trade.AcceptedVersionOf(trade.PartnerOf(command.CharacterId)) != command.Version)
        {
            return new TradeResult(TradeOutcome.Applied, await TradeRecords.ViewAsync(t, command.Trade));
        }

        await t.Tx.SaveAsync(SettleSavepoint, t.Ct);
        TradeOutcome settled = await TradeSettlement.SettleAsync(t, trade, command, content);
        if (settled == TradeOutcome.Settled)
        {
            return new TradeResult(settled, await TradeRecords.CloseAsync(t, command.Trade, TradeStatus.Completed));
        }

        // Nothing of the exchange stays; both must look again and accept anew.
        await t.Tx.RollbackAsync(SettleSavepoint, t.Ct);
        await t.Connection.ExecuteAsync(t.Cmd(ClearAcceptancesSql, new { TradeId = command.Trade.Bytes }));
        return new TradeResult(settled, await TradeRecords.ViewAsync(t, command.Trade));
    }

    private static async Task<TradeResult> CancelStepAsync(TradeTx t, CancelTradeCommand command)
    {
        TradeId? tradeId = command.Trade ?? await TradeRecords.OpenTradeOfAsync(t, command.CharacterId);
        if (tradeId is not { } id)
        {
            return TradeResult.Refused(TradeOutcome.NotParticipant);
        }

        if (await OpenTradeOfCallerAsync(t, id, command.CharacterId) is { Refusal: { } refusal })
        {
            return refusal;
        }

        return new TradeResult(TradeOutcome.Cancelled, await TradeRecords.CloseAsync(t, id, TradeStatus.Cancelled));
    }

    /// <summary>Locks the trade and answers why the caller may not act on it; an expired trade is closed here.</summary>
    private static async Task<(TradeRow? Trade, TradeResult? Refusal)> OpenTradeOfCallerAsync(TradeTx t, TradeId tradeId, CharacterId caller)
    {
        TradeRow? trade = await TradeRecords.LockAsync(t, tradeId);
        if (trade is null || !trade.Involves(caller))
        {
            return (null, TradeResult.Refused(TradeOutcome.NotParticipant));
        }

        if (trade.IsOpen && trade.Expired)
        {
            return (trade, new TradeResult(TradeOutcome.TradeClosed, await TradeRecords.CloseAsync(t, tradeId, TradeStatus.Cancelled)));
        }

        return trade.IsOpen
            ? (trade, null)
            : (trade, new TradeResult(TradeOutcome.TradeClosed, await TradeRecords.ViewAsync(t, tradeId)));
    }
}

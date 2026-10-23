using System.Text.Json;
using Dapper;
using Npgsql;
using Sa.GameBackend.Common;
using Sa.GameBackend.Modules.Economy;

namespace Sa.GameBackend.Modules.Trading;

/// <summary>The connection, transaction and world of one trade call; every statement of the call runs in it.</summary>
internal sealed record TradeTx(NpgsqlConnection Connection, NpgsqlTransaction Tx, WorldId WorldId, CancellationToken Ct)
{
    public CommandDefinition Cmd(string sql, object parameters) => new(sql, parameters, Tx, cancellationToken: Ct);
}

/// <summary>A <c>game.trades</c> row, with whether its expiry has passed by the database clock.</summary>
internal sealed record TradeRow(
    byte[] IdBytes,
    CharacterId Initiator,
    CharacterId Counterparty,
    string Status,
    int Version,
    long InitiatorGold,
    long CounterpartyGold,
    int? InitiatorAccepted,
    int? CounterpartyAccepted,
    bool Expired)
{
    public TradeId Id => new(IdBytes);

    public bool IsOpen => string.Equals(Status, TradeRecords.OpenStatus, StringComparison.Ordinal);

    public bool Involves(CharacterId character) => character == Initiator || character == Counterparty;

    public CharacterId PartnerOf(CharacterId character) => character == Initiator ? Counterparty : Initiator;

    public long GoldOf(CharacterId character) => character == Initiator ? InitiatorGold : CounterpartyGold;

    public int? AcceptedVersionOf(CharacterId character) => character == Initiator ? InitiatorAccepted : CounterpartyAccepted;
}

/// <summary>An item on offer, joined with where the item actually is now.</summary>
internal sealed record TradeItemRow(byte[] Id, ItemTemplateId ItemTemplateId, CharacterId OfferedBy, CharacterId? Owner, string Location, int? SlotNo)
{
    public TradeOfferedItem ToOffered() => new(new ItemInstanceId(Id), ItemTemplateId);
}

/// <summary>
/// Reading a trade's state, closing it, and the stored form a retried call is answered from. The
/// trade row is always read FOR UPDATE: every call that looks at a trade may change it, and two
/// calls on one trade then take turns.
/// </summary>
internal static class TradeRecords
{
    public const string OpenStatus = "open";
    public const string CompletedStatus = "completed";
    public const string CancelledStatus = "cancelled";

    private const string LockTradeSql = """
        SELECT trade_id AS "IdBytes", initiator_id AS "Initiator", counterparty_id AS "Counterparty", status AS "Status",
               version AS "Version", initiator_gold AS "InitiatorGold", counterparty_gold AS "CounterpartyGold",
               initiator_accepted_version AS "InitiatorAccepted", counterparty_accepted_version AS "CounterpartyAccepted",
               expires_at < now() AS "Expired"
        FROM game.trades
        WHERE trade_id = @TradeId AND world_id = @WorldId
        FOR UPDATE
        """;

    private const string TradeItemsSql = """
        SELECT ti.item_instance_id AS "Id", i.item_template_id AS "ItemTemplateId", ti.offered_by AS "OfferedBy",
               i.owner_character_id AS "Owner", i.location AS "Location", i.slot_no AS "SlotNo"
        FROM game.trade_items ti
        JOIN game.item_instances i ON i.item_instance_id = ti.item_instance_id
        WHERE ti.trade_id = @TradeId
        ORDER BY ti.offered_by, ti.item_instance_id
        """;

    private const string OpenTradeOfSql = """
        SELECT trade_id FROM game.trade_participants WHERE world_id = @WorldId AND character_id = @CharacterId
        """;

    // Locked in id order so that two calls closing the same expired trades take them in one order.
    private const string ExpiredTradesOfSql = """
        SELECT t.trade_id
        FROM game.trade_participants p
        JOIN game.trades t ON t.trade_id = p.trade_id
        WHERE p.world_id = @WorldId AND p.character_id = ANY(@Characters) AND t.status = 'open' AND t.expires_at < now()
        ORDER BY t.trade_id
        FOR UPDATE OF t
        """;

    private const string CloseTradeSql = """
        UPDATE game.trades SET status = @Status, updated_at = now() WHERE trade_id = @TradeId AND status = 'open'
        """;

    // Releasing the rows frees the two characters and the offered items for their next trade.
    private const string ReleaseParticipantsSql = "DELETE FROM game.trade_participants WHERE trade_id = @TradeId";

    private const string ReleaseItemsSql = "DELETE FROM game.trade_items WHERE trade_id = @TradeId";

    public static async Task<TradeRow?> LockAsync(TradeTx t, TradeId tradeId)
        => await t.Connection.QuerySingleOrDefaultAsync<TradeRow>(t.Cmd(LockTradeSql, new { TradeId = tradeId.Bytes, t.WorldId }));

    public static async Task<IReadOnlyList<TradeItemRow>> ItemsAsync(TradeTx t, TradeId tradeId)
        => (await t.Connection.QueryAsync<TradeItemRow>(t.Cmd(TradeItemsSql, new { TradeId = tradeId.Bytes }))).ToList();

    /// <summary>The trade the character is in, if any; at most one, by the participants' primary key.</summary>
    public static async Task<TradeId?> OpenTradeOfAsync(TradeTx t, CharacterId character)
        => await t.Connection.ExecuteScalarAsync<byte[]?>(t.Cmd(OpenTradeOfSql, new { t.WorldId, CharacterId = character })) is { } id
            ? new TradeId(id)
            : null;

    public static async Task<TradeView> ViewAsync(TradeTx t, TradeId tradeId)
    {
        TradeRow trade = await LockAsync(t, tradeId) ?? throw new InvalidOperationException("the trade row vanished inside its own transaction");
        return View(trade, await ItemsAsync(t, tradeId));
    }

    /// <summary>
    /// Closes an open trade and releases its characters and items. Returns the trade as it stood at
    /// the close, offers included, so a settlement reports what changed hands.
    /// </summary>
    public static async Task<TradeView> CloseAsync(TradeTx t, TradeId tradeId, TradeStatus status)
    {
        TradeView closing = await ViewAsync(t, tradeId);
        await t.Connection.ExecuteAsync(t.Cmd(CloseTradeSql, new { TradeId = tradeId.Bytes, Status = StatusName(status) }));
        await t.Connection.ExecuteAsync(t.Cmd(ReleaseParticipantsSql, new { TradeId = tradeId.Bytes }));
        await t.Connection.ExecuteAsync(t.Cmd(ReleaseItemsSql, new { TradeId = tradeId.Bytes }));
        return closing with { Status = status };
    }

    /// <summary>Closes the expired trades these characters still sit in, so a cancel that never came does not keep them from trading.</summary>
    public static async Task CloseExpiredOfAsync(TradeTx t, IEnumerable<CharacterId> characters)
    {
        long[] ids = characters.Select(c => c.Value).ToArray();
        foreach (byte[] expired in await t.Connection.QueryAsync<byte[]>(t.Cmd(ExpiredTradesOfSql, new { t.WorldId, Characters = ids })))
        {
            await CloseAsync(t, new TradeId(expired), TradeStatus.Cancelled);
        }
    }

    public static TradeView View(TradeRow trade, IReadOnlyList<TradeItemRow> items)
        => new(trade.Id, StatusOf(trade.Status), trade.Version, SideOf(trade, trade.Initiator, items), SideOf(trade, trade.Counterparty, items));

    private static TradeSideView SideOf(TradeRow trade, CharacterId character, IEnumerable<TradeItemRow> items)
        => new(character, trade.GoldOf(character), items.Where(i => i.OfferedBy == character).Select(i => i.ToOffered()).ToList(),
            trade.AcceptedVersionOf(character) == trade.Version);

    private static TradeStatus StatusOf(string status) => status switch
    {
        OpenStatus => TradeStatus.Open,
        CompletedStatus => TradeStatus.Completed,
        CancelledStatus => TradeStatus.Cancelled,
        _ => throw new InvalidDataException($"unknown trade status {status}"),
    };

    private static string StatusName(TradeStatus status) => status switch
    {
        TradeStatus.Completed => CompletedStatus,
        TradeStatus.Cancelled => CancelledStatus,
        _ => throw new InvalidOperationException($"a trade is not closed as {status}"),
    };
}

/// <summary>The jsonb payload of a stored trade call: enough to rebuild the response for a retry.</summary>
internal sealed record StoredTrade(string? TradeId, TradeStatus Status, int Version, StoredTradeSide? Initiator, StoredTradeSide? Counterparty)
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    public static string Serialize(TradeResult result)
    {
        TradeView? trade = result.Trade;
        StoredTrade stored = trade is null
            ? new StoredTrade(null, TradeStatus.Open, 0, null, null)
            : new StoredTrade(OpaqueId.ToHex(trade.Id.Bytes), trade.Status, trade.Version, StoredTradeSide.Of(trade.Initiator), StoredTradeSide.Of(trade.Counterparty));
        return JsonSerializer.Serialize(stored, Json);
    }

    public static TradeResult Replay(StoredCommand stored)
    {
        var outcome = Enum.Parse<TradeOutcome>(stored.Outcome);
        StoredTrade payload = JsonSerializer.Deserialize<StoredTrade>(stored.Payload, Json)
                              ?? throw new InvalidDataException("stored trade payload is null");
        if (payload is not { TradeId: { } id, Initiator: { } initiator, Counterparty: { } counterparty })
        {
            return TradeResult.Refused(outcome);
        }

        return new TradeResult(outcome, new TradeView(new TradeId(Convert.FromHexString(id)), payload.Status, payload.Version, initiator.ToView(), counterparty.ToView()));
    }
}

internal sealed record StoredTradeSide(long Character, long Gold, bool Accepted, IReadOnlyList<StoredTradeItem> Items)
{
    public static StoredTradeSide Of(TradeSideView side)
        => new(side.Character.Value, side.Gold, side.Accepted, side.Items.Select(i => new StoredTradeItem(OpaqueId.ToHex(i.Id.Bytes), i.ItemTemplateId.Value)).ToList());

    public TradeSideView ToView()
        => new(new CharacterId(Character), Gold,
            Items.Select(i => new TradeOfferedItem(new ItemInstanceId(Convert.FromHexString(i.Id)), new ItemTemplateId(i.ItemTemplateId))).ToList(), Accepted);
}

internal sealed record StoredTradeItem(string Id, int ItemTemplateId);

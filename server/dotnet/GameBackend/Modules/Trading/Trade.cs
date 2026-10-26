using System.Security.Cryptography;
using Sa.GameBackend.Common;
using Sa.GameBackend.Modules.Economy;

namespace Sa.GameBackend.Modules.Trading;

internal enum TradeOutcome
{
    Applied,
    Settled,
    Cancelled,
    StaleAuthority,
    CounterpartyNotHere,
    SelfTrade,
    AlreadyTrading,
    NotParticipant,
    TradeClosed,
    VersionMismatch,
    ItemUnavailable,
    InsufficientGold,
    InventoryFull,
    RequestMismatch,
}

internal enum TradeStatus
{
    Open,
    Completed,
    Cancelled,
}

internal sealed record TradeOfferedItem(ItemInstanceId Id, ItemTemplateId ItemTemplateId);

/// <summary>One side of a trade: what it offers and whether it accepted the current version.</summary>
internal sealed record TradeSideView(CharacterId Character, long Gold, IReadOnlyList<TradeOfferedItem> Items, bool Accepted);

internal sealed record TradeView(TradeId Id, TradeStatus Status, int Version, TradeSideView Initiator, TradeSideView Counterparty);

/// <summary>The outcome of one trade call and the trade as it stands afterwards, when a trade row was found.</summary>
internal sealed record TradeResult(TradeOutcome Outcome, TradeView? Trade)
{
    public static TradeResult Refused(TradeOutcome outcome) => new(outcome, null);
}

/// <summary>
/// What every trade call carries: the idempotency key, the acting character and the zone's claim to
/// act for it. <see cref="BodyHash"/> covers everything but the request id, so a retry matches it and
/// a different command under the same id does not.
/// </summary>
internal abstract record TradeCommand(RequestId RequestId, WorldId WorldId, CharacterId CharacterId, ZoneAuthority Authority)
{
    public abstract byte[] BodyHash();
}

internal sealed record OpenTradeCommand(RequestId RequestId, WorldId WorldId, CharacterId CharacterId, ZoneAuthority Authority, CharacterId Counterparty)
    : TradeCommand(RequestId, WorldId, CharacterId, Authority)
{
    public override byte[] BodyHash() => TradeRequestHash.Of(TradeCommandKind.Open, [WorldId.Value, CharacterId.Value, Counterparty.Value], []);
}

internal sealed record OfferTradeCommand(
    RequestId RequestId,
    WorldId WorldId,
    CharacterId CharacterId,
    ZoneAuthority Authority,
    TradeId Trade,
    IReadOnlyList<ItemInstanceId> Items,
    long Gold)
    : TradeCommand(RequestId, WorldId, CharacterId, Authority)
{
    public override byte[] BodyHash()
        => TradeRequestHash.Of(TradeCommandKind.Offer, [WorldId.Value, CharacterId.Value, Gold], [Trade.Bytes, .. Items.Select(i => i.Bytes)]);
}

internal sealed record AcceptTradeCommand(RequestId RequestId, WorldId WorldId, CharacterId CharacterId, ZoneAuthority Authority, TradeId Trade, int Version)
    : TradeCommand(RequestId, WorldId, CharacterId, Authority)
{
    public override byte[] BodyHash() => TradeRequestHash.Of(TradeCommandKind.Accept, [WorldId.Value, CharacterId.Value, Version], [Trade.Bytes]);
}

/// <summary>Without a trade id: every open trade of the character, which is what a zone sends when the session ends.</summary>
internal sealed record CancelTradeCommand(RequestId RequestId, WorldId WorldId, CharacterId CharacterId, ZoneAuthority Authority, TradeId? Trade)
    : TradeCommand(RequestId, WorldId, CharacterId, Authority)
{
    public override byte[] BodyHash()
        => TradeRequestHash.Of(TradeCommandKind.Cancel, [WorldId.Value, CharacterId.Value], Trade is { } trade ? [trade.Bytes] : []);
}

/// <summary>Leads the hashed body, so that an offer and an acceptance with equal numbers never hash alike under one request id.</summary>
internal enum TradeCommandKind : byte
{
    Open = 1,
    Offer = 2,
    Accept = 3,
    Cancel = 4,
}

/// <summary>SHA-256 over the kind, the numbers and the 16-byte ids of a command, in that order.</summary>
internal static class TradeRequestHash
{
    public static byte[] Of(TradeCommandKind kind, ReadOnlySpan<long> numbers, IReadOnlyList<byte[]> ids)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        hash.AppendData([(byte)kind]);
        Span<byte> scratch = stackalloc byte[sizeof(long)];
        foreach (long number in numbers)
        {
            BitConverter.TryWriteBytes(scratch, number);
            hash.AppendData(scratch);
        }

        foreach (byte[] id in ids)
        {
            hash.AppendData(id);
        }

        return hash.GetHashAndReset();
    }
}

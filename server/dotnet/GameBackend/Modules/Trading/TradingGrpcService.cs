using Google.Protobuf;
using Grpc.Core;
using Sa.Backend.V1;
using Sa.GameBackend.Common;
using Sa.GameBackend.Modules.Economy;
using ZoneAuthority = Sa.GameBackend.Common.ZoneAuthority;

namespace Sa.GameBackend.Modules.Trading;

/// <summary>gRPC surface for zones. Pure mapping: every decision lives in <see cref="TradingRepository"/>.</summary>
internal sealed class TradingGrpcService(TradingRepository repository) : Sa.Backend.V1.Trade.TradeBase
{
    public override async Task<TradeResponse> OpenTrade(OpenTradeRequest request, ServerCallContext context)
    {
        (RequestId requestId, ZoneAuthority authority) = Identity(request.RequestId, request.Authority);
        TradeResult result = await repository.OpenAsync(
            new OpenTradeCommand(requestId, new WorldId((long)request.WorldId), new CharacterId((long)request.CharacterId), authority,
                new CharacterId((long)request.CounterpartyCharacterId)),
            context.CancellationToken);
        return Map(result);
    }

    public override async Task<TradeResponse> OfferTrade(OfferTradeRequest request, ServerCallContext context)
    {
        (RequestId requestId, ZoneAuthority authority) = Identity(request.RequestId, request.Authority);
        var items = new List<ItemInstanceId>(request.ItemInstanceIds.Count);
        foreach (ByteString raw in request.ItemInstanceIds)
        {
            if (!ItemInstanceId.TryFrom(raw.Span, out ItemInstanceId item))
            {
                throw new RpcException(new Status(StatusCode.InvalidArgument, "item_instance_ids must be 16 bytes each"));
            }

            items.Add(item);
        }

        // Balances are bigint: an amount beyond it is malformed, not a large offer.
        if (request.Gold > long.MaxValue)
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "gold is out of range"));
        }

        TradeResult result = await repository.OfferAsync(
            new OfferTradeCommand(requestId, new WorldId((long)request.WorldId), new CharacterId((long)request.CharacterId), authority,
                TradeIdOf(request.TradeId), items, (long)request.Gold),
            context.CancellationToken);
        return Map(result);
    }

    public override async Task<TradeResponse> AcceptTrade(AcceptTradeRequest request, ServerCallContext context)
    {
        (RequestId requestId, ZoneAuthority authority) = Identity(request.RequestId, request.Authority);
        if (request.Version > int.MaxValue)
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "version is out of range"));
        }

        TradeResult result = await repository.AcceptAsync(
            new AcceptTradeCommand(requestId, new WorldId((long)request.WorldId), new CharacterId((long)request.CharacterId), authority,
                TradeIdOf(request.TradeId), (int)request.Version),
            context.CancellationToken);
        return Map(result);
    }

    public override async Task<TradeResponse> CancelTrade(CancelTradeRequest request, ServerCallContext context)
    {
        (RequestId requestId, ZoneAuthority authority) = Identity(request.RequestId, request.Authority);
        TradeId? trade = request.TradeId.IsEmpty ? null : TradeIdOf(request.TradeId);
        TradeResult result = await repository.CancelAsync(
            new CancelTradeCommand(requestId, new WorldId((long)request.WorldId), new CharacterId((long)request.CharacterId), authority, trade),
            context.CancellationToken);
        return Map(result);
    }

    private static (RequestId, ZoneAuthority) Identity(ByteString requestId, Sa.Backend.V1.ZoneAuthority? authority)
    {
        if (!RequestId.TryFrom(requestId.Span, out RequestId id) || !ZoneAuthority.TryFrom(authority, out ZoneAuthority claim))
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "request_id and authority.zone_instance_id must be 16 bytes"));
        }

        return (id, claim);
    }

    private static TradeId TradeIdOf(ByteString raw)
    {
        if (!TradeId.TryFrom(raw.Span, out TradeId id))
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "trade_id must be 16 bytes"));
        }

        return id;
    }

    private static TradeResponse Map(TradeResult result)
    {
        var response = new TradeResponse { Outcome = Map(result.Outcome) };
        if (result.Trade is { } trade)
        {
            response.Trade = new TradeState
            {
                TradeId = ByteString.CopyFrom(trade.Id.Bytes),
                Status = Map(trade.Status),
                Version = (uint)trade.Version,
                Initiator = Map(trade.Initiator),
                Counterparty = Map(trade.Counterparty),
            };
        }

        return response;
    }

    private static Sa.Backend.V1.TradeSide Map(TradeSideView side)
    {
        var mapped = new Sa.Backend.V1.TradeSide { CharacterId = (ulong)side.Character.Value, Gold = (ulong)side.Gold, Accepted = side.Accepted };
        mapped.Items.AddRange(side.Items.Select(item => new TradeItem
        {
            ItemInstanceId = ByteString.CopyFrom(item.Id.Bytes),
            ItemTemplateId = (uint)item.ItemTemplateId.Value,
        }));
        return mapped;
    }

    private static Sa.Backend.V1.TradeStatus Map(TradeStatus status) => status switch
    {
        TradeStatus.Open => Sa.Backend.V1.TradeStatus.Open,
        TradeStatus.Completed => Sa.Backend.V1.TradeStatus.Completed,
        TradeStatus.Cancelled => Sa.Backend.V1.TradeStatus.Cancelled,
        _ => throw new InvalidOperationException($"unmapped status {status}"),
    };

    private static Sa.Backend.V1.TradeOutcome Map(TradeOutcome outcome) => outcome switch
    {
        TradeOutcome.Applied => Sa.Backend.V1.TradeOutcome.Applied,
        TradeOutcome.Settled => Sa.Backend.V1.TradeOutcome.Settled,
        TradeOutcome.Cancelled => Sa.Backend.V1.TradeOutcome.Cancelled,
        TradeOutcome.StaleAuthority => Sa.Backend.V1.TradeOutcome.StaleAuthority,
        TradeOutcome.CounterpartyNotHere => Sa.Backend.V1.TradeOutcome.CounterpartyNotHere,
        TradeOutcome.SelfTrade => Sa.Backend.V1.TradeOutcome.SelfTrade,
        TradeOutcome.AlreadyTrading => Sa.Backend.V1.TradeOutcome.AlreadyTrading,
        TradeOutcome.NotParticipant => Sa.Backend.V1.TradeOutcome.NotParticipant,
        TradeOutcome.TradeClosed => Sa.Backend.V1.TradeOutcome.TradeClosed,
        TradeOutcome.VersionMismatch => Sa.Backend.V1.TradeOutcome.VersionMismatch,
        TradeOutcome.ItemUnavailable => Sa.Backend.V1.TradeOutcome.ItemUnavailable,
        TradeOutcome.InsufficientGold => Sa.Backend.V1.TradeOutcome.InsufficientGold,
        TradeOutcome.InventoryFull => Sa.Backend.V1.TradeOutcome.InventoryFull,
        TradeOutcome.RequestMismatch => Sa.Backend.V1.TradeOutcome.RequestMismatch,
        _ => throw new InvalidOperationException($"unmapped outcome {outcome}"),
    };
}

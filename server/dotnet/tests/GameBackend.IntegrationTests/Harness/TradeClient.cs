using Google.Protobuf;
using Grpc.Net.Client;
using Sa.Backend.V1;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Plays the zone's role against the Trade service over in-process gRPC.</summary>
internal sealed class TradeClient : IDisposable
{
    private readonly GrpcChannel _channel;
    private readonly Sa.Backend.V1.Trade.TradeClient _client;

    public TradeClient(BackendFactory factory)
    {
        _channel = GrpcChannel.ForAddress(factory.Server.BaseAddress, new GrpcChannelOptions { HttpHandler = factory.Server.CreateHandler() });
        _client = new Sa.Backend.V1.Trade.TradeClient(_channel);
    }

    public static OpenTradeRequest Open(long worldId, EnteredCharacter caller, long counterpartyId, byte[]? requestId = null) => new()
    {
        RequestId = ByteString.CopyFrom(requestId ?? EconomyClient.NewId()),
        WorldId = (ulong)worldId,
        CharacterId = (ulong)caller.CharacterId,
        Authority = caller.Authority,
        CounterpartyCharacterId = (ulong)counterpartyId,
    };

    public static OfferTradeRequest Offer(long worldId, EnteredCharacter caller, ByteString tradeId, ulong gold, params ByteString[] items)
    {
        var request = new OfferTradeRequest
        {
            RequestId = ByteString.CopyFrom(EconomyClient.NewId()),
            WorldId = (ulong)worldId,
            CharacterId = (ulong)caller.CharacterId,
            Authority = caller.Authority,
            TradeId = tradeId,
            Gold = gold,
        };
        request.ItemInstanceIds.AddRange(items);
        return request;
    }

    public static AcceptTradeRequest Accept(long worldId, EnteredCharacter caller, ByteString tradeId, uint version, byte[]? requestId = null) => new()
    {
        RequestId = ByteString.CopyFrom(requestId ?? EconomyClient.NewId()),
        WorldId = (ulong)worldId,
        CharacterId = (ulong)caller.CharacterId,
        Authority = caller.Authority,
        TradeId = tradeId,
        Version = version,
    };

    public static CancelTradeRequest Cancel(long worldId, EnteredCharacter caller, ByteString? tradeId = null) => new()
    {
        RequestId = ByteString.CopyFrom(EconomyClient.NewId()),
        WorldId = (ulong)worldId,
        CharacterId = (ulong)caller.CharacterId,
        Authority = caller.Authority,
        TradeId = tradeId ?? ByteString.Empty,
    };

    public async Task<TradeResponse> OpenAsync(OpenTradeRequest request) => await _client.OpenTradeAsync(request);

    public async Task<TradeResponse> OfferAsync(OfferTradeRequest request) => await _client.OfferTradeAsync(request);

    public async Task<TradeResponse> AcceptAsync(AcceptTradeRequest request) => await _client.AcceptTradeAsync(request);

    public async Task<TradeResponse> CancelAsync(CancelTradeRequest request) => await _client.CancelTradeAsync(request);

    public void Dispose() => _channel.Dispose();
}

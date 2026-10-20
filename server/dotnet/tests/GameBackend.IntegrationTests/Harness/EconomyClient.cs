using Google.Protobuf;
using Grpc.Net.Client;
using Sa.Backend.V1;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Plays the zone's role against the Economy service over in-process gRPC.</summary>
internal sealed class EconomyClient : IDisposable
{
    private readonly GrpcChannel _channel;
    private readonly Sa.Backend.V1.Economy.EconomyClient _client;

    public EconomyClient(BackendFactory factory)
    {
        _channel = GrpcChannel.ForAddress(factory.Server.BaseAddress, new GrpcChannelOptions { HttpHandler = factory.Server.CreateHandler() });
        _client = new Sa.Backend.V1.Economy.EconomyClient(_channel);
    }

    public static byte[] NewId() => Guid.NewGuid().ToByteArray();

    public static ZoneAuthority AuthorityOf(RedeemEntryTicketResponse granted)
        => new() { ZoneInstanceId = granted.Zone.ZoneInstanceId, OwnerEpoch = granted.OwnerEpoch };

    /// <summary>Builds a kill report for the given monster with one roll per item.</summary>
    public static RecordKillRequest KillRequest(long worldId, long killerId, ZoneAuthority authority, uint monsterTemplateId, string contentVersion, params uint[] itemTemplateIds)
    {
        var request = new RecordKillRequest
        {
            KillId = ByteString.CopyFrom(NewId()),
            WorldId = (ulong)worldId,
            KillerCharacterId = (ulong)killerId,
            Authority = authority,
            MonsterTemplateId = monsterTemplateId,
            ContentVersion = contentVersion,
        };
        request.Drops.AddRange(itemTemplateIds.Select((item, i) => new DropRoll { ItemTemplateId = item, Position = new Sa.Game.V1.Vec2 { X = i, Y = 0 } }));
        return request;
    }

    public static PickupItemRequest PickupRequest(long worldId, long characterId, ZoneAuthority authority, ByteString itemInstanceId, byte[]? requestId = null) => new()
    {
        RequestId = ByteString.CopyFrom(requestId ?? NewId()),
        WorldId = (ulong)worldId,
        CharacterId = (ulong)characterId,
        Authority = authority,
        ItemInstanceId = itemInstanceId,
    };

    public async Task<RecordKillResponse> RecordKillAsync(RecordKillRequest request) => await _client.RecordKillAsync(request);

    public async Task<PickupItemResponse> PickupAsync(PickupItemRequest request) => await _client.PickupItemAsync(request);

    public async Task<GetCharacterAssetsResponse> AssetsAsync(long worldId, long characterId)
        => await _client.GetCharacterAssetsAsync(new GetCharacterAssetsRequest { WorldId = (ulong)worldId, CharacterId = (ulong)characterId });

    public void Dispose() => _channel.Dispose();
}

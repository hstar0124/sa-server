using Google.Protobuf;
using Grpc.Net.Client;
using Sa.Backend.V1;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Plays the gateway and zone roles against WorldDirectory over in-process gRPC.</summary>
internal sealed class DirectoryClient : IDisposable
{
    private readonly GrpcChannel _channel;
    private readonly Sa.Backend.V1.WorldDirectory.WorldDirectoryClient _client;

    public DirectoryClient(BackendFactory factory)
    {
        _channel = GrpcChannel.ForAddress(factory.Server.BaseAddress, new GrpcChannelOptions { HttpHandler = factory.Server.CreateHandler() });
        _client = new Sa.Backend.V1.WorldDirectory.WorldDirectoryClient(_channel);
    }

    public static byte[] NewInstanceId() => Guid.NewGuid().ToByteArray();

    public async Task<RenewGatewayLeaseResponse> RenewGatewayAsync(byte[] gatewayInstanceId)
        => await _client.RenewGatewayLeaseAsync(new RenewGatewayLeaseRequest { GatewayInstanceId = ByteString.CopyFrom(gatewayInstanceId) });

    public async Task<RenewZoneLeaseResponse> RenewZoneAsync(byte[] zoneInstanceId, long worldId, uint zoneId)
        => await _client.RenewZoneLeaseAsync(new RenewZoneLeaseRequest
        {
            Zone = new ZoneInstanceRef { WorldId = (ulong)worldId, ZoneId = zoneId, ZoneInstanceId = ByteString.CopyFrom(zoneInstanceId) },
        });

    /// <summary>Redeems exactly as a gateway would: identity fields copied from the signed ticket.</summary>
    public async Task<RedeemEntryTicketResponse> RedeemAsync(byte[] signedTicketBytes, byte[] gatewayInstanceId, byte[] sessionId, Action<RedeemEntryTicketRequest>? tamper = null)
    {
        EntryTicket ticket = EntryTicket.Parser.ParseFrom(SignedEntryTicket.Parser.ParseFrom(signedTicketBytes).Ticket);
        var request = new RedeemEntryTicketRequest
        {
            TicketId = ticket.TicketId,
            AccountId = ticket.AccountId,
            CharacterId = ticket.CharacterId,
            WorldId = ticket.WorldId,
            GatewayInstanceId = ByteString.CopyFrom(gatewayInstanceId),
            GatewaySessionId = ByteString.CopyFrom(sessionId),
        };
        tamper?.Invoke(request);
        return await _client.RedeemEntryTicketAsync(request);
    }

    public async Task<EndSessionResponse> EndSessionAsync(byte[] sessionId, string reason = "test")
        => await _client.EndSessionAsync(new EndSessionRequest { GatewaySessionId = ByteString.CopyFrom(sessionId), Reason = reason });

    public void Dispose() => _channel.Dispose();
}

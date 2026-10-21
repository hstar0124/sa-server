using Google.Protobuf;
using Grpc.Core;
using Microsoft.Extensions.Options;
using Sa.Backend.V1;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.WorldDirectory;

/// <summary>gRPC surface for gateways and zones. Pure mapping: decisions live in <see cref="WorldDirectoryRepository"/>.</summary>
internal sealed partial class WorldDirectoryGrpcService(
    WorldDirectoryRepository repository,
    IOptions<WorldDirectoryOptions> options,
    ILogger<WorldDirectoryGrpcService> logger) : Sa.Backend.V1.WorldDirectory.WorldDirectoryBase
{
    public override async Task<RedeemEntryTicketResponse> RedeemEntryTicket(RedeemEntryTicketRequest request, ServerCallContext context)
    {
        if (!TicketId.TryFrom(request.TicketId.Span, out TicketId ticketId)
            || !InstanceId.TryFrom(request.GatewayInstanceId.Span, out InstanceId gatewayId)
            || !InstanceId.TryFrom(request.GatewaySessionId.Span, out InstanceId sessionId))
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "ticket_id, gateway_instance_id and gateway_session_id must be 16 bytes"));
        }

        RedeemResult result = await repository.RedeemEntryTicketAsync(
            new RedeemRequest(ticketId, new AccountId((long)request.AccountId), new CharacterId((long)request.CharacterId),
                new WorldId((long)request.WorldId), gatewayId, sessionId),
            context.CancellationToken);

        var response = new RedeemEntryTicketResponse { Outcome = Map(result.Outcome) };
        if (result is { Zone: { } zone, Character: { } character })
        {
            response.Zone = new ZoneInstanceRef
            {
                WorldId = (ulong)zone.WorldId.Value,
                ZoneId = (uint)zone.ZoneId.Value,
                ZoneInstanceId = ByteString.CopyFrom(zone.ZoneInstanceId.Bytes),
            };
            response.OwnerEpoch = (ulong)zone.OwnerEpoch.Value;
            response.Character = new Sa.Backend.V1.CharacterProfile
            {
                CharacterId = (ulong)character.Id.Value,
                Name = character.Name,
                ClassId = (uint)character.ClassId.Value,
            };
        }

        return response;
    }

    public override async Task<EndSessionResponse> EndSession(EndSessionRequest request, ServerCallContext context)
    {
        if (!InstanceId.TryFrom(request.GatewaySessionId.Span, out InstanceId sessionId))
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "gateway_session_id must be 16 bytes"));
        }

        EndSessionOutcome outcome = await repository.EndSessionAsync(sessionId, request.Reason, context.CancellationToken);
        return new EndSessionResponse { Ended = outcome == EndSessionOutcome.Ended };
    }

    public override async Task<RenewZoneLeaseResponse> RenewZoneLease(RenewZoneLeaseRequest request, ServerCallContext context)
    {
        if (request.Zone is null || !InstanceId.TryFrom(request.Zone.ZoneInstanceId.Span, out InstanceId instanceId) || request.Zone.ZoneId == 0)
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "zone must carry a 16-byte instance id and a positive zone id"));
        }

        ZoneLeaseRenewal renewal = await repository.RenewZoneLeaseAsync(
            instanceId, new WorldId((long)request.Zone.WorldId), new ZoneId((int)request.Zone.ZoneId), options.Value.ZoneLeaseDuration, context.CancellationToken);
        if (renewal.Outcome != ZoneLeaseOutcome.Renewed)
        {
            LogZoneLeaseRefused(instanceId, request.Zone.ZoneId, renewal.Outcome);
            return new RenewZoneLeaseResponse { Renewed = false };
        }

        return new RenewZoneLeaseResponse
        {
            Renewed = true,
            LeaseExpiresAtMs = (ulong)renewal.ExpiresAt!.Value.ToUnixTimeMilliseconds(),
        };
    }

    public override async Task<RenewGatewayLeaseResponse> RenewGatewayLease(RenewGatewayLeaseRequest request, ServerCallContext context)
    {
        if (!InstanceId.TryFrom(request.GatewayInstanceId.Span, out InstanceId instanceId))
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "gateway_instance_id must be 16 bytes"));
        }

        DateTimeOffset expires = await repository.RenewGatewayLeaseAsync(instanceId, options.Value.GatewayLeaseDuration, context.CancellationToken);
        return new RenewGatewayLeaseResponse { Renewed = true, LeaseExpiresAtMs = (ulong)expires.ToUnixTimeMilliseconds() };
    }

    private static Sa.Backend.V1.RedeemOutcome Map(RedeemOutcome outcome) => outcome switch
    {
        RedeemOutcome.Granted => Sa.Backend.V1.RedeemOutcome.Granted,
        RedeemOutcome.AlreadyRedeemed => Sa.Backend.V1.RedeemOutcome.AlreadyRedeemed,
        RedeemOutcome.UnknownTicket => Sa.Backend.V1.RedeemOutcome.UnknownTicket,
        RedeemOutcome.Expired => Sa.Backend.V1.RedeemOutcome.Expired,
        RedeemOutcome.ActiveSessionExists => Sa.Backend.V1.RedeemOutcome.ActiveSessionExists,
        RedeemOutcome.CharacterNotOwned => Sa.Backend.V1.RedeemOutcome.CharacterNotOwned,
        RedeemOutcome.NoZoneAvailable => Sa.Backend.V1.RedeemOutcome.NoZoneAvailable,
        RedeemOutcome.UnknownGateway => Sa.Backend.V1.RedeemOutcome.UnknownGateway,
        _ => throw new InvalidOperationException($"unmapped outcome {outcome}"),
    };

    [LoggerMessage(Level = LogLevel.Warning, Message = "zone lease refused for instance {Instance} (zone {ZoneId}): {Outcome}")]
    private partial void LogZoneLeaseRefused(InstanceId instance, uint zoneId, ZoneLeaseOutcome outcome);
}

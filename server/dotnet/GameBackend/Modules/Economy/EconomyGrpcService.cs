using Google.Protobuf;
using Grpc.Core;
using Sa.Backend.V1;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Economy;

/// <summary>gRPC surface for zones. Pure mapping: every decision lives in <see cref="EconomyRepository"/>.</summary>
internal sealed class EconomyGrpcService(EconomyRepository repository) : Sa.Backend.V1.Economy.EconomyBase
{
    public override async Task<RecordKillResponse> RecordKill(RecordKillRequest request, ServerCallContext context)
    {
        if (!KillId.TryFrom(request.KillId.Span, out KillId killId) || !TryAuthority(request.Authority, out ZoneAuthority authority))
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "kill_id and authority.zone_instance_id must be 16 bytes"));
        }

        List<DropRoll> drops = request.Drops.Select(d => new DropRoll((int)d.ItemTemplateId, d.Position?.X ?? 0, d.Position?.Y ?? 0)).ToList();
        KillResult result = await repository.RecordKillAsync(
            new RecordKillCommand(killId, new WorldId((long)request.WorldId), new CharacterId((long)request.KillerCharacterId), authority,
                (int)request.MonsterTemplateId, request.ContentVersion, drops),
            context.CancellationToken);

        var response = new RecordKillResponse
        {
            Outcome = Map(result.Outcome),
            GoldGranted = (ulong)result.GoldGranted,
            GoldBalance = (ulong)result.GoldBalance,
        };
        response.Items.AddRange(result.Items.Select(item => new Sa.Backend.V1.DroppedItem
        {
            ItemInstanceId = ByteString.CopyFrom(item.Id.Bytes),
            ItemTemplateId = (uint)item.ItemTemplateId,
            Position = new Sa.Game.V1.Vec2 { X = item.X, Y = item.Y },
        }));
        return response;
    }

    public override async Task<PickupItemResponse> PickupItem(PickupItemRequest request, ServerCallContext context)
    {
        if (!RequestId.TryFrom(request.RequestId.Span, out RequestId requestId)
            || !ItemInstanceId.TryFrom(request.ItemInstanceId.Span, out ItemInstanceId itemId)
            || !TryAuthority(request.Authority, out ZoneAuthority authority))
        {
            throw new RpcException(new Status(StatusCode.InvalidArgument, "request_id, item_instance_id and authority.zone_instance_id must be 16 bytes"));
        }

        PickupResult result = await repository.PickupAsync(
            new PickupCommand(requestId, new WorldId((long)request.WorldId), new CharacterId((long)request.CharacterId), authority, itemId),
            context.CancellationToken);

        var response = new PickupItemResponse { Outcome = Map(result.Outcome), InventoryVersion = (uint)result.InventoryVersion };
        if (result.Item is { } item)
        {
            response.Item = Map(item);
        }

        return response;
    }

    public override async Task<GetCharacterAssetsResponse> GetCharacterAssets(GetCharacterAssetsRequest request, ServerCallContext context)
    {
        CharacterAssets assets = await repository.GetAssetsAsync(new WorldId((long)request.WorldId), new CharacterId((long)request.CharacterId), context.CancellationToken);
        var response = new GetCharacterAssetsResponse
        {
            InventoryCapacity = (uint)assets.InventoryCapacity,
            InventoryVersion = (uint)assets.InventoryVersion,
        };
        response.Items.AddRange(assets.Items.Select(Map));
        response.Wallet.AddRange(assets.Wallet.Select(w => new Sa.Backend.V1.WalletEntry { Currency = w.Currency, Balance = (ulong)w.Balance }));
        return response;
    }

    private static bool TryAuthority(Sa.Backend.V1.ZoneAuthority? proto, out ZoneAuthority authority)
    {
        authority = null!;
        if (proto is null || !InstanceId.TryFrom(proto.ZoneInstanceId.Span, out InstanceId zone))
        {
            return false;
        }

        authority = new ZoneAuthority(zone, (long)proto.OwnerEpoch);
        return true;
    }

    private static Sa.Backend.V1.InventoryItem Map(InventoryItem item) => new()
    {
        ItemInstanceId = ByteString.CopyFrom(item.Id.Bytes),
        ItemTemplateId = (uint)item.ItemTemplateId,
        SlotNo = (uint)item.SlotNo,
    };

    private static Sa.Backend.V1.KillOutcome Map(KillOutcome outcome) => outcome switch
    {
        KillOutcome.Recorded => Sa.Backend.V1.KillOutcome.Recorded,
        KillOutcome.AlreadyRecorded => Sa.Backend.V1.KillOutcome.AlreadyRecorded,
        KillOutcome.StaleAuthority => Sa.Backend.V1.KillOutcome.StaleAuthority,
        KillOutcome.ContentVersionMismatch => Sa.Backend.V1.KillOutcome.ContentVersionMismatch,
        KillOutcome.UnknownTemplate => Sa.Backend.V1.KillOutcome.UnknownTemplate,
        _ => throw new InvalidOperationException($"unmapped outcome {outcome}"),
    };

    private static Sa.Backend.V1.PickupOutcome Map(PickupOutcome outcome) => outcome switch
    {
        PickupOutcome.Acquired => Sa.Backend.V1.PickupOutcome.Acquired,
        PickupOutcome.AlreadyTaken => Sa.Backend.V1.PickupOutcome.AlreadyTaken,
        PickupOutcome.InventoryFull => Sa.Backend.V1.PickupOutcome.InventoryFull,
        PickupOutcome.StaleAuthority => Sa.Backend.V1.PickupOutcome.StaleAuthority,
        PickupOutcome.RequestMismatch => Sa.Backend.V1.PickupOutcome.RequestMismatch,
        PickupOutcome.UnknownItem => Sa.Backend.V1.PickupOutcome.UnknownItem,
        _ => throw new InvalidOperationException($"unmapped outcome {outcome}"),
    };
}

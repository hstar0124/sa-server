using System.Net.Http.Json;
using Grpc.Core;
using Sa.Backend.V1;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.WorldDirectory;

/// <summary>
/// ADR-0010: a late save of the previous owner is refused, a transfer moves the ownership once, a
/// zone whose lease ended acts for nobody, and re-entry lands where the character was.
/// </summary>
[Collection(SharedPostgres.Name)]
public sealed class TransferTests(PostgresFixture postgres) : IDisposable
{
    // World 1: other tests lease zone 1 there as well, and world 2 must stay without zones for the redeem tests.
    private const long World = 1;
    private const uint Meadow = 1;
    private const uint Highlands = 2;
    private const uint NoSuchZone = 9;

    private readonly BackendFactory _factory = new(postgres.ConnectionString);

    private ApiClient Api => new(_factory.CreateClient());

    private DirectoryClient Directory => new(_factory);

    private static async Task<(byte[] Gateway, byte[] MeadowZone)> TwoZonesAsync(DirectoryClient directory)
    {
        (byte[] gateway, byte[] meadow) = await WorldEntry.LiveInfrastructureAsync(directory, World, Meadow);
        Assert.True((await directory.RenewZoneAsync(DirectoryClient.NewInstanceId(), World, Highlands)).Renewed);
        return (gateway, meadow);
    }

    private async Task<string> OwnershipAsync(long characterId)
        => await Db.ScalarAsync<string>(postgres.ConnectionString,
            "SELECT zone_id || ':' || owner_epoch FROM game.character_ownership WHERE world_id = @World AND character_id = @characterId", new { World, characterId });

    private static async Task<RedeemEntryTicketResponse> ReenterAsync(DirectoryClient directory, byte[] gateway, EnteredCharacter entered)
    {
        await directory.EndSessionAsync(entered.SessionId);
        HttpResponseMessage enter = await entered.Owner.EnterAsync(World, entered.CharacterId);
        enter.EnsureSuccessStatusCode();
        ApiClient.EnterResponse body = (await enter.Content.ReadFromJsonAsync<ApiClient.EnterResponse>())!;
        RedeemEntryTicketResponse grant = await directory.RedeemAsync(Convert.FromBase64String(body.EntryTicket), gateway, DirectoryClient.NewInstanceId());
        Assert.Equal(RedeemOutcome.Granted, grant.Outcome);
        return grant;
    }

    [Fact]
    public async Task Checkpoints_advance_by_version_and_the_previous_owners_late_save_is_refused()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await TwoZonesAsync(directory);
        EnteredCharacter entered = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        ZoneAuthority zoneA = entered.Authority;

        Assert.Equal(CheckpointOutcome.Saved, (await directory.SaveCheckpointAsync(World, entered.CharacterId, zoneA, Meadow, 10, 20, 1)).Outcome);
        Assert.Equal(CheckpointOutcome.Saved, (await directory.SaveCheckpointAsync(World, entered.CharacterId, zoneA, Meadow, 11, 21, 2)).Outcome);
        Assert.Equal(CheckpointOutcome.StaleVersion, (await directory.SaveCheckpointAsync(World, entered.CharacterId, zoneA, Meadow, 0, 0, 2)).Outcome);

        // The character leaves and re-enters: a new ownership generation. The old zone's late save must lose.
        RedeemEntryTicketResponse again = await ReenterAsync(directory, gateway, entered);
        Assert.Equal(entered.Grant.OwnerEpoch + 1, again.OwnerEpoch);
        Assert.Equal(Meadow, again.Zone.ZoneId);
        Assert.Equal(11, again.Checkpoint.Position.X);  // placed where the last accepted save left it
        Assert.Equal(CheckpointOutcome.StaleAuthority, (await directory.SaveCheckpointAsync(World, entered.CharacterId, zoneA, Meadow, 99, 99, 3)).Outcome);
        Assert.Equal(CheckpointOutcome.Saved, (await directory.SaveCheckpointAsync(World, entered.CharacterId, EconomyClient.AuthorityOf(again), Meadow, 12, 22, 1)).Outcome);
    }

    [Fact]
    public async Task A_transfer_moves_ownership_once_and_a_retry_returns_the_same_decision()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await TwoZonesAsync(directory);
        EnteredCharacter entered = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        byte[] transferId = DirectoryClient.NewInstanceId();

        TransferCharacterResponse moved = await directory.TransferAsync(World, entered.CharacterId, entered.Authority, Highlands, transferId, x: 8, y: 100);
        TransferCharacterResponse retried = await directory.TransferAsync(World, entered.CharacterId, entered.Authority, Highlands, transferId);
        TransferCharacterResponse stale = await directory.TransferAsync(World, entered.CharacterId, entered.Authority, Meadow);

        Assert.Equal(TransferOutcome.Transferred, moved.Outcome);
        Assert.Equal(Highlands, moved.ToZone.ZoneId);
        Assert.Equal(entered.Grant.OwnerEpoch + 1, moved.OwnerEpoch);
        Assert.Equal(TransferOutcome.AlreadyTransferred, retried.Outcome);
        Assert.Equal(moved.ToZone.ZoneInstanceId, retried.ToZone.ZoneInstanceId);
        Assert.Equal(moved.OwnerEpoch, retried.OwnerEpoch);
        Assert.Equal(TransferOutcome.StaleAuthority, stale.Outcome);  // the meadow no longer owns the character
        Assert.Equal($"{Highlands}:{moved.OwnerEpoch}", await OwnershipAsync(entered.CharacterId));
        long sessionEpoch = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT owner_epoch FROM game.character_sessions WHERE gateway_session_id = @id", new { id = entered.SessionId });
        Assert.Equal((long)moved.OwnerEpoch, sessionEpoch);  // the live session follows the generation
        long transfers = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.zone_transfers WHERE character_id = @c AND outcome = 'transferred'", new { c = entered.CharacterId });
        Assert.Equal(1, transfers);

        // Re-entry after the move lands in the highlands at the arrival point, under the next generation.
        RedeemEntryTicketResponse again = await ReenterAsync(directory, gateway, entered);
        Assert.Equal(Highlands, again.Zone.ZoneId);
        Assert.Equal(8, again.Checkpoint.Position.X);
        Assert.Equal(moved.OwnerEpoch + 1, again.OwnerEpoch);
    }

    [Fact]
    public async Task Two_copies_of_one_transfer_at_once_move_the_character_once()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await TwoZonesAsync(directory);
        EnteredCharacter entered = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        byte[] transferId = DirectoryClient.NewInstanceId();

        TransferCharacterResponse[] answers = await Task.WhenAll(
            directory.TransferAsync(World, entered.CharacterId, entered.Authority, Highlands, transferId),
            directory.TransferAsync(World, entered.CharacterId, entered.Authority, Highlands, transferId));

        Assert.Single(answers, a => a.Outcome == TransferOutcome.Transferred);
        Assert.Single(answers, a => a.Outcome == TransferOutcome.AlreadyTransferred);
        Assert.Equal(answers[0].OwnerEpoch, answers[1].OwnerEpoch);
        Assert.Equal($"{Highlands}:{entered.Grant.OwnerEpoch + 1}", await OwnershipAsync(entered.CharacterId));
    }

    [Fact]
    public async Task Without_a_live_destination_or_to_the_same_zone_nothing_moves()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await TwoZonesAsync(directory);
        EnteredCharacter entered = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        string before = await OwnershipAsync(entered.CharacterId);

        TransferCharacterResponse nowhere = await directory.TransferAsync(World, entered.CharacterId, entered.Authority, NoSuchZone);
        TransferCharacterResponse same = await directory.TransferAsync(World, entered.CharacterId, entered.Authority, Meadow);

        Assert.Equal(TransferOutcome.NoZoneAvailable, nowhere.Outcome);
        Assert.Equal(TransferOutcome.SameZone, same.Outcome);
        Assert.Equal(before, await OwnershipAsync(entered.CharacterId));
        Assert.Equal(CheckpointOutcome.Saved, (await directory.SaveCheckpointAsync(World, entered.CharacterId, entered.Authority, Meadow, 1, 1, 1)).Outcome);  // still the owner
    }

    [Fact]
    public async Task A_zone_whose_lease_ended_can_neither_save_nor_hand_over_the_character_it_still_names()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, byte[] meadowZone) = await TwoZonesAsync(directory);
        EnteredCharacter entered = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        await Db.ExecuteAsync(postgres.ConnectionString,
            "UPDATE game.zone_leases SET lease_expires_at = now() - interval '1 second' WHERE zone_instance_id = @id", new { id = meadowZone });

        CheckpointOutcome saved = (await directory.SaveCheckpointAsync(World, entered.CharacterId, entered.Authority, Meadow, 1, 1, 1)).Outcome;
        TransferOutcome moved = (await directory.TransferAsync(World, entered.CharacterId, entered.Authority, Highlands)).Outcome;

        Assert.Equal(CheckpointOutcome.StaleAuthority, saved);
        Assert.Equal(TransferOutcome.StaleAuthority, moved);
    }

    [Fact]
    public async Task A_checkpoint_for_another_zone_or_with_a_position_that_is_not_finite_is_refused()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await TwoZonesAsync(directory);
        EnteredCharacter entered = await WorldEntry.EnterAsync(Api, directory, gateway, World);

        CheckpointOutcome elsewhere = (await directory.SaveCheckpointAsync(World, entered.CharacterId, entered.Authority, Highlands, 1, 1, 1)).Outcome;
        RpcException nan = await Assert.ThrowsAsync<RpcException>(() => directory.SaveCheckpointAsync(World, entered.CharacterId, entered.Authority, Meadow, float.NaN, 1, 1));
        RpcException infinite = await Assert.ThrowsAsync<RpcException>(() => directory.TransferAsync(World, entered.CharacterId, entered.Authority, Highlands, x: float.PositiveInfinity));

        Assert.Equal(CheckpointOutcome.StaleAuthority, elsewhere);  // the meadow's instance does not simulate the highlands
        Assert.Equal(StatusCode.InvalidArgument, nan.StatusCode);
        Assert.Equal(StatusCode.InvalidArgument, infinite.StatusCode);
    }

    public void Dispose() => _factory.Dispose();
}

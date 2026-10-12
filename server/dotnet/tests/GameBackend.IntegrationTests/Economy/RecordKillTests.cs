using Google.Protobuf;
using Sa.Backend.V1;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Economy;

/// <summary>Design document 8.2 and 21.1: one grant per kill, only from the zone that owns the killer, only under the agreed content.</summary>
[Collection(SharedPostgres.Name)]
public sealed class RecordKillTests(PostgresFixture postgres) : IDisposable
{
    private const long World = 1;
    private const uint Zone = 1;
    private const uint Slime = 101;
    private const uint SlimeGel = 2001;
    private const uint RustySword = 2101;
    private const uint WolfPelt = 2002;  // allowed for wolves (102), not for slimes
    private const ulong SlimeGold = 3;
    private const uint UnknownMonster = 999;

    private readonly BackendFactory _factory = new(postgres.ConnectionString);

    private ApiClient Api => new(_factory.CreateClient());

    private DirectoryClient Directory => new(_factory);

    private EconomyClient Economy => new(_factory);

    private static string ContentVersion => TestContent.Version;

    [Fact]
    public async Task A_kill_grants_items_and_gold_once_and_a_retry_echoes_the_items_without_granting_again()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter killer = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        RecordKillRequest kill = EconomyClient.KillRequest(World, killer.CharacterId, killer.Authority, Slime, ContentVersion, SlimeGel, RustySword);

        RecordKillResponse first = await economy.RecordKillAsync(kill);
        RecordKillResponse retry = await economy.RecordKillAsync(kill);

        Assert.Equal(KillOutcome.Recorded, first.Outcome);
        Assert.Equal(SlimeGold, first.GoldGranted);
        Assert.Equal(SlimeGold, first.GoldBalance);
        Assert.Equal(new[] { SlimeGel, RustySword }, first.Items.Select(i => i.ItemTemplateId));
        Assert.Equal(KillOutcome.AlreadyRecorded, retry.Outcome);
        Assert.Equal(0UL, retry.GoldGranted);
        Assert.Equal(SlimeGold, retry.GoldBalance);
        // The echo lists the same instances; their order is not part of the contract.
        Assert.Equal(first.Items.Select(i => i.ItemInstanceId.ToBase64()).Order(), retry.Items.Select(i => i.ItemInstanceId.ToBase64()).Order());

        long receipts = await Db.ScalarAsync<long>(postgres.ConnectionString, "SELECT count(*) FROM game.kill_receipts WHERE kill_id = @id", new { id = kill.KillId.ToByteArray() });
        long credits = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.economy_ledger WHERE character_id = @c AND kind = 'currency_credit'", new { c = killer.CharacterId });
        long groundItems = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.item_instances WHERE origin_kill_id = @id AND location = 'ground'", new { id = kill.KillId.ToByteArray() });
        Assert.Equal(1, receipts);
        Assert.Equal(1, credits);
        Assert.Equal(2, groundItems);
    }

    [Fact]
    public async Task A_zone_whose_ownership_epoch_is_stale_grants_nothing()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter killer = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        ZoneAuthority staleAuthority = new(killer.Authority) { OwnerEpoch = killer.Authority.OwnerEpoch + 1 };
        ZoneAuthority otherZone = new(killer.Authority) { ZoneInstanceId = ByteString.CopyFrom(EconomyClient.NewId()) };

        RecordKillResponse wrongEpoch = await economy.RecordKillAsync(EconomyClient.KillRequest(World, killer.CharacterId, staleAuthority, Slime, ContentVersion, SlimeGel));
        RecordKillResponse wrongZone = await economy.RecordKillAsync(EconomyClient.KillRequest(World, killer.CharacterId, otherZone, Slime, ContentVersion, SlimeGel));

        Assert.Equal(KillOutcome.StaleAuthority, wrongEpoch.Outcome);
        Assert.Equal(KillOutcome.StaleAuthority, wrongZone.Outcome);
        GetCharacterAssetsResponse assets = await economy.AssetsAsync(World, killer.CharacterId);
        Assert.Empty(assets.Wallet);
        long receipts = await Db.ScalarAsync<long>(postgres.ConnectionString, "SELECT count(*) FROM game.kill_receipts WHERE killer_character_id = @c", new { c = killer.CharacterId });
        Assert.Equal(0, receipts);
    }

    [Fact]
    public async Task Kills_under_another_content_version_or_with_disallowed_drops_are_refused()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter killer = await WorldEntry.EnterAsync(Api, directory, gateway, World);

        RecordKillResponse otherVersion = await economy.RecordKillAsync(EconomyClient.KillRequest(World, killer.CharacterId, killer.Authority, Slime, ContentVersion + "-other", SlimeGel));
        RecordKillResponse unknownMonster = await economy.RecordKillAsync(EconomyClient.KillRequest(World, killer.CharacterId, killer.Authority, UnknownMonster, ContentVersion));
        RecordKillResponse foreignDrop = await economy.RecordKillAsync(EconomyClient.KillRequest(World, killer.CharacterId, killer.Authority, Slime, ContentVersion, WolfPelt));

        Assert.Equal(KillOutcome.ContentVersionMismatch, otherVersion.Outcome);
        Assert.Equal(KillOutcome.UnknownTemplate, unknownMonster.Outcome);
        Assert.Equal(KillOutcome.UnknownTemplate, foreignDrop.Outcome);
        long receipts = await Db.ScalarAsync<long>(postgres.ConnectionString, "SELECT count(*) FROM game.kill_receipts WHERE killer_character_id = @c", new { c = killer.CharacterId });
        Assert.Equal(0, receipts);
    }

    public void Dispose() => _factory.Dispose();
}

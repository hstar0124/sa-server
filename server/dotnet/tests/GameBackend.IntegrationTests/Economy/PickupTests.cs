using Google.Protobuf;
using Sa.Backend.V1;
using Sa.Events.V1;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Economy;

/// <summary>Scenarios from tests/integration/README.md: concurrent pickup, duplicate request, lost response, id misuse, full inventory.</summary>
[Collection(SharedPostgres.Name)]
public sealed class PickupTests(PostgresFixture postgres) : IDisposable
{
    private const long World = 1;
    private const uint Zone = 1;
    private const uint Slime = 101;
    private const uint SlimeGel = 2001;
    private const uint RustySword = 2101;
    private const int Racers = 8;

    // Each racer registers and logs in; the production credential limit would refuse that many attempts from one address.
    private readonly BackendFactory _factory = new(postgres.ConnectionString, new BackendSettings(CredentialAttemptsPerMinute: Racers * 4));

    private ApiClient Api => new(_factory.CreateClient());

    private DirectoryClient Directory => new(_factory);

    private EconomyClient Economy => new(_factory);

    /// <summary>Drops the given items in front of the character and returns them as the zone would see them.</summary>
    private static async Task<IReadOnlyList<DroppedItem>> DropAsync(EconomyClient economy, EnteredCharacter killer, params uint[] items)
    {
        RecordKillResponse kill = await economy.RecordKillAsync(EconomyClient.KillRequest(World, killer.CharacterId, killer.Authority, Slime, TestContent.Version, items));
        Assert.Equal(KillOutcome.Recorded, kill.Outcome);
        return kill.Items;
    }

    [Fact]
    public async Task Of_many_characters_grabbing_one_item_exactly_one_acquires_it()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter[] racers = await Task.WhenAll(Enumerable.Range(0, Racers).Select(_ => WorldEntry.EnterAsync(Api, directory, gateway, World)));
        DroppedItem item = (await DropAsync(economy, racers[0], SlimeGel))[0];

        PickupItemResponse[] results = await Task.WhenAll(racers.Select(r => economy.PickupAsync(EconomyClient.PickupRequest(World, r.CharacterId, r.Authority, item.ItemInstanceId))));

        Assert.Equal(1, results.Count(r => r.Outcome == PickupOutcome.Acquired));
        Assert.Equal(Racers - 1, results.Count(r => r.Outcome == PickupOutcome.AlreadyTaken));
        long owners = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.item_instances WHERE item_instance_id = @id AND location = 'inventory' AND owner_character_id IS NOT NULL",
            new { id = item.ItemInstanceId.ToByteArray() });
        long acquisitions = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.economy_ledger WHERE item_instance_id = @id AND kind = 'item_acquired'", new { id = item.ItemInstanceId.ToByteArray() });
        Assert.Equal(1, owners);
        Assert.Equal(1, acquisitions);
        long acquiredEvents = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.outbox_events WHERE event_type = @type AND aggregate_id = ANY(@racers)",
            new { type = InventoryItemAcquired.Descriptor.Name, racers = racers.Select(r => r.CharacterId).ToArray() });
        Assert.Equal(1, acquiredEvents);
    }

    [Fact]
    public async Task A_retried_request_returns_the_stored_outcome_and_a_different_body_under_the_same_id_is_refused()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter looter = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        IReadOnlyList<DroppedItem> drops = await DropAsync(economy, looter, SlimeGel, RustySword);
        byte[] requestId = EconomyClient.NewId();

        PickupItemResponse first = await economy.PickupAsync(EconomyClient.PickupRequest(World, looter.CharacterId, looter.Authority, drops[0].ItemInstanceId, requestId));
        PickupItemResponse retried = await economy.PickupAsync(EconomyClient.PickupRequest(World, looter.CharacterId, looter.Authority, drops[0].ItemInstanceId, requestId));
        PickupItemResponse reused = await economy.PickupAsync(EconomyClient.PickupRequest(World, looter.CharacterId, looter.Authority, drops[1].ItemInstanceId, requestId));

        Assert.Equal(PickupOutcome.Acquired, first.Outcome);
        Assert.Equal(first, retried);
        Assert.Equal(PickupOutcome.RequestMismatch, reused.Outcome);
        GetCharacterAssetsResponse assets = await economy.AssetsAsync(World, looter.CharacterId);
        Assert.Single(assets.Items);
        Assert.Equal(first.InventoryVersion, assets.InventoryVersion);
        long stored = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.command_results WHERE request_id = @id", new { id = requestId });
        Assert.Equal(1, stored);

        // Kill then pickup: the character's stream numbers them 1 and 2; the retry and the refused reuse added nothing.
        IReadOnlyList<Db.OutboxRow> outbox = await Db.OutboxOfCharacterAsync(postgres.ConnectionString, World, looter.CharacterId);
        Assert.Equal(new[] { KillRecorded.Descriptor.Name, InventoryItemAcquired.Descriptor.Name }, outbox.Select(o => o.EventType));
        Assert.Equal(new long[] { 1, 2 }, outbox.Select(o => o.AggregateSeq));
        Assert.Equal(requestId, outbox[1].RequestId);
        InventoryItemAcquired acquired = InventoryItemAcquired.Parser.ParseFrom(outbox[1].Payload);
        Assert.Equal(drops[0].ItemInstanceId, acquired.ItemInstanceId);
        Assert.Equal(first.InventoryVersion, acquired.InventoryVersion);
    }

    [Fact]
    public async Task Items_are_only_reachable_through_the_zone_that_owns_the_character()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter looter = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        EnteredCharacter bystander = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        DroppedItem item = (await DropAsync(economy, looter, SlimeGel))[0];
        ZoneAuthority staleAuthority = new(looter.Authority) { OwnerEpoch = looter.Authority.OwnerEpoch + 1 };

        // The looter's zone asking on behalf of a character it does not own (id misuse), and a stale epoch.
        PickupItemResponse misused = await economy.PickupAsync(EconomyClient.PickupRequest(World, bystander.CharacterId, new ZoneAuthority(bystander.Authority) { OwnerEpoch = 99 }, item.ItemInstanceId));
        PickupItemResponse stale = await economy.PickupAsync(EconomyClient.PickupRequest(World, looter.CharacterId, staleAuthority, item.ItemInstanceId));
        PickupItemResponse unknown = await economy.PickupAsync(EconomyClient.PickupRequest(World, looter.CharacterId, looter.Authority, ByteString.CopyFrom(EconomyClient.NewId())));

        Assert.Equal(PickupOutcome.StaleAuthority, misused.Outcome);
        Assert.Equal(PickupOutcome.StaleAuthority, stale.Outcome);
        Assert.Equal(PickupOutcome.UnknownItem, unknown.Outcome);
        string location = await Db.ScalarAsync<string>(postgres.ConnectionString,
            "SELECT location FROM game.item_instances WHERE item_instance_id = @id", new { id = item.ItemInstanceId.ToByteArray() });
        Assert.Equal("ground", location);
    }

    [Fact]
    public async Task A_full_inventory_refuses_the_next_item_and_slots_are_dense()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter looter = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        int capacity = TestContent.InventoryCapacity;
        var ground = new List<DroppedItem>();
        while (ground.Count <= capacity)
        {
            ground.AddRange(await DropAsync(economy, looter, SlimeGel, RustySword));
        }

        var outcomes = new List<PickupOutcome>();
        foreach (DroppedItem item in ground)
        {
            outcomes.Add((await economy.PickupAsync(EconomyClient.PickupRequest(World, looter.CharacterId, looter.Authority, item.ItemInstanceId))).Outcome);
        }

        Assert.Equal(capacity, outcomes.Count(o => o == PickupOutcome.Acquired));
        Assert.Equal(ground.Count - capacity, outcomes.Count(o => o == PickupOutcome.InventoryFull));
        GetCharacterAssetsResponse assets = await economy.AssetsAsync(World, looter.CharacterId);
        Assert.Equal((uint)capacity, assets.InventoryCapacity);
        Assert.Equal(Enumerable.Range(0, capacity).Select(i => (uint)i), assets.Items.Select(i => i.SlotNo));
        // A refusal commits its stored outcome but no event: only acquisitions are facts to publish.
        IReadOnlyList<Db.OutboxRow> outbox = await Db.OutboxOfCharacterAsync(postgres.ConnectionString, World, looter.CharacterId);
        Assert.Equal(capacity, outbox.Count(o => o.EventType == InventoryItemAcquired.Descriptor.Name));
        Assert.Equal(Enumerable.Range(1, outbox.Count).Select(i => (long)i), outbox.Select(o => o.AggregateSeq));
    }

    [Fact]
    public async Task Two_copies_of_one_pickup_request_at_once_take_effect_once()
    {
        using DirectoryClient directory = Directory;
        using EconomyClient economy = Economy;
        (byte[] gateway, _) = await WorldEntry.LiveInfrastructureAsync(directory, World, Zone);
        EnteredCharacter picker = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        DroppedItem item = (await DropAsync(economy, picker, SlimeGel))[0];
        PickupItemRequest request = EconomyClient.PickupRequest(World, picker.CharacterId, picker.Authority, item.ItemInstanceId);

        // A zone retry racing its own first attempt: both carry the same key.
        PickupItemResponse[] answers = await Task.WhenAll(economy.PickupAsync(request), economy.PickupAsync(request));

        Assert.All(answers, a => Assert.Equal(PickupOutcome.Acquired, a.Outcome));
        long acquisitions = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.economy_ledger WHERE item_instance_id = @id AND kind = 'item_acquired'", new { id = item.ItemInstanceId.ToByteArray() });
        int version = await Db.ScalarAsync<int>(postgres.ConnectionString,
            "SELECT version FROM game.inventories WHERE character_id = @c", new { c = picker.CharacterId });
        Assert.Equal(1, acquisitions);
        Assert.Equal(1, version);
        long acquiredEvents = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.outbox_events WHERE event_type = @type AND aggregate_id = @c",
            new { type = InventoryItemAcquired.Descriptor.Name, c = picker.CharacterId });
        Assert.Equal(1, acquiredEvents);
    }

    public void Dispose() => _factory.Dispose();
}

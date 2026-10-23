using Google.Protobuf;
using Sa.Backend.V1;
using Sa.GameBackend.IntegrationTests.Economy;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Trading;

/// <summary>
/// Characters entered through one gateway into one zone instance, stocked through the real kill and
/// pickup path. The gateway and zone are registered once, on the first entry: a second registration
/// of the same zone id would supersede the first and leave the characters entered through it without
/// a live owner.
/// </summary>
internal sealed class TradeWorld(BackendFactory factory) : IDisposable
{
    public const long World = 1;
    public const uint Zone = 1;
    public const uint OtherZone = 2;
    public const uint Slime = 101;
    public const uint SlimeGel = 2001;
    public const uint RustySword = 2101;

    public DirectoryClient Directory { get; } = new(factory);

    public EconomyClient Economy { get; } = new(factory);

    public TradeClient Trade { get; } = new(factory);

    private byte[]? _gateway;

    public async Task<EnteredCharacter> EnterAsync()
    {
        _gateway ??= (await WorldEntry.LiveInfrastructureAsync(Directory, World, Zone)).Gateway;
        return await WorldEntry.EnterAsync(new ApiClient(factory.CreateClient()), Directory, _gateway, World);
    }

    /// <summary>Kills a slime (it grants gold) and picks up the given drops, so the character owns items and gold.</summary>
    public async Task<IReadOnlyList<ByteString>> StockAsync(EnteredCharacter who, params uint[] items)
    {
        RecordKillResponse kill = await Economy.RecordKillAsync(EconomyClient.KillRequest(World, who.CharacterId, who.Authority, Slime, TestContent.Version, items));
        Assert.Equal(KillOutcome.Recorded, kill.Outcome);
        var owned = new List<ByteString>();
        foreach (DroppedItem drop in kill.Items)
        {
            PickupItemResponse pickup = await Economy.PickupAsync(EconomyClient.PickupRequest(World, who.CharacterId, who.Authority, drop.ItemInstanceId));
            Assert.Equal(PickupOutcome.Acquired, pickup.Outcome);
            owned.Add(drop.ItemInstanceId);
        }

        return owned;
    }

    /// <summary>Picks up until every slot of the inventory is taken.</summary>
    public async Task FillInventoryAsync(EnteredCharacter who)
    {
        int held = (await AssetsAsync(who)).Items.Count;
        while (held < TestContent.InventoryCapacity)
        {
            uint[] drops = TestContent.InventoryCapacity - held > 1 ? [SlimeGel, RustySword] : [SlimeGel];
            held += (await StockAsync(who, drops)).Count;
        }
    }

    public async Task<GetCharacterAssetsResponse> AssetsAsync(EnteredCharacter who) => await Economy.AssetsAsync(World, who.CharacterId);

    public async Task<ulong> GoldOfAsync(EnteredCharacter who)
        => (await AssetsAsync(who)).Wallet.SingleOrDefault(w => w.Currency == TestContent.PrimaryCurrency)?.Balance ?? 0;

    public void Dispose()
    {
        Directory.Dispose();
        Economy.Dispose();
        Trade.Dispose();
    }
}

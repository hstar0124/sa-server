using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.WorldDirectory;

/// <summary>
/// One live instance per zone id (issue 013): a restarted zone process is a new instance, it takes
/// over the zone, and the instance it replaced may neither receive entries nor renew.
/// </summary>
[Collection(SharedPostgres.Name)]
public sealed class ZoneInstanceTests(PostgresFixture postgres) : IDisposable
{
    private const long World = 1;
    private const uint Zone = 1;

    private readonly BackendFactory _factory = new(postgres.ConnectionString);

    private ApiClient Api => new(_factory.CreateClient());

    private DirectoryClient Directory => new(_factory);

    [Fact]
    public async Task A_restarted_zone_takes_over_and_the_instance_it_replaced_may_not_renew()
    {
        using DirectoryClient directory = Directory;
        byte[] gateway = DirectoryClient.NewInstanceId();
        byte[] before = DirectoryClient.NewInstanceId();
        byte[] restarted = DirectoryClient.NewInstanceId();
        Assert.True((await directory.RenewGatewayAsync(gateway)).Renewed);
        Assert.True((await directory.RenewZoneAsync(before, World, Zone)).Renewed);

        // The process restarts within the old lease: the new instance registers while the old lease still runs.
        Assert.True((await directory.RenewZoneAsync(restarted, World, Zone)).Renewed);

        EnteredCharacter entered = await WorldEntry.EnterAsync(Api, directory, gateway, World);
        Assert.Equal(restarted, entered.Grant.Zone.ZoneInstanceId.ToByteArray());
        Assert.False((await directory.RenewZoneAsync(before, World, Zone)).Renewed);
        bool oldAlive = await Db.ScalarAsync<bool>(postgres.ConnectionString,
            "SELECT lease_expires_at > now() FROM game.zone_leases WHERE zone_instance_id = @id", new { id = before });
        Assert.False(oldAlive);
    }

    [Fact]
    public async Task A_zone_that_lost_its_lease_alone_may_renew_again()
    {
        using DirectoryClient directory = Directory;
        byte[] zone = DirectoryClient.NewInstanceId();
        Assert.True((await directory.RenewZoneAsync(zone, World, Zone)).Renewed);
        await Db.ExecuteAsync(postgres.ConnectionString,
            "UPDATE game.zone_leases SET lease_expires_at = now() - interval '1 second' WHERE zone_instance_id = @id", new { id = zone });

        // A stall that outlived the lease with no replacement: refusing would leave the zone dead.
        Assert.True((await directory.RenewZoneAsync(zone, World, Zone)).Renewed);
    }

    public void Dispose() => _factory.Dispose();
}

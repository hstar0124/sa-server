using Sa.Backend.V1;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>A character that has entered the world: the zone that owns it and the epoch of that ownership.</summary>
internal sealed record EnteredCharacter(long CharacterId, RedeemEntryTicketResponse Grant)
{
    public ZoneAuthority Authority => EconomyClient.AuthorityOf(Grant);
}

/// <summary>Walks the real entry path (register, create, enter, redeem) so economy tests start from a legitimately owned character.</summary>
internal static class WorldEntry
{
    /// <summary>Registers a live gateway and zone exactly as the deployed processes would.</summary>
    public static async Task<(byte[] Gateway, byte[] Zone)> LiveInfrastructureAsync(DirectoryClient directory, long worldId, uint zoneId)
    {
        byte[] gateway = DirectoryClient.NewInstanceId();
        byte[] zone = DirectoryClient.NewInstanceId();
        Assert.True((await directory.RenewGatewayAsync(gateway)).Renewed);
        Assert.True((await directory.RenewZoneAsync(zone, worldId, zoneId)).Renewed);
        return (gateway, zone);
    }

    public static async Task<EnteredCharacter> EnterAsync(ApiClient api, DirectoryClient directory, byte[] gateway, long worldId)
    {
        ApiClient owner = await api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket, long characterId) = await owner.CreateCharacterAndEnterAsync(worldId, Unique.CharacterName());
        RedeemEntryTicketResponse grant = await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId());
        Assert.Equal(RedeemOutcome.Granted, grant.Outcome);
        return new EnteredCharacter(characterId, grant);
    }
}

using System.Net.Http.Json;
using Dapper;
using Npgsql;
using Sa.Backend.V1;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.WorldDirectory;

/// <summary>see README "네트워크와 입장": one ticket, one live session, one owning zone.</summary>
[Collection(SharedPostgres.Name)]
public sealed class RedeemTests(PostgresFixture postgres) : IDisposable
{
    private const long World = 1;
    private const long WorldWithoutZones = 2;  // seeded in 0003; no zone process ever leases it
    private const uint Zone = 1;

    private readonly BackendFactory _factory = new(postgres.ConnectionString);

    private ApiClient Api => new(_factory.CreateClient());

    private DirectoryClient Directory => new(_factory);

    /// <summary>A live gateway and a live zone, as the deployed processes would have registered.</summary>
    private static async Task<(byte[] Gateway, byte[] Zone)> LiveInfrastructureAsync(DirectoryClient directory)
    {
        byte[] gateway = DirectoryClient.NewInstanceId();
        byte[] zone = DirectoryClient.NewInstanceId();
        Assert.True((await directory.RenewGatewayAsync(gateway)).Renewed);
        Assert.True((await directory.RenewZoneAsync(zone, World, Zone)).Renewed);
        return (gateway, zone);
    }

    [Fact]
    public async Task First_redeem_is_granted_second_is_already_redeemed()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await LiveInfrastructureAsync(directory);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        string name = Unique.CharacterName();
        (byte[] ticket, long characterId) = await owner.CreateCharacterAndEnterAsync(World, name);
        byte[] session = DirectoryClient.NewInstanceId();

        RedeemEntryTicketResponse first = await directory.RedeemAsync(ticket, gateway, session);
        RedeemEntryTicketResponse second = await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId());

        Assert.Equal(RedeemOutcome.Granted, first.Outcome);
        Assert.Equal(1UL, first.OwnerEpoch);
        Assert.Equal(Zone, first.Zone.ZoneId);
        // Any zone with a live lease for the world may be chosen (other tests lease zones too).
        bool assignedZoneIsLive = await Db.ScalarAsync<bool>(postgres.ConnectionString,
            "SELECT lease_expires_at > now() FROM game.zone_leases WHERE zone_instance_id = @id AND world_id = @World",
            new { id = first.Zone.ZoneInstanceId.ToByteArray(), World });
        Assert.True(assignedZoneIsLive);
        Assert.Equal(name, first.Character.Name);
        Assert.Equal(RedeemOutcome.AlreadyRedeemed, second.Outcome);

        long liveSessions = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.character_sessions WHERE character_id = @characterId AND ended_at IS NULL", new { characterId });
        Assert.Equal(1, liveSessions);
        string state = await Db.ScalarAsync<string>(postgres.ConnectionString,
            "SELECT state FROM game.character_ownership WHERE world_id = @World AND character_id = @characterId", new { World, characterId });
        Assert.Equal("active", state);
    }

    [Fact]
    public async Task Unknown_gateway_is_refused_before_the_ticket_is_consumed()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await LiveInfrastructureAsync(directory);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket, _) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());
        byte[] unknownGateway = DirectoryClient.NewInstanceId();

        RedeemEntryTicketResponse refused = await directory.RedeemAsync(ticket, unknownGateway, DirectoryClient.NewInstanceId());
        RedeemEntryTicketResponse retried = await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId());

        Assert.Equal(RedeemOutcome.UnknownGateway, refused.Outcome);
        Assert.Equal(RedeemOutcome.Granted, retried.Outcome);
    }

    [Fact]
    public async Task No_live_zone_means_no_grant_but_the_ticket_is_still_consumed()
    {
        using DirectoryClient directory = Directory;
        byte[] gateway = DirectoryClient.NewInstanceId();
        await directory.RenewGatewayAsync(gateway);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket, long characterId) = await owner.CreateCharacterAndEnterAsync(WorldWithoutZones, Unique.CharacterName());

        RedeemEntryTicketResponse refused = await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId());

        Assert.Equal(RedeemOutcome.NoZoneAvailable, refused.Outcome);
        Assert.Equal(RedeemOutcome.AlreadyRedeemed, (await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId())).Outcome);
        long sessions = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.character_sessions WHERE character_id = @characterId", new { characterId });
        Assert.Equal(0, sessions);
    }

    [Fact]
    public async Task A_live_session_blocks_a_second_one_until_it_ends_then_the_epoch_advances()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await LiveInfrastructureAsync(directory);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket1, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());
        byte[] session1 = DirectoryClient.NewInstanceId();
        Assert.Equal(RedeemOutcome.Granted, (await directory.RedeemAsync(ticket1, gateway, session1)).Outcome);

        byte[] ticket2 = await IssueAnotherTicketAsync(owner, characterId);
        RedeemEntryTicketResponse blocked = await directory.RedeemAsync(ticket2, gateway, DirectoryClient.NewInstanceId());
        Assert.Equal(RedeemOutcome.ActiveSessionExists, blocked.Outcome);

        Assert.True((await directory.EndSessionAsync(session1, "client_disconnected")).Ended);
        Assert.False((await directory.EndSessionAsync(session1)).Ended);

        byte[] ticket3 = await IssueAnotherTicketAsync(owner, characterId);
        RedeemEntryTicketResponse again = await directory.RedeemAsync(ticket3, gateway, DirectoryClient.NewInstanceId());
        Assert.Equal(RedeemOutcome.Granted, again.Outcome);
        Assert.Equal(2UL, again.OwnerEpoch);
    }

    [Fact]
    public async Task Request_that_names_a_different_character_than_the_ticket_is_refused_and_consumes_the_ticket()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await LiveInfrastructureAsync(directory);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());

        RedeemEntryTicketResponse refused = await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId(),
            tamper: r => r.CharacterId = (ulong)characterId + 1);

        Assert.Equal(RedeemOutcome.CharacterNotOwned, refused.Outcome);
        Assert.Equal(RedeemOutcome.AlreadyRedeemed, (await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId())).Outcome);
    }

    [Fact]
    public async Task Two_gateways_redeeming_two_tickets_for_one_character_concurrently_yield_one_grant()
    {
        using DirectoryClient directory = Directory;
        (byte[] gatewayA, _) = await LiveInfrastructureAsync(directory);
        byte[] gatewayB = DirectoryClient.NewInstanceId();
        await directory.RenewGatewayAsync(gatewayB);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticketA, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());
        byte[] ticketB = await IssueAnotherTicketAsync(owner, characterId);

        const int rounds = 5;
        int grants = 0;
        for (int i = 0; i < rounds; i++)
        {
            byte[] a = i == 0 ? ticketA : await IssueAnotherTicketAsync(owner, characterId);
            byte[] b = i == 0 ? ticketB : await IssueAnotherTicketAsync(owner, characterId);
            byte[] sessionA = DirectoryClient.NewInstanceId();
            byte[] sessionB = DirectoryClient.NewInstanceId();
            RedeemEntryTicketResponse[] results = await Task.WhenAll(
                directory.RedeemAsync(a, gatewayA, sessionA),
                directory.RedeemAsync(b, gatewayB, sessionB));

            int granted = results.Count(r => r.Outcome == RedeemOutcome.Granted);
            Assert.Equal(1, granted);
            Assert.Equal(1, results.Count(r => r.Outcome == RedeemOutcome.ActiveSessionExists));
            grants += granted;
            await directory.EndSessionAsync(results[0].Outcome == RedeemOutcome.Granted ? sessionA : sessionB);
        }

        Assert.Equal(rounds, grants);
        long live = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.character_sessions WHERE character_id = @characterId AND ended_at IS NULL", new { characterId });
        Assert.Equal(0, live);
    }

    [Fact]
    public async Task Sessions_of_a_gateway_whose_lease_lapsed_are_reaped_on_the_next_redeem()
    {
        using var shortLease = new BackendFactory(postgres.ConnectionString, new BackendSettings(GatewayLeaseDuration: TimeSpan.FromSeconds(1)));
        using var directory = new DirectoryClient(shortLease);
        byte[] dyingGateway = DirectoryClient.NewInstanceId();
        byte[] zone = DirectoryClient.NewInstanceId();
        await directory.RenewGatewayAsync(dyingGateway);
        await directory.RenewZoneAsync(zone, World, Zone);
        var api = new ApiClient(shortLease.CreateClient());
        ApiClient owner = await api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket1, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());
        byte[] staleSession = DirectoryClient.NewInstanceId();
        Assert.Equal(RedeemOutcome.Granted, (await directory.RedeemAsync(ticket1, dyingGateway, staleSession)).Outcome);

        await Task.Delay(TimeSpan.FromSeconds(1.2));  // the gateway never renewed
        byte[] healthyGateway = DirectoryClient.NewInstanceId();
        await directory.RenewGatewayAsync(healthyGateway);
        byte[] ticket2 = await IssueAnotherTicketAsync(owner, characterId);

        RedeemEntryTicketResponse result = await directory.RedeemAsync(ticket2, healthyGateway, DirectoryClient.NewInstanceId());

        Assert.Equal(RedeemOutcome.Granted, result.Outcome);
        string reason = await Db.ScalarAsync<string>(postgres.ConnectionString,
            "SELECT end_reason FROM game.character_sessions WHERE gateway_session_id = @id", new { id = staleSession });
        Assert.Equal("gateway_lease_expired", reason);
    }

    [Fact]
    public async Task A_zone_instance_cannot_renew_under_a_different_zone_id()
    {
        using DirectoryClient directory = Directory;
        byte[] zone = DirectoryClient.NewInstanceId();
        Assert.True((await directory.RenewZoneAsync(zone, World, Zone)).Renewed);

        RenewZoneLeaseResponse moved = await directory.RenewZoneAsync(zone, World, Zone + 1);

        Assert.False(moved.Renewed);
    }

    private static async Task<byte[]> IssueAnotherTicketAsync(ApiClient owner, long characterId)
    {
        HttpResponseMessage enter = await owner.EnterAsync(World, characterId);
        enter.EnsureSuccessStatusCode();
        return Convert.FromBase64String((await enter.Content.ReadFromJsonAsync<ApiClient.EnterResponse>())!.EntryTicket);
    }

    [Fact]
    public async Task Two_gateways_redeeming_the_same_ticket_at_once_admit_exactly_one()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await LiveInfrastructureAsync(directory);
        byte[] otherGateway = DirectoryClient.NewInstanceId();
        Assert.True((await directory.RenewGatewayAsync(otherGateway)).Renewed);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());

        RedeemEntryTicketResponse[] answers = await Task.WhenAll(
            directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId()),
            directory.RedeemAsync(ticket, otherGateway, DirectoryClient.NewInstanceId()));

        Assert.Single(answers, a => a.Outcome == RedeemOutcome.Granted);
        Assert.Single(answers, a => a.Outcome == RedeemOutcome.AlreadyRedeemed);
        long live = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.character_sessions WHERE character_id = @c AND ended_at IS NULL", new { c = characterId });
        Assert.Equal(1, live);
    }

    [Fact]
    public async Task An_expired_ticket_is_refused_and_creates_nothing()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await LiveInfrastructureAsync(directory);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());
        await Db.ExecuteAsync(postgres.ConnectionString,
            "UPDATE game.entry_tickets SET expires_at = now() - interval '1 second' WHERE character_id = @c", new { c = characterId });

        RedeemEntryTicketResponse answer = await directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId());

        Assert.Equal(RedeemOutcome.Expired, answer.Outcome);
        long sessions = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.character_sessions WHERE character_id = @c", new { c = characterId });
        Assert.Equal(0, sessions);
    }

    [Fact]
    public async Task A_redeem_that_loses_the_race_at_the_unique_index_spends_its_ticket_and_moves_no_ownership()
    {
        using DirectoryClient directory = Directory;
        (byte[] gateway, _) = await LiveInfrastructureAsync(directory);
        byte[] otherGateway = DirectoryClient.NewInstanceId();
        Assert.True((await directory.RenewGatewayAsync(otherGateway)).Renewed);
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        (byte[] ticket, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());

        // Another gateway's session for the same character, inserted but not committed yet: the redeem
        // passes the existence check, then waits on the unique index until the rival commits.
        await using var rival = new NpgsqlConnection(postgres.ConnectionString);
        await rival.OpenAsync();
        await using NpgsqlTransaction rivalTx = await rival.BeginTransactionAsync();
        await rival.ExecuteAsync("""
            INSERT INTO game.character_sessions
                (gateway_session_id, gateway_instance_id, world_id, character_id, account_id, owner_epoch)
            SELECT @Session, @Gateway, world_id, character_id, account_id, 0
            FROM game.characters WHERE character_id = @CharacterId
            """, new { Session = DirectoryClient.NewInstanceId(), Gateway = otherGateway, CharacterId = characterId }, rivalTx);

        Task<RedeemEntryTicketResponse> redeem = directory.RedeemAsync(ticket, gateway, DirectoryClient.NewInstanceId());
        await Db.WaitForLockWaitAsync(postgres.ConnectionString);
        await rivalTx.CommitAsync();

        Assert.Equal(RedeemOutcome.ActiveSessionExists, (await redeem).Outcome);
        long unspent = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.entry_tickets WHERE character_id = @c AND redeemed_at IS NULL", new { c = characterId });
        long ownership = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.character_ownership WHERE character_id = @c", new { c = characterId });
        Assert.Equal(0, unspent);
        Assert.Equal(0, ownership);
    }

    public void Dispose() => _factory.Dispose();
}

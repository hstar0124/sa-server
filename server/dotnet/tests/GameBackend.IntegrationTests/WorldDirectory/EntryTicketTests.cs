using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using Sa.Backend.V1;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.WorldDirectory;

[Collection(SharedPostgres.Name)]
public sealed class EntryTicketTests(PostgresFixture postgres) : IDisposable
{
    private const long World = 1;

    private readonly BackendFactory _factory = new(postgres.ConnectionString);

    private ApiClient Api => new(_factory.CreateClient());

    [Fact]
    public async Task Enter_issues_a_ticket_whose_signature_verifies_and_whose_row_is_unredeemed()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());

        (byte[] signedBytes, long characterId) = await owner.CreateCharacterAndEnterAsync(World, Unique.CharacterName());

        SignedEntryTicket signed = SignedEntryTicket.Parser.ParseFrom(signedBytes);
        byte[] expected = HMACSHA256.HashData(_factory.TicketSigningKey, signed.Ticket.ToByteArray());
        Assert.Equal(expected, signed.Signature.ToByteArray());

        EntryTicket ticket = EntryTicket.Parser.ParseFrom(signed.Ticket);
        Assert.Equal((ulong)characterId, ticket.CharacterId);
        Assert.Equal((ulong)World, ticket.WorldId);
        Assert.True(ticket.ExpiresAtMs > ticket.IssuedAtMs);

        bool redeemed = await Db.ScalarAsync<bool>(postgres.ConnectionString,
            "SELECT redeemed_at IS NOT NULL FROM game.entry_tickets WHERE ticket_id = @id", new { id = ticket.TicketId.ToByteArray() });
        Assert.False(redeemed);
    }

    [Fact]
    public async Task Enter_for_someone_elses_character_is_404_and_issues_nothing()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        ApiClient intruder = await Api.AsNewAccountAsync(Unique.Username());
        HttpResponseMessage created = await owner.CreateCharacterAsync(World, Unique.CharacterName());
        long characterId = (await created.Content.ReadFromJsonAsync<ApiClient.CharacterResponse>())!.CharacterId;

        HttpResponseMessage response = await intruder.EnterAsync(World, characterId);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        long tickets = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.entry_tickets WHERE character_id = @characterId", new { characterId });
        Assert.Equal(0, tickets);
    }

    public void Dispose() => _factory.Dispose();
}

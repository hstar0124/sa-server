using System.Net.Http.Json;
using Npgsql;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Database;

/// <summary>
/// The database keeps the consistency rules on its own (migration 0005): a write that breaks one
/// fails with the named constraint even when it bypasses the Backend's code.
/// </summary>
[Collection(SharedPostgres.Name)]
public sealed class ConstraintTests(PostgresFixture postgres) : IDisposable
{
    private const long World = 1;
    private const long OtherWorld = 2;  // seeded in 0003

    private readonly BackendFactory _factory = new(postgres.ConnectionString);

    private ApiClient Api => new(_factory.CreateClient());

    private async Task<(long AccountId, long CharacterId)> CharacterAsync()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        ApiClient.CharacterResponse created = await (await owner.CreateCharacterAsync(World, Unique.CharacterName())).Content
            .ReadFromJsonAsync<ApiClient.CharacterResponse>() ?? throw new InvalidOperationException("no character in the response");
        long account = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT account_id FROM game.characters WHERE character_id = @c", new { c = created.CharacterId });
        return (account, created.CharacterId);
    }

    private async Task<string?> ViolationAsync(string sql, object parameters)
    {
        PostgresException refused = await Assert.ThrowsAsync<PostgresException>(() => Db.ExecuteAsync(postgres.ConnectionString, sql, parameters));
        return refused.ConstraintName;
    }

    private const string InsertTicketSql = """
        INSERT INTO game.entry_tickets (ticket_id, account_id, character_id, world_id, expires_at)
        VALUES (@Ticket, @Account, @Character, @World, now() + interval '30 seconds')
        """;

    [Fact]
    public async Task A_ticket_must_name_the_characters_own_account_and_world()
    {
        (long account, long character) = await CharacterAsync();
        (long otherAccount, _) = await CharacterAsync();

        Assert.Equal("entry_tickets_character_account_fkey", await ViolationAsync(InsertTicketSql,
            new { Ticket = Guid.NewGuid().ToByteArray(), Account = otherAccount, Character = character, World }));
        Assert.Equal("entry_tickets_world_character_fkey", await ViolationAsync(InsertTicketSql,
            new { Ticket = Guid.NewGuid().ToByteArray(), Account = account, Character = character, World = OtherWorld }));
    }

    [Fact]
    public async Task An_inventory_slot_is_never_negative()
    {
        (_, long character) = await CharacterAsync();
        Assert.Equal("item_instances_slot_non_negative", await ViolationAsync("""
            INSERT INTO game.item_instances (item_instance_id, world_id, item_template_id, location, owner_character_id, slot_no)
            VALUES (@Id, @World, 1, 'inventory', @Character, -1)
            """, new { Id = Guid.NewGuid().ToByteArray(), World, Character = character }));
    }

    [Fact]
    public async Task A_ledger_row_is_a_whole_currency_or_item_movement()
    {
        (_, long character) = await CharacterAsync();
        Assert.Equal("economy_ledger_shape", await ViolationAsync("""
            INSERT INTO game.economy_ledger (world_id, character_id, kind, currency, amount, reason)
            VALUES (@World, @Character, 'currency_credit', 'gold', NULL, 'kill')
            """, new { World, Character = character }));
        Assert.Equal("economy_ledger_shape", await ViolationAsync("""
            INSERT INTO game.economy_ledger (world_id, character_id, kind, currency, amount, reason)
            VALUES (@World, @Character, 'currency_credit', 'gold', -5, 'kill')
            """, new { World, Character = character }));
    }

    public void Dispose() => _factory.Dispose();
}

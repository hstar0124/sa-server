using System.Data;
using Dapper;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Characters;

internal enum CreateCharacterOutcome
{
    Created,
    NameTaken,
    LimitReached,
    UnknownWorld,
}

internal sealed record CreateCharacterResult(CreateCharacterOutcome Outcome, Character? Character);

internal sealed class CharacterRepository(NpgsqlDataSource dataSource)
{
    private const string NameUniqueIndex = "characters_name_unique";
    private const string WorldForeignKey = "characters_world_id_fkey";

    private const string SelectColumns = """
        character_id AS "Id", world_id AS "WorldId", account_id AS "AccountId", name AS "Name", class_id AS "ClassId"
        """;

    private const string ListSql = $"""
        SELECT {SelectColumns}
        FROM game.characters
        WHERE account_id = @AccountId AND world_id = @WorldId
        ORDER BY character_id
        """;

    // Serialises concurrent creations for one account so the per-world limit cannot be exceeded by
    // a burst of parallel requests. The lock is released at commit.
    private const string LockAccountSql = """
        SELECT account_id FROM game.accounts WHERE account_id = @AccountId FOR UPDATE
        """;

    private const string CountSql = """
        SELECT count(*) FROM game.characters WHERE account_id = @AccountId AND world_id = @WorldId
        """;

    private const string InsertSql = $"""
        INSERT INTO game.characters (world_id, account_id, name, class_id)
        VALUES (@WorldId, @AccountId, @Name, @ClassId)
        RETURNING {SelectColumns}
        """;

    private const string OwnedSql = """
        SELECT 1 FROM game.characters
        WHERE character_id = @CharacterId AND account_id = @AccountId AND world_id = @WorldId
        """;

    public async Task<bool> IsOwnedByAsync(CharacterId characterId, AccountId accountId, WorldId worldId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        return await connection.ExecuteScalarAsync<int?>(
            new CommandDefinition(OwnedSql, new { CharacterId = characterId, AccountId = accountId, WorldId = worldId }, cancellationToken: cancellationToken)) is not null;
    }

    public async Task<IReadOnlyList<Character>> ListAsync(AccountId accountId, WorldId worldId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        IEnumerable<Character> rows = await connection.QueryAsync<Character>(
            new CommandDefinition(ListSql, new { AccountId = accountId, WorldId = worldId }, cancellationToken: cancellationToken));
        return rows.ToList();
    }

    public async Task<CreateCharacterResult> CreateAsync(
        AccountId accountId, WorldId worldId, string name, ClassId classId, int maxPerWorld, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, cancellationToken);

        var scope = new { AccountId = accountId, WorldId = worldId };
        await connection.ExecuteAsync(new CommandDefinition(LockAccountSql, scope, transaction, cancellationToken: cancellationToken));

        long existing = await connection.ExecuteScalarAsync<long>(new CommandDefinition(CountSql, scope, transaction, cancellationToken: cancellationToken));
        if (existing >= maxPerWorld)
        {
            return new CreateCharacterResult(CreateCharacterOutcome.LimitReached, null);
        }

        try
        {
            Character created = await connection.QuerySingleAsync<Character>(new CommandDefinition(
                InsertSql, new { WorldId = worldId, AccountId = accountId, Name = name, ClassId = classId }, transaction, cancellationToken: cancellationToken));
            await transaction.CommitAsync(cancellationToken);
            return new CreateCharacterResult(CreateCharacterOutcome.Created, created);
        }
        catch (PostgresException ex) when (ex.IsUniqueViolation(NameUniqueIndex))
        {
            return new CreateCharacterResult(CreateCharacterOutcome.NameTaken, null);
        }
        catch (PostgresException ex) when (ex.IsForeignKeyViolation(WorldForeignKey))
        {
            return new CreateCharacterResult(CreateCharacterOutcome.UnknownWorld, null);
        }
    }
}

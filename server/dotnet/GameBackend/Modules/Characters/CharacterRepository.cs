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


    // One statement checks ownership and reads the read model: no row means not this account's
    // character; a character without events yet reads as zeros.
    private const string StatsSql = """
        SELECT coalesce(p.kills, 0) AS "Kills", coalesce(p.items_acquired, 0) AS "ItemsAcquired",
               coalesce(p.gold_earned, 0) AS "GoldEarned",
               coalesce(p.trades_completed, 0) AS "TradesCompleted", coalesce(p.last_event_seq, 0) AS "AsOfEventSeq"
        FROM game.characters c
        LEFT JOIN game.character_projections p ON p.world_id = c.world_id AND p.character_id = c.character_id
        WHERE c.character_id = @CharacterId AND c.account_id = @AccountId AND c.world_id = @WorldId
        """;

    /// <summary>Null when the account has no such character in the world: the caller answers "not found" either way.</summary>
    public async Task<CharacterStats?> ReadStatsAsync(CharacterId characterId, AccountId accountId, WorldId worldId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        return await connection.QuerySingleOrDefaultAsync<CharacterStats>(new CommandDefinition(
            StatsSql, new { CharacterId = characterId, AccountId = accountId, WorldId = worldId }, cancellationToken: cancellationToken));
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

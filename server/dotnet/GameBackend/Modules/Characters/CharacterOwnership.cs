using Dapper;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Characters;

/// <summary>
/// What other modules may ask about characters: whether an account owns one in a world. The rest of
/// the module (creation, listing, its repository) stays internal to it.
/// </summary>
internal sealed class CharacterOwnership(NpgsqlDataSource dataSource)
{
    private const string OwnedSql = """
        SELECT 1 FROM game.characters
        WHERE character_id = @CharacterId AND account_id = @AccountId AND world_id = @WorldId
        """;

    /// <summary>A predicate, not a result: the caller answers "unknown character" either way.</summary>
    public async Task<bool> IsOwnedByAsync(CharacterId characterId, AccountId accountId, WorldId worldId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        return await connection.ExecuteScalarAsync<int?>(
            new CommandDefinition(OwnedSql, new { CharacterId = characterId, AccountId = accountId, WorldId = worldId }, cancellationToken: cancellationToken)) is not null;
    }
}

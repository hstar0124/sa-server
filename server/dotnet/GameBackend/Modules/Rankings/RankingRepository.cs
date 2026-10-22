using Dapper;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Rankings;

internal sealed record RankingEntry(long Rank, CharacterId CharacterId, string Name, long Kills);

internal sealed record CharacterRank(long Rank, long Kills);

/// <summary>
/// Kill ranking over the character statistics read model (<c>character_projections</c>, kept by
/// the EventWorker from confirmed events). Ties break by the lower character id, so ranks are total
/// and stable. Both queries walk the (world, kills, character) index.
/// </summary>
internal sealed class RankingRepository(NpgsqlDataSource dataSource)
{
    private const string TopSql = """
        SELECT row_number() OVER (ORDER BY p.kills DESC, p.character_id) AS "Rank",
               p.character_id AS "CharacterId", c.name AS "Name", p.kills AS "Kills"
        FROM game.character_projections p
        JOIN game.characters c ON c.world_id = p.world_id AND c.character_id = p.character_id
        WHERE p.world_id = @WorldId
        ORDER BY p.kills DESC, p.character_id
        LIMIT @Top
        """;

    // Rank = 1 + the number of characters strictly ahead. It counts the index range before this
    // character, so the cost grows with how far down the character sits. GROUP BY makes a character
    // without a row return no row: an aggregate over nothing would still answer "rank 1".
    private const string RankSql = """
        WITH mine AS (
            SELECT kills FROM game.character_projections WHERE world_id = @WorldId AND character_id = @CharacterId
        )
        SELECT 1 + count(p.character_id) AS "Rank", mine.kills AS "Kills"
        FROM mine
        LEFT JOIN game.character_projections p
            ON p.world_id = @WorldId
           AND (p.kills > mine.kills OR (p.kills = mine.kills AND p.character_id < @CharacterId))
        GROUP BY mine.kills
        """;

    public async Task<IReadOnlyList<RankingEntry>> TopAsync(WorldId worldId, int top, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        IEnumerable<RankingEntry> rows = await connection.QueryAsync<RankingEntry>(
            new CommandDefinition(TopSql, new { WorldId = worldId, Top = top }, cancellationToken: cancellationToken));
        return rows.ToList();
    }

    /// <summary>Null when the character has no statistics row yet: nothing recorded, so no rank.</summary>
    public async Task<CharacterRank?> RankOfAsync(WorldId worldId, CharacterId characterId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        return await connection.QuerySingleOrDefaultAsync<CharacterRank>(
            new CommandDefinition(RankSql, new { WorldId = worldId, CharacterId = characterId }, cancellationToken: cancellationToken));
    }
}

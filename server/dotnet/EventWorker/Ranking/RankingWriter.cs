using System.Collections.Concurrent;
using Dapper;
using Npgsql;
using Sa.Shared.Ranking;
using StackExchange.Redis;

namespace Sa.EventWorker.Ranking;

/// <summary>
/// Keeps the Redis kill ranking in step with <c>character_projections</c>, its source of truth.
/// Scores are absolute (the character's total, never "+1") and written with GT, so a late write
/// carrying an older total cannot lower a newer one. That relies on kill totals only growing,
/// which they do: the read model only adds.
/// </summary>
internal sealed partial class RankingWriter(IConnectionMultiplexer redis, NpgsqlDataSource dataSource, ILogger<RankingWriter> logger)
{
    private const string WorldsSql = "SELECT world_id FROM game.worlds ORDER BY world_id";

    private const string StatisticsPageSql = """
        SELECT character_id AS "CharacterId", kills AS "Kills"
        FROM game.character_projections
        WHERE world_id = @WorldId AND character_id > @After
        ORDER BY character_id
        LIMIT @Batch
        """;

    // Worlds whose ranking missed a write; the reconciler refills them.
    private readonly ConcurrentDictionary<long, bool> _behind = new();

    public async Task UpdateAsync(long worldId, long characterId, long kills)
    {
        IDatabase db = redis.GetDatabase();
        await db.SortedSetAddAsync(RankingKeys.KillsKey(worldId), RankingKeys.Member(characterId), RankingKeys.Score(kills), SortedSetWhen.GreaterThan);
    }

    /// <summary>Records that a write to this world's ranking was lost.</summary>
    public void MarkBehind(long worldId) => _behind[worldId] = true;

    /// <summary>True once per lost write, then false until the next one.</summary>
    public bool TakeBehind(long worldId) => _behind.TryRemove(worldId, out _);

    public async Task<bool> ExistsAsync(long worldId) => await redis.GetDatabase().KeyExistsAsync(RankingKeys.KillsKey(worldId));

    public async Task<IReadOnlyList<long>> WorldsAsync(CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        return (await connection.QueryAsync<long>(new CommandDefinition(WorldsSql, cancellationToken: ct))).ToList();
    }

    /// <summary>
    /// Writes every statistics row of the world into the sorted set, page by page in id order, with
    /// GT so a concurrent live write is never undone by an older page. Returns the members written.
    /// </summary>
    public async Task<long> RefillAsync(long worldId, int batchSize, CancellationToken ct)
    {
        IDatabase db = redis.GetDatabase();
        RedisKey key = RankingKeys.KillsKey(worldId);
        long after = 0;
        long written = 0;
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        while (true)
        {
            List<StatisticsRow> page = (await connection.QueryAsync<StatisticsRow>(new CommandDefinition(
                StatisticsPageSql, new { WorldId = worldId, After = after, Batch = batchSize }, cancellationToken: ct))).ToList();
            if (page.Count == 0)
            {
                break;
            }

            SortedSetEntry[] entries = page.Select(r => new SortedSetEntry(RankingKeys.Member(r.CharacterId), RankingKeys.Score(r.Kills))).ToArray();
            await db.SortedSetAddAsync(key, entries, SortedSetWhen.GreaterThan);
            written += entries.Length;
            after = page[^1].CharacterId;
        }

        if (written > 0)
        {
            LogRefilled(worldId, written);
        }

        return written;
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "ranking of world {WorldId} refilled from the statistics: {Members} members")]
    private partial void LogRefilled(long worldId, long members);

    private sealed record StatisticsRow(long CharacterId, long Kills);
}

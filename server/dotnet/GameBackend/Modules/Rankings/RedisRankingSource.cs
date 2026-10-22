using Sa.GameBackend.Common;
using Sa.Shared.Ranking;
using StackExchange.Redis;

namespace Sa.GameBackend.Modules.Rankings;

/// <summary>
/// The ranking from the sorted set the EventWorker keeps: ZREVRANGE for the top and ZREVRANK for a
/// place, both logarithmic in the number of characters. Names still come from PostgreSQL, one query
/// per page. Redis is a copy that can be empty (just restarted), behind (a new character's first
/// kill not yet written) or unreachable; in each case the answer comes from PostgreSQL instead, so
/// it is never wrong, only slower.
/// </summary>
internal sealed partial class RedisRankingSource(IConnectionMultiplexer redis, RankingRepository postgres, ILogger<RedisRankingSource> logger) : IRankingSource
{
    public async Task<IReadOnlyList<RankingEntry>> TopAsync(WorldId worldId, int top, CancellationToken cancellationToken)
    {
        SortedSetEntry[] entries;
        try
        {
            IDatabase db = redis.GetDatabase();
            RedisKey key = RankingKeys.KillsKey(worldId.Value);
            if (!await db.KeyExistsAsync(key))
            {
                return await postgres.TopAsync(worldId, top, cancellationToken);
            }

            entries = await db.SortedSetRangeByRankWithScoresAsync(key, 0, top - 1, Order.Descending);
        }
        catch (RedisException ex)
        {
            LogUnavailable(ex.Message);
            return await postgres.TopAsync(worldId, top, cancellationToken);
        }

        CharacterId[] ids = entries.Select(e => new CharacterId(RankingKeys.CharacterOf(e.Element.ToString()))).ToArray();
        IReadOnlyDictionary<CharacterId, string> names = await postgres.NamesAsync(worldId, ids, cancellationToken);
        return entries.Select((e, i) => new RankingEntry(i + 1, ids[i], names.GetValueOrDefault(ids[i], string.Empty), RankingKeys.KillsOf(e.Score))).ToList();
    }

    public async Task<CharacterRank?> RankOfAsync(WorldId worldId, CharacterId characterId, CancellationToken cancellationToken)
    {
        long? rank;
        double? score;
        try
        {
            IDatabase db = redis.GetDatabase();
            RedisKey key = RankingKeys.KillsKey(worldId.Value);
            RedisValue member = RankingKeys.Member(characterId.Value);
            // Sent together; the multiplexer pipelines them.
            Task<long?> rankTask = db.SortedSetRankAsync(key, member, Order.Descending);
            Task<double?> scoreTask = db.SortedSetScoreAsync(key, member);
            rank = await rankTask;
            score = await scoreTask;
        }
        catch (RedisException ex)
        {
            LogUnavailable(ex.Message);
            return await postgres.RankOfAsync(worldId, characterId, cancellationToken);
        }

        // Not in the sorted set: no kills yet, or the copy has not caught up. PostgreSQL knows which.
        return rank is null || score is null
            ? await postgres.RankOfAsync(worldId, characterId, cancellationToken)
            : new CharacterRank(rank.Value + 1, RankingKeys.KillsOf(score.Value));
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "ranking read from Redis failed ({Reason}); answered from PostgreSQL")]
    private partial void LogUnavailable(string reason);
}

using Microsoft.Extensions.Logging.Abstractions;
using Sa.EventWorker.Ranking;
using Sa.Shared.Ranking;
using StackExchange.Redis;

namespace Sa.EventWorker.Tests.Harness;

/// <summary>Redis helpers: one shared connection, the ranking writer under test, and direct reads of the sorted set.</summary>
internal static class Redis
{
    private static IConnectionMultiplexer? _connection;

    public static IConnectionMultiplexer Connection(InfrastructureFixture infra)
        => _connection ??= ConnectionMultiplexer.Connect(infra.RedisConnectionString + ",allowAdmin=true");

    public static RankingWriter Writer(InfrastructureFixture infra)
        => new(Connection(infra), infra.DataSource, NullLogger<RankingWriter>.Instance);

    /// <summary>The character's place and kills in the world's sorted set, or null when it is not there.</summary>
    public static async Task<(long Rank, long Kills)?> PlaceAsync(InfrastructureFixture infra, long worldId, long characterId)
    {
        IDatabase db = Connection(infra).GetDatabase();
        long? rank = await db.SortedSetRankAsync(RankingKeys.KillsKey(worldId), RankingKeys.Member(characterId), Order.Descending);
        double? score = await db.SortedSetScoreAsync(RankingKeys.KillsKey(worldId), RankingKeys.Member(characterId));
        return rank is null || score is null ? null : (rank.Value + 1, RankingKeys.KillsOf(score.Value));
    }

    /// <summary>The world's characters from the top, as Redis orders them.</summary>
    public static async Task<IReadOnlyList<long>> OrderAsync(InfrastructureFixture infra, long worldId)
    {
        RedisValue[] members = await Connection(infra).GetDatabase().SortedSetRangeByRankAsync(RankingKeys.KillsKey(worldId), 0, -1, Order.Descending);
        return members.Select(m => RankingKeys.CharacterOf(m.ToString())).ToList();
    }

    public static Task DeleteAsync(InfrastructureFixture infra, long worldId)
        => Connection(infra).GetDatabase().KeyDeleteAsync(RankingKeys.KillsKey(worldId));
}

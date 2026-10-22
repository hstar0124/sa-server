using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Rankings;

internal sealed record RankingEntry(long Rank, CharacterId CharacterId, string Name, long Kills);

internal sealed record CharacterRank(long Rank, long Kills);

/// <summary>
/// The kill ranking: the top N and one character's place, in one order (kills descending, then the
/// lower character id). Two implementations, chosen by Rankings:Backend: the PostgreSQL statistics
/// and the Redis sorted set the EventWorker keeps from them. The measurement comparing the two is
/// docs/reports/01-ranking.md.
/// </summary>
internal interface IRankingSource
{
    public Task<IReadOnlyList<RankingEntry>> TopAsync(WorldId worldId, int top, CancellationToken cancellationToken);

    /// <summary>Null when the character has no recorded kills yet.</summary>
    public Task<CharacterRank?> RankOfAsync(WorldId worldId, CharacterId characterId, CancellationToken cancellationToken);
}

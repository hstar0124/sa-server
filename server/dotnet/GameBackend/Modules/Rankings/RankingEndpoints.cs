using Microsoft.AspNetCore.Http.HttpResults;
using Microsoft.Extensions.Options;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Rankings;

internal sealed record RankingEntryResponse(long Rank, long CharacterId, string Name, long Kills)
{
    public static RankingEntryResponse From(RankingEntry entry) => new(entry.Rank, entry.CharacterId.Value, entry.Name, entry.Kills);
}

internal sealed record CharacterRankResponse(long Rank, long Kills);

/// <summary>Leaderboard and "where am I" over the kill statistics. Any signed-in player may read any rank.</summary>
internal static class RankingEndpoints
{
    public static IEndpointRouteBuilder MapRankingEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/v1/worlds/{worldId:long}/rankings/kills", TopAsync).RequireAuthorization();
        app.MapGet("/api/v1/worlds/{worldId:long}/characters/{characterId:long}/rank", RankAsync).RequireAuthorization();
        return app;
    }

    private static async Task<Ok<IReadOnlyList<RankingEntryResponse>>> TopAsync(
        WorldId worldId, int? top, RankingRepository rankings, IOptions<RankingsOptions> options, CancellationToken cancellationToken)
    {
        // A page outside [1, MaxTop] is brought inside rather than refused: the answer is still a leaderboard.
        int size = Math.Clamp(top ?? options.Value.DefaultTop, 1, options.Value.MaxTop);
        IReadOnlyList<RankingEntry> rows = await rankings.TopAsync(worldId, size, cancellationToken);
        return TypedResults.Ok<IReadOnlyList<RankingEntryResponse>>(rows.Select(RankingEntryResponse.From).ToList());
    }

    private static async Task<Results<Ok<CharacterRankResponse>, ProblemHttpResult>> RankAsync(
        WorldId worldId, CharacterId characterId, RankingRepository rankings, CancellationToken cancellationToken)
    {
        CharacterRank? rank = await rankings.RankOfAsync(worldId, characterId, cancellationToken);
        return rank is null
            ? Problems.NotFound("unranked", "the character has no recorded kills yet")
            : TypedResults.Ok(new CharacterRankResponse(rank.Rank, rank.Kills));
    }
}

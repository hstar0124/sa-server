using System.Globalization;

namespace Sa.Shared.Ranking;

/// <summary>
/// The kill ranking as a Redis sorted set: one key per world, the score is the kill count, and the
/// member encodes the character id. The order must equal the PostgreSQL ranking: kills descending,
/// then the lower character id first. Redis orders equal scores by member string, in reverse for a
/// descending read (ZREVRANGE, ZREVRANK). So the member is <c>long.MaxValue - id</c> written with a
/// fixed width: a lower id gives a larger string and comes first. Nothing is folded into the score,
/// so neither the kill count nor the id has a limit below what a double holds exactly. The key names
/// the encoding's version: a different encoding gets a different key, so two formats never share
/// one sorted set (an older writer's members would otherwise sort among these).
/// </summary>
internal static class RankingKeys
{
    // long.MaxValue has 19 digits; a fixed width makes string order equal number order.
    private const string MemberFormat = "D19";

    /// <summary>Kill counts up to 2^53 are exact in a double score.</summary>
    public const long MaxKills = 1L << 53;

    private const string EncodingVersion = "v1";

    public static string KillsKey(long worldId) => string.Create(CultureInfo.InvariantCulture, $"ranking:kills:{EncodingVersion}:{worldId}");

    /// <summary>Throws for a negative id: no character has one, and it would break the fixed width.</summary>
    public static string Member(long characterId)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(characterId);
        return (long.MaxValue - characterId).ToString(MemberFormat, CultureInfo.InvariantCulture);
    }

    public static long CharacterOf(string member) => long.MaxValue - long.Parse(member, NumberStyles.None, CultureInfo.InvariantCulture);

    /// <summary>Throws for a count a double cannot hold exactly.</summary>
    public static double Score(long kills)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(kills);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(kills, MaxKills);
        return kills;
    }

    public static long KillsOf(double score) => (long)score;
}

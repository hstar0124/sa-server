using Sa.Shared.Ranking;

namespace Sa.GameBackend.Tests.Ranking;

/// <summary>The sorted-set encoding must order exactly like the PostgreSQL ranking, with no limit a real game reaches.</summary>
public sealed class RankingKeysTests
{
    [Fact]
    public void For_equal_scores_a_descending_member_order_is_an_ascending_character_order()
    {
        long[] ids = [1, 2, 9, 10, 99, 100, 4_294_967_296, long.MaxValue - 1];

        // Redis reads equal scores in reverse string order (ordinal, byte by byte).
        long[] read = ids.Select(RankingKeys.Member).OrderDescending(StringComparer.Ordinal).Select(RankingKeys.CharacterOf).ToArray();

        Assert.Equal(ids, read);
    }

    [Fact]
    public void The_key_names_the_world_and_the_encoding_version()
    {
        Assert.Equal("ranking:kills:v1:7", RankingKeys.KillsKey(7));
    }

    [Fact]
    public void Members_and_scores_round_trip_and_out_of_range_values_are_refused()
    {
        Assert.Equal(123_456_789_012L, RankingKeys.CharacterOf(RankingKeys.Member(123_456_789_012L)));
        Assert.Equal(RankingKeys.MaxKills, RankingKeys.KillsOf(RankingKeys.Score(RankingKeys.MaxKills)));
        Assert.Throws<ArgumentOutOfRangeException>(() => RankingKeys.Member(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => RankingKeys.Score(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => RankingKeys.Score(RankingKeys.MaxKills + 1));
    }
}

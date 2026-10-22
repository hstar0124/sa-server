using System.Net;
using System.Net.Http.Json;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Rankings;

/// <summary>Kill leaderboard over the statistics read model: order, tie-break and the "where am I" count.</summary>
[Collection(SharedPostgres.Name)]
public sealed class RankingTests(PostgresFixture postgres) : IDisposable
{
    // A world no other test records statistics in, so the leaderboard is exactly what a test seeds.
    private const long World = 2;
    private const int MaxTop = 3;

    private readonly BackendFactory _factory = new(postgres.ConnectionString, new BackendSettings(RankingsMaxTop: MaxTop, RankingsDefaultTop: MaxTop));

    private ApiClient Api => new(_factory.CreateClient());

    private static async Task<long> CharacterAsync(ApiClient owner)
    {
        HttpResponseMessage created = await owner.CreateCharacterAsync(World, Unique.CharacterName());
        created.EnsureSuccessStatusCode();
        return (await created.Content.ReadFromJsonAsync<ApiClient.CharacterResponse>())!.CharacterId;
    }

    private async Task<long> CharacterWithKillsAsync(ApiClient owner, long kills)
    {
        long id = await CharacterAsync(owner);
        await Db.ExecuteAsync(postgres.ConnectionString,
            "INSERT INTO game.character_projections (world_id, character_id, kills) VALUES (@World, @id, @kills)", new { World, id, kills });
        return id;
    }

    [Fact]
    public async Task The_leaderboard_orders_by_kills_breaks_ties_by_id_and_a_rank_counts_those_ahead()
    {
        // Far above anything else in this world, so these three are its top three.
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        long first = await CharacterWithKillsAsync(owner, 3_000_000);
        long second = await CharacterWithKillsAsync(owner, 2_000_000);
        long third = await CharacterWithKillsAsync(owner, 2_000_000);  // tie: the lower id ranks ahead
        long unranked = await CharacterAsync(owner);

        HttpResponseMessage board = await owner.GetKillRankingAsync(World, top: 3);
        HttpResponseMessage rankOfThird = await owner.GetRankAsync(World, third);
        HttpResponseMessage rankOfNobody = await owner.GetRankAsync(World, unranked);
        HttpResponseMessage rankInAnotherWorld = await owner.GetRankAsync(World - 1, first);

        Assert.Equal(HttpStatusCode.OK, board.StatusCode);
        List<ApiClient.RankingEntryResponse> rows = (await board.Content.ReadFromJsonAsync<List<ApiClient.RankingEntryResponse>>())!;
        Assert.Equal(new[] { first, second, third }, rows.Select(r => r.CharacterId));
        Assert.Equal(new long[] { 1, 2, 3 }, rows.Select(r => r.Rank));
        Assert.Equal(new ApiClient.CharacterRankResponse(3, 2_000_000), await rankOfThird.Content.ReadFromJsonAsync<ApiClient.CharacterRankResponse>());
        Assert.Equal(HttpStatusCode.NotFound, rankOfNobody.StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, rankInAnotherWorld.StatusCode);
    }

    [Fact]
    public async Task A_page_size_outside_the_allowed_range_is_brought_inside_it()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        for (int i = 0; i <= MaxTop; i++)
        {
            await CharacterWithKillsAsync(owner, 1_000 + i);
        }

        List<ApiClient.RankingEntryResponse> huge = (await (await owner.GetKillRankingAsync(World, top: 100_000)).Content.ReadFromJsonAsync<List<ApiClient.RankingEntryResponse>>())!;
        List<ApiClient.RankingEntryResponse> zero = (await (await owner.GetKillRankingAsync(World, top: 0)).Content.ReadFromJsonAsync<List<ApiClient.RankingEntryResponse>>())!;

        Assert.Equal(MaxTop, huge.Count);
        Assert.Single(zero);
    }

    [Fact]
    public async Task A_default_page_larger_than_the_maximum_stops_the_backend_from_starting()
    {
        using var misconfigured = new BackendFactory(postgres.ConnectionString, new BackendSettings(RankingsMaxTop: 5, RankingsDefaultTop: 10));
        // Start-up validation reports every failing option; the rankings one must be among them.
        Exception refused = Assert.ThrowsAny<Exception>(() => misconfigured.CreateClient());
        Assert.Contains("Rankings:DefaultTop must not exceed Rankings:MaxTop", refused.ToString(), StringComparison.Ordinal);
    }

    public void Dispose() => _factory.Dispose();
}

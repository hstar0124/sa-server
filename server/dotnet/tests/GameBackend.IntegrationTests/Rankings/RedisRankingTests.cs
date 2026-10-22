using System.Net.Http.Json;
using Sa.GameBackend.IntegrationTests.Harness;
using Sa.Shared.Ranking;
using StackExchange.Redis;

namespace Sa.GameBackend.IntegrationTests.Rankings;

/// <summary>
/// The Redis ranking answers exactly like the PostgreSQL one, and when Redis is empty, behind or
/// unreachable the answer still comes, from PostgreSQL.
/// </summary>
[Collection(SharedPostgres.Name)]
public sealed class RedisRankingTests(PostgresFixture postgres, RedisFixture redis) : IClassFixture<RedisFixture>, IDisposable
{
    private const int Top = 10;
    private static long _nextWorld = 2_000 + (DateTime.UtcNow.Ticks % 1_000_000);

    private readonly BackendFactory _fromPostgres = new(postgres.ConnectionString);
    private readonly BackendFactory _fromRedis = new(postgres.ConnectionString, new BackendSettings(RedisConnectionString: redis.ConnectionString));

    private async Task<long> FreshWorldAsync()
    {
        long world = Interlocked.Increment(ref _nextWorld);
        await Db.ExecuteAsync(postgres.ConnectionString, "INSERT INTO game.worlds (world_id, name) VALUES (@world, 'ranking-' || @world)", new { world });
        return world;
    }

    /// <summary>Characters with these kill counts, in the statistics and, when asked, in Redis as the EventWorker writes them.</summary>
    private async Task<long[]> CharactersAsync(ApiClient owner, long world, long[] kills, bool mirrorToRedis)
    {
        var ids = new long[kills.Length];
        for (int i = 0; i < kills.Length; i++)
        {
            HttpResponseMessage created = await owner.CreateCharacterAsync(world, Unique.CharacterName());
            created.EnsureSuccessStatusCode();
            ids[i] = (await created.Content.ReadFromJsonAsync<ApiClient.CharacterResponse>())!.CharacterId;
            await Db.ExecuteAsync(postgres.ConnectionString,
                "INSERT INTO game.character_projections (world_id, character_id, kills) VALUES (@world, @id, @kills)", new { world, id = ids[i], kills = kills[i] });
            if (mirrorToRedis)
            {
                await redis.Database.SortedSetAddAsync(RankingKeys.KillsKey(world), RankingKeys.Member(ids[i]), RankingKeys.Score(kills[i]));
            }
        }

        return ids;
    }

    private static async Task<List<ApiClient.RankingEntryResponse>> BoardAsync(ApiClient client, long world)
        => (await (await client.GetKillRankingAsync(world, Top)).Content.ReadFromJsonAsync<List<ApiClient.RankingEntryResponse>>())!;

    private static async Task<ApiClient.CharacterRankResponse?> RankAsync(ApiClient client, long world, long character)
    {
        HttpResponseMessage response = await client.GetRankAsync(world, character);
        return response.IsSuccessStatusCode ? await response.Content.ReadFromJsonAsync<ApiClient.CharacterRankResponse>() : null;
    }

    [Fact]
    public async Task Redis_and_postgres_give_the_same_leaderboard_and_the_same_ranks_ties_included()
    {
        // MaxCharactersPerWorld is 4 by default, so two accounts.
        long world = await FreshWorldAsync();
        ApiClient pg = await new ApiClient(_fromPostgres.CreateClient()).AsNewAccountAsync(Unique.Username());
        ApiClient rd = await new ApiClient(_fromRedis.CreateClient()).AsNewAccountAsync(Unique.Username());
        long[] ids = [.. await CharactersAsync(pg, world, [5, 9, 5, 1], mirrorToRedis: true), .. await CharactersAsync(rd, world, [9, 0], mirrorToRedis: true)];

        List<ApiClient.RankingEntryResponse> fromPostgres = await BoardAsync(pg, world);
        List<ApiClient.RankingEntryResponse> fromRedis = await BoardAsync(rd, world);

        Assert.Equal(ids.Length, fromPostgres.Count);
        Assert.Equal(fromPostgres, fromRedis);
        foreach (long id in ids)
        {
            Assert.Equal(await RankAsync(pg, world, id), await RankAsync(rd, world, id));
        }
    }

    [Fact]
    public async Task An_empty_or_lagging_redis_is_answered_from_postgres()
    {
        long world = await FreshWorldAsync();
        ApiClient rd = await new ApiClient(_fromRedis.CreateClient()).AsNewAccountAsync(Unique.Username());
        long[] unmirrored = await CharactersAsync(rd, world, [3, 7], mirrorToRedis: false);

        // No key at all: the world's ranking was just lost.
        List<ApiClient.RankingEntryResponse> board = await BoardAsync(rd, world);
        Assert.Equal(new[] { unmirrored[1], unmirrored[0] }, board.Select(e => e.CharacterId));

        // The key exists but lacks a character whose first kill has not been copied yet.
        long[] mirrored = await CharactersAsync(rd, world, [5], mirrorToRedis: true);
        Assert.Equal(new ApiClient.CharacterRankResponse(1, 7), await RankAsync(rd, world, unmirrored[1]));
        Assert.Equal(new ApiClient.CharacterRankResponse(1, 5), await RankAsync(rd, world, mirrored[0]));  // Redis alone: rank 1 of one
    }

    [Fact]
    public async Task An_unreachable_redis_is_answered_from_postgres()
    {
        long world = await FreshWorldAsync();
        using var unreachable = new BackendFactory(postgres.ConnectionString,
            new BackendSettings(RedisConnectionString: "127.0.0.1:1,abortConnect=false,connectTimeout=200,asyncTimeout=500"));
        ApiClient client = await new ApiClient(unreachable.CreateClient()).AsNewAccountAsync(Unique.Username());
        long[] ids = await CharactersAsync(client, world, [4, 8], mirrorToRedis: false);

        Assert.Equal(new[] { ids[1], ids[0] }, (await BoardAsync(client, world)).Select(e => e.CharacterId));
        Assert.Equal(new ApiClient.CharacterRankResponse(2, 4), await RankAsync(client, world, ids[0]));
    }

    public void Dispose()
    {
        _fromPostgres.Dispose();
        _fromRedis.Dispose();
    }
}

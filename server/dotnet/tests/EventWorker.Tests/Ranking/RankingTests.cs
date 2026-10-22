using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Sa.Events.V1;
using Sa.EventWorker.Consuming;
using Sa.EventWorker.Ranking;
using Sa.EventWorker.Tests.Harness;

namespace Sa.EventWorker.Tests.Ranking;

/// <summary>
/// The Redis ranking is a copy of the statistics: absolute scores that never go backwards, the same
/// order as PostgreSQL, and every way it can fall behind repaired from the statistics.
/// </summary>
[Collection(SharedInfrastructure.Name)]
public sealed class RankingTests(InfrastructureFixture infra)
{
    private const string Consumer = "ranking-test";
    private const int Batch = 100;

    private static RankingReconciler Reconciler(RankingWriter writer)
        => new(writer, Options.Create(new RankingOptions { RebuildBatchSize = Batch }), NullLogger<RankingReconciler>.Instance);

    /// <summary>Applies kills to the statistics only, as a consumer that died before its Redis write would have.</summary>
    private async Task KillsWithoutRankingAsync(long world, long character, int kills)
    {
        var store = new ProjectionStore(infra.DataSource);
        for (int i = 1; i <= kills; i++)
        {
            EventEnvelope kill = Outbox.Envelope(character, (ulong)i, Outbox.Kill(character, 1));
            kill.WorldId = (ulong)world;
            await store.ApplyAsync(Consumer, kill, CancellationToken.None);
        }
    }

    [Fact]
    public async Task Scores_are_absolute_and_a_late_lower_write_cannot_undo_a_newer_one()
    {
        long world = await Characters.FreshWorldAsync(infra.ConnectionString);
        long character = await Characters.CreateAsync(infra.ConnectionString, world);
        RankingWriter writer = Redis.Writer(infra);

        await writer.UpdateAsync(world, character, 5);
        await writer.UpdateAsync(world, character, 3);  // an older total arriving late

        Assert.Equal((1L, 5L), await Redis.PlaceAsync(infra, world, character));
    }

    [Fact]
    public async Task Redis_orders_ties_like_postgres_the_lower_character_id_first()
    {
        long world = await Characters.FreshWorldAsync(infra.ConnectionString);
        long low = await Characters.CreateAsync(infra.ConnectionString, world);
        long high = await Characters.CreateAsync(infra.ConnectionString, world);
        long top = await Characters.CreateAsync(infra.ConnectionString, world);
        RankingWriter writer = Redis.Writer(infra);

        await writer.UpdateAsync(world, high, 7);
        await writer.UpdateAsync(world, low, 7);
        await writer.UpdateAsync(world, top, 9);

        Assert.Equal(new[] { top, low, high }, await Redis.OrderAsync(infra, world));
    }

    [Fact]
    public async Task A_missing_ranking_is_refilled_from_the_statistics()
    {
        long world = await Characters.FreshWorldAsync(infra.ConnectionString);
        long a = await Characters.CreateAsync(infra.ConnectionString, world);
        long b = await Characters.CreateAsync(infra.ConnectionString, world);
        await KillsWithoutRankingAsync(world, a, 2);
        await KillsWithoutRankingAsync(world, b, 1);
        RankingWriter writer = Redis.Writer(infra);
        using RankingReconciler reconciler = Reconciler(writer);
        await reconciler.ReconcileOnceAsync(CancellationToken.None);  // start-up pass

        await Redis.DeleteAsync(infra, world);  // Redis restarted: it keeps nothing on disk
        Assert.Null(await Redis.PlaceAsync(infra, world, a));
        await reconciler.ReconcileOnceAsync(CancellationToken.None);

        Assert.Equal((1L, 2L), await Redis.PlaceAsync(infra, world, a));
        Assert.Equal((2L, 1L), await Redis.PlaceAsync(infra, world, b));
    }

    [Fact]
    public async Task The_start_up_pass_repairs_a_write_lost_when_the_previous_process_died()
    {
        // The key exists, so only the start-up pass can notice that a character is missing from it:
        // the redelivered event is a duplicate and writes nothing.
        long world = await Characters.FreshWorldAsync(infra.ConnectionString);
        long ranked = await Characters.CreateAsync(infra.ConnectionString, world);
        long lost = await Characters.CreateAsync(infra.ConnectionString, world);
        RankingWriter writer = Redis.Writer(infra);
        await writer.UpdateAsync(world, ranked, 1);
        await KillsWithoutRankingAsync(world, lost, 3);

        using RankingReconciler reconciler = Reconciler(writer);
        await reconciler.ReconcileOnceAsync(CancellationToken.None);

        Assert.Equal((1L, 3L), await Redis.PlaceAsync(infra, world, lost));
    }

    [Fact]
    public async Task A_failed_write_marks_the_world_behind_and_only_then_is_it_refilled()
    {
        long world = await Characters.FreshWorldAsync(infra.ConnectionString);
        long ranked = await Characters.CreateAsync(infra.ConnectionString, world);
        long lost = await Characters.CreateAsync(infra.ConnectionString, world);
        RankingWriter writer = Redis.Writer(infra);
        using RankingReconciler reconciler = Reconciler(writer);
        await writer.UpdateAsync(world, ranked, 1);
        await reconciler.ReconcileOnceAsync(CancellationToken.None);  // start-up pass done

        await KillsWithoutRankingAsync(world, lost, 2);  // the write after this commit failed
        await reconciler.ReconcileOnceAsync(CancellationToken.None);
        Assert.Null(await Redis.PlaceAsync(infra, world, lost));  // the key exists: nothing to notice

        writer.MarkBehind(world);  // what the consumer does when the write throws
        await reconciler.ReconcileOnceAsync(CancellationToken.None);

        Assert.Equal((1L, 2L), await Redis.PlaceAsync(infra, world, lost));
    }
}

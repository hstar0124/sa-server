using Microsoft.Extensions.Options;

namespace Sa.EventWorker.Ranking;

/// <summary>
/// Redis is a copy of the statistics, not a second source of truth. Committing the statistics and
/// writing Redis are not one transaction, so the copy can fall behind in three ways, and each one
/// is repaired by refilling the world from the statistics:
/// <list type="bullet">
///   <item>at start-up: the previous process may have committed the statistics and died before the
///   Redis write, and the redelivered event is then a duplicate that writes nothing;</item>
///   <item>when the key is missing: Redis was flushed or restarted (it keeps no data on disk);</item>
///   <item>when a write failed: the consumer marks the world behind.</item>
/// </list>
/// </summary>
internal sealed partial class RankingReconciler(RankingWriter writer, IOptions<RankingOptions> options, ILogger<RankingReconciler> logger) : BackgroundService
{
    private long _refills;
    private bool _startedUp;

    public long Refills => Interlocked.Read(ref _refills);

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(options.Value.ReconcileInterval);
        try
        {
            do
            {
                await ReconcileOnceAsync(stoppingToken);
            }
            while (await timer.WaitForNextTickAsync(stoppingToken));
        }
        catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
        {
            // Normal shutdown.
        }
    }

    /// <summary>One pass over every world. The first successful pass refills them all.</summary>
    public async Task ReconcileOnceAsync(CancellationToken ct)
    {
        IReadOnlyList<long> worlds;
        try
        {
            worlds = await writer.WorldsAsync(ct);
        }
        catch (Npgsql.NpgsqlException ex)
        {
            LogFailed(0, ex.Message);
            return;
        }

        bool everyWorld = !_startedUp;
        bool allDone = true;
        foreach (long worldId in worlds)
        {
            bool behind = writer.TakeBehind(worldId);
            try
            {
                if (everyWorld || behind || !await writer.ExistsAsync(worldId))
                {
                    await writer.RefillAsync(worldId, options.Value.RebuildBatchSize, ct);
                    Interlocked.Increment(ref _refills);
                }
            }
            catch (Exception ex) when (ex is StackExchange.Redis.RedisException or Npgsql.NpgsqlException)
            {
                allDone = false;
                if (behind)
                {
                    writer.MarkBehind(worldId);  // still behind: try again next pass
                }

                LogFailed(worldId, ex.Message);
            }
        }

        _startedUp |= everyWorld && allDone;
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "ranking reconcile of world {WorldId} failed: {Reason}; the next pass tries again")]
    private partial void LogFailed(long worldId, string reason);
}

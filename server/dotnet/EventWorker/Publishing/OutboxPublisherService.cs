using Microsoft.Extensions.Options;
using Sa.EventWorker.Topics;

namespace Sa.EventWorker.Publishing;

/// <summary>
/// Drains <c>game.outbox_events</c> to Kafka while this instance holds the publisher lock; without it
/// the instance waits as a standby. A full batch is followed by another pass at once, an empty one
/// waits for the poll interval. A failed event is logged and retried on the next pass; nothing is
/// dropped.
/// </summary>
internal sealed partial class OutboxPublisherService(
    OutboxPublisher publisher,
    OutboxRepository outbox,
    TopicRouting routing,
    IOptions<OutboxPublisherOptions> options,
    ILogger<OutboxPublisherService> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        TimeSpan interval = options.Value.PollInterval;
        LogStarted(interval, options.Value.BatchSize);
        using var timer = new PeriodicTimer(interval);
        try
        {
            long unroutable = await outbox.CountUnroutableAsync(routing.AggregateTypes, stoppingToken);
            if (unroutable > 0)
            {
                LogUnroutable(unroutable);
            }

            bool standby = false;
            while (!stoppingToken.IsCancellationRequested)
            {
                await using (PublisherLock? held = await outbox.TryAcquirePublisherLockAsync(stoppingToken))
                {
                    if (held is not null)
                    {
                        LogActive();
                        standby = false;
                        await PublishWhileHeldAsync(held, timer, stoppingToken);
                        continue;
                    }
                }

                if (!standby)
                {
                    LogStandby();
                    standby = true;
                }

                await timer.WaitForNextTickAsync(stoppingToken);
            }
        }
        catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
        {
            // Normal shutdown.
        }

        LogStopped();
    }

    private async Task PublishWhileHeldAsync(PublisherLock held, PeriodicTimer timer, CancellationToken ct)
    {
        while (await held.StillHeldAsync(ct))
        {
            PublishPass pass = await publisher.PublishOnceAsync(ct);
            if (pass.Published > 0 || pass.Failed > 0)
            {
                LogPass(pass.Fetched, pass.Published, pass.Failed);
            }

            bool more = pass.Fetched >= options.Value.BatchSize && pass.Failed == 0;
            if (!more)
            {
                await timer.WaitForNextTickAsync(ct);
            }
        }

        LogLockLost();
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "outbox publisher started, poll interval {Interval}, batch {Batch}")]
    private partial void LogStarted(TimeSpan interval, int batch);

    [LoggerMessage(Level = LogLevel.Information, Message = "outbox publisher active")]
    private partial void LogActive();

    [LoggerMessage(Level = LogLevel.Information, Message = "another instance is publishing; waiting as a standby")]
    private partial void LogStandby();

    [LoggerMessage(Level = LogLevel.Warning, Message = "publisher lock lost (database connection gone); trying to take it again")]
    private partial void LogLockLost();

    [LoggerMessage(Level = LogLevel.Error, Message = "{Count} unpublished outbox rows have an aggregate type without a topic and will not be published")]
    private partial void LogUnroutable(long count);

    [LoggerMessage(Level = LogLevel.Information, Message = "outbox pass: fetched {Fetched} published {Published} failed {Failed}")]
    private partial void LogPass(int fetched, int published, int failed);

    [LoggerMessage(Level = LogLevel.Information, Message = "outbox publisher stopped")]
    private partial void LogStopped();
}

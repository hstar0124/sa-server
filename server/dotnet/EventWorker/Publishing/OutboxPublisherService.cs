using Microsoft.Extensions.Options;

namespace Sa.EventWorker.Publishing;

/// <summary>
/// Drains <c>game.outbox_events</c> to Kafka in aggregate order.
/// Stage 0 only establishes the hosted-service shape and its polling cadence; the Outbox query,
/// producer and publish cursor arrive in stage 6.
/// </summary>
internal sealed partial class OutboxPublisherService(
    IOptions<OutboxPublisherOptions> options,
    ILogger<OutboxPublisherService> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        TimeSpan interval = options.Value.PollInterval;
        LogStarted(interval);

        using var timer = new PeriodicTimer(interval);
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken))
            {
                // Stage 6: fetch unpublished rows, publish, persist the cursor.
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown.
        }

        LogStopped();
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "Outbox publisher started, poll interval {Interval}")]
    private partial void LogStarted(TimeSpan interval);

    [LoggerMessage(Level = LogLevel.Information, Message = "Outbox publisher stopped")]
    private partial void LogStopped();
}

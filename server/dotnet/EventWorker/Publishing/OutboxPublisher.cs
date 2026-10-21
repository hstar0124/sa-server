using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Microsoft.Extensions.Options;
using Sa.Events.V1;
using Sa.EventWorker.Topics;

namespace Sa.EventWorker.Publishing;

/// <summary>What one pass did; the service logs it and decides whether to pass again at once.</summary>
internal sealed record PublishPass(int Fetched, int Published, int Failed);

/// <summary>
/// Publishes a batch of outbox rows aggregate by aggregate in sequence order. When an event of an
/// aggregate fails, the rest of that aggregate waits for the next pass, so a later sequence never
/// overtakes an earlier one; other aggregates continue.
/// </summary>
internal sealed partial class OutboxPublisher(
    OutboxRepository outbox,
    IEventProducer producer,
    TopicRouting routing,
    IOptions<OutboxPublisherOptions> options,
    ILogger<OutboxPublisher> logger)
{
    public async Task<PublishPass> PublishOnceAsync(CancellationToken ct)
    {
        IReadOnlyList<OutboxRow> rows = await outbox.FetchUnpublishedAsync(routing.AggregateTypes, options.Value.BatchSize, ct);
        int published = 0;
        int failed = 0;
        // The query returns each aggregate's rows together and in sequence order.
        foreach (IGrouping<(long, string, long), OutboxRow> aggregate in rows.GroupBy(r => (r.WorldId, r.AggregateType, r.AggregateId)))
        {
            string topic = routing.TopicFor(aggregate.Key.Item2);
            foreach (OutboxRow row in aggregate)
            {
                if (!await PublishRowAsync(topic, row, ct))
                {
                    failed += 1;
                    break;  // keep the aggregate's order: its later rows wait for the next pass
                }

                published += 1;
            }
        }

        return new PublishPass(rows.Count, published, failed);
    }

    /// <summary>True once the broker acknowledged the event and the row is marked; false when the broker did not.</summary>
    private async Task<bool> PublishRowAsync(string topic, OutboxRow row, CancellationToken ct)
    {
        EventEnvelope envelope = ToEnvelope(row);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(options.Value.PublishTimeout);
        try
        {
            await producer.PublishAsync(topic, TopicRouting.KeyFor(envelope), envelope, timeout.Token);
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !ct.IsCancellationRequested)
        {
            LogPublishFailed(Convert.ToHexStringLower(row.EventId), row.EventType, ex.Message);
            return false;
        }

        await outbox.MarkPublishedAsync(row.EventId, ct);
        return true;
    }

    public static EventEnvelope ToEnvelope(OutboxRow row) => new()
    {
        EventId = ByteString.CopyFrom(row.EventId),
        EventType = row.EventType,
        SchemaVersion = (uint)row.SchemaVersion,
        WorldId = (ulong)row.WorldId,
        AggregateType = row.AggregateType,
        AggregateId = (ulong)row.AggregateId,
        AggregateSeq = (ulong)row.AggregateSeq,
        RequestId = row.RequestId is null ? ByteString.Empty : ByteString.CopyFrom(row.RequestId),
        OccurredAt = Timestamp.FromDateTime(DateTime.SpecifyKind(row.OccurredAt, DateTimeKind.Utc)),
        Payload = ByteString.CopyFrom(row.Payload),
    };

    [LoggerMessage(Level = LogLevel.Warning, Message = "publish of {EventId} ({EventType}) failed: {Reason}; its aggregate waits for the next pass")]
    private partial void LogPublishFailed(string eventId, string eventType, string reason);
}

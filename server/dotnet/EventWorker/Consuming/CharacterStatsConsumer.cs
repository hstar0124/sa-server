using Confluent.Kafka;
using Google.Protobuf;
using Microsoft.Extensions.Options;
using Npgsql;
using Sa.Events.V1;
using Sa.EventWorker.Publishing;
using Sa.EventWorker.Topics;

namespace Sa.EventWorker.Consuming;

/// <summary>
/// Reads the character topic and keeps <c>character_projections</c> current: the database
/// transaction first, the Kafka offset commit after, so a crash in between re-reads the message and
/// the Inbox skips it. A message whose write fails for a reason that may pass is read again after a
/// pause. One that can never be applied (it does not decode, or the database refuses its row) goes
/// to the failed topic and is committed, so it never blocks its partition.
/// </summary>
internal sealed partial class CharacterStatsConsumer(
    ProjectionStore store,
    IEventProducer producer,
    IOptions<KafkaOptions> kafka,
    IOptions<ConsumerOptions> options,
    ILogger<CharacterStatsConsumer> logger) : BackgroundService
{
    public const string UndecodableEventType = "Undecodable";

    private long _applied;
    private long _duplicates;
    private long _rejected;

    public long Applied => Interlocked.Read(ref _applied);

    public long Duplicates => Interlocked.Read(ref _duplicates);

    public long Rejected => Interlocked.Read(ref _rejected);

    protected override Task ExecuteAsync(CancellationToken stoppingToken)
        // Consume() blocks; keep it off the thread pool's shared workers.
        => Task.Factory.StartNew(() => RunAsync(stoppingToken), stoppingToken, TaskCreationOptions.LongRunning, TaskScheduler.Default).Unwrap();

    private async Task RunAsync(CancellationToken ct)
    {
        var config = new ConsumerConfig
        {
            BootstrapServers = kafka.Value.BootstrapServers,
            GroupId = options.Value.GroupId,
            EnableAutoCommit = false,
            AutoOffsetReset = AutoOffsetReset.Earliest,
        };
        using IConsumer<string, byte[]> consumer = new ConsumerBuilder<string, byte[]>(config).Build();
        consumer.Subscribe(kafka.Value.CharacterEvents.Name);
        LogStarted(options.Value.GroupId, kafka.Value.CharacterEvents.Name);
        try
        {
            while (!ct.IsCancellationRequested)
            {
                ConsumeResult<string, byte[]>? result = consumer.Consume(options.Value.PollTimeout);
                if (result is null)
                {
                    continue;
                }

                try
                {
                    await HandleAsync(result.Message, ct);
                    consumer.Commit(result);
                }
                catch (Exception ex) when (ex is NpgsqlException or KafkaException)
                {
                    // The database or the broker is unreachable: read the same message again after a pause.
                    LogRetry(result.TopicPartitionOffset.ToString(), ex.Message);
                    consumer.Seek(result.TopicPartitionOffset);
                    await Task.Delay(options.Value.RetryDelay, ct);
                }
            }
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested)
        {
            // Normal shutdown.
        }
        finally
        {
            consumer.Close();
            LogStopped(Applied, Duplicates, Rejected);
        }
    }

    /// <summary>Applies one message; visible to tests so the once-only rule is checked without a broker loop.</summary>
    public async Task<ApplyOutcome> HandleAsync(Message<string, byte[]> message, CancellationToken ct)
    {
        EventEnvelope envelope;
        try
        {
            envelope = EventEnvelope.Parser.ParseFrom(message.Value);
        }
        catch (InvalidProtocolBufferException ex)
        {
            return await RejectAsync(message.Key, new EventEnvelope { EventType = UndecodableEventType, Payload = ByteString.CopyFrom(message.Value) }, ex.Message, ct);
        }

        try
        {
            ApplyOutcome outcome = await store.ApplyAsync(options.Value.GroupId, envelope, ct);
            Interlocked.Increment(ref outcome == ApplyOutcome.Duplicate ? ref _duplicates : ref _applied);
            return outcome;
        }
        catch (InvalidProtocolBufferException ex)
        {
            return await RejectAsync(message.Key, envelope, ex.Message, ct);
        }
        catch (PostgresException ex) when (ex.SqlState.StartsWith(IntegrityViolationClass, StringComparison.Ordinal))
        {
            // A constraint refused the row (an unknown character, a negative count): retrying cannot help.
            return await RejectAsync(message.Key, envelope, $"{ex.ConstraintName}: {ex.MessageText}", ct);
        }
    }

    // SQLSTATE class 23: integrity constraint violations (foreign key, check, unique, not null).
    private const string IntegrityViolationClass = "23";

    private async Task<ApplyOutcome> RejectAsync(string key, EventEnvelope envelope, string reason, CancellationToken ct)
    {
        LogRejected(Convert.ToHexStringLower(envelope.EventId.Span), envelope.EventType, reason);
        await producer.PublishAsync(kafka.Value.FailedEvents.Name, key, envelope, ct);
        Interlocked.Increment(ref _rejected);
        return ApplyOutcome.Rejected;
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "character stats consumer started: group {Group}, topic {Topic}")]
    private partial void LogStarted(string group, string topic);

    [LoggerMessage(Level = LogLevel.Warning, Message = "message at {Offset} not applied: {Reason}; retrying")]
    private partial void LogRetry(string offset, string reason);

    [LoggerMessage(Level = LogLevel.Error, Message = "event {EventId} ({EventType}) rejected to the failed topic: {Reason}")]
    private partial void LogRejected(string eventId, string eventType, string reason);

    [LoggerMessage(Level = LogLevel.Information, Message = "character stats consumer stopped: applied {Applied} duplicates {Duplicates} rejected {Rejected}")]
    private partial void LogStopped(long applied, long duplicates, long rejected);
}

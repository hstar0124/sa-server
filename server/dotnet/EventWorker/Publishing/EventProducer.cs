using System.Text;
using Confluent.Kafka;
using Google.Protobuf;
using Sa.Events.V1;

namespace Sa.EventWorker.Publishing;

/// <summary>Delivers one envelope to a topic; the publisher's only dependency on the broker, so its ordering rules are tested without one.</summary>
internal interface IEventProducer
{
    /// <summary>Returns when the broker acknowledged the write; throws when it did not.</summary>
    public Task PublishAsync(string topic, string key, EventEnvelope envelope, CancellationToken ct);
}

/// <summary>
/// Kafka producer: idempotent, acknowledged by all in-sync replicas, event type as a header for
/// tooling. The delivery timeout is the publisher's: a message the publisher gave up on is not
/// still being retried behind its back.
/// </summary>
internal sealed class KafkaEventProducer : IEventProducer, IDisposable
{
    public const string EventTypeHeader = "event_type";

    private static readonly TimeSpan FlushOnDispose = TimeSpan.FromSeconds(5);

    private readonly IProducer<string, byte[]> _producer;

    public KafkaEventProducer(string bootstrapServers, TimeSpan deliveryTimeout)
    {
        var config = new ProducerConfig
        {
            BootstrapServers = bootstrapServers,
            EnableIdempotence = true,
            Acks = Acks.All,
            MessageTimeoutMs = (int)deliveryTimeout.TotalMilliseconds,
        };
        _producer = new ProducerBuilder<string, byte[]>(config).Build();
    }

    public async Task PublishAsync(string topic, string key, EventEnvelope envelope, CancellationToken ct)
    {
        var message = new Message<string, byte[]>
        {
            Key = key,
            Value = envelope.ToByteArray(),
            Headers = new Headers { { EventTypeHeader, Encoding.UTF8.GetBytes(envelope.EventType) } },
        };
        await _producer.ProduceAsync(topic, message, ct);
    }

    public void Dispose()
    {
        _producer.Flush(FlushOnDispose);
        _producer.Dispose();
    }
}

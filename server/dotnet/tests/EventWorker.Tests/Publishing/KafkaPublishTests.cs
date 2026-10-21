using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Sa.Events.V1;
using Sa.EventWorker.Publishing;
using Sa.EventWorker.Tests.Harness;
using Sa.EventWorker.Topics;

namespace Sa.EventWorker.Tests.Publishing;

/// <summary>The real producer against a real broker, including the redelivery a publisher crash causes.</summary>
[Collection(SharedInfrastructure.Name)]
public sealed class KafkaPublishTests(InfrastructureFixture infra)
{
    private static readonly OutboxPublisherOptions PublisherOptions = new() { BatchSize = 100 };

    [Fact]
    public async Task Envelopes_reach_the_character_topic_keyed_by_world_and_character_and_a_crash_after_the_ack_republishes_the_same_event_id()
    {
        IOptions<KafkaOptions> topics = Kafka.FreshTopics(infra.BootstrapServers);
        await Kafka.ProvisionAsync(topics);
        await Kafka.ProvisionAsync(topics);  // idempotent: topics already present are fine
        long a = Outbox.FreshAggregate();
        byte[] killId = await Outbox.AppendAsync(infra.ConnectionString, a, 1, Outbox.Kill(a, 3));
        await Outbox.AppendAsync(infra.ConnectionString, a, 2, Outbox.Acquired(a, 1));
        using var producer = new KafkaEventProducer(infra.BootstrapServers, PublisherOptions.PublishTimeout);
        var publisher = new OutboxPublisher(new OutboxRepository(infra.DataSource), producer, new TopicRouting(topics),
            Options.Create(PublisherOptions), NullLogger<OutboxPublisher>.Instance);

        PublishPass pass = await publisher.PublishOnceAsync(CancellationToken.None);
        // The publisher died after the broker's ack and before the UPDATE: the row is unpublished again.
        await Outbox.ForgetPublishedAsync(infra.ConnectionString, killId);
        PublishPass again = await publisher.PublishOnceAsync(CancellationToken.None);

        Assert.Equal(0, pass.Failed);
        Assert.Equal(0, again.Failed);
        List<(string Key, EventEnvelope Envelope)> read = Kafka.ReadEnvelopes(infra.BootstrapServers, topics.Value.CharacterEvents.Name, 3);
        var ofA = read.Where(r => r.Envelope.AggregateId == (ulong)a).ToList();
        Assert.Equal(3, ofA.Count);
        Assert.All(ofA, r => Assert.Equal($"{Outbox.World}:{a}", r.Key));
        // The same event twice: consumers deduplicate by event id.
        Assert.Equal(2, ofA.Count(r => r.Envelope.EventId.ToByteArray().SequenceEqual(killId)));
        EventEnvelope kill = ofA.First(r => r.Envelope.AggregateSeq == 1).Envelope;
        Assert.Equal(KillRecorded.Descriptor.Name, kill.EventType);
        Assert.Equal(3UL, KillRecorded.Parser.ParseFrom(kill.Payload).GoldGranted);
        Assert.True(kill.OccurredAt.ToDateTime() > DateTime.UtcNow.AddMinutes(-5));
    }
}

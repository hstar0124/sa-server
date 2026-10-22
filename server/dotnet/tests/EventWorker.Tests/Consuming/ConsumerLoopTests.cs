using Confluent.Kafka;
using Google.Protobuf;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Sa.Events.V1;
using Sa.EventWorker.Consuming;
using Sa.EventWorker.Publishing;
using Sa.EventWorker.Tests.Harness;
using Sa.EventWorker.Tests.Publishing;
using Sa.EventWorker.Topics;

namespace Sa.EventWorker.Tests.Consuming;

/// <summary>Outbox → Kafka → read model through the real publisher and consumer, and the routes to the failed topic.</summary>
[Collection(SharedInfrastructure.Name)]
public sealed class ConsumerLoopTests(InfrastructureFixture infra)
{
    private static readonly TimeSpan Settle = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan SettlePoll = TimeSpan.FromMilliseconds(100);
    private static readonly OutboxPublisherOptions PublisherOptions = new();

    private static IOptions<ConsumerOptions> FreshGroup() => Options.Create(new ConsumerOptions
    {
        GroupId = "stats-" + Guid.NewGuid().ToString("N")[..8],
        PollTimeout = TimeSpan.FromMilliseconds(200),
        RetryDelay = TimeSpan.FromMilliseconds(200),
    });

    private static async Task<T> EventuallyAsync<T>(Func<Task<T>> read, Func<T, bool> done)
    {
        DateTime deadline = DateTime.UtcNow + Settle;
        T value = await read();
        while (!done(value) && DateTime.UtcNow < deadline)
        {
            await Task.Delay(SettlePoll);
            value = await read();
        }

        return value;
    }

    [Fact]
    public async Task Published_events_end_up_in_the_read_model_exactly_once_even_when_published_twice()
    {
        IOptions<KafkaOptions> topics = Kafka.FreshTopics(infra.BootstrapServers);
        await Kafka.ProvisionAsync(topics);
        long character = await Characters.CreateAsync(infra.ConnectionString);
        byte[] killId = await Outbox.AppendAsync(infra.ConnectionString, character, 1, Outbox.Kill(character, 7));
        await Outbox.AppendAsync(infra.ConnectionString, character, 2, Outbox.Acquired(character, 1));
        using var producer = new KafkaEventProducer(infra.BootstrapServers, PublisherOptions.PublishTimeout);
        var publisher = new OutboxPublisher(new OutboxRepository(infra.DataSource), producer, new TopicRouting(topics),
            Options.Create(PublisherOptions), NullLogger<OutboxPublisher>.Instance);
        IOptions<ConsumerOptions> group = FreshGroup();
        using var consumer = new CharacterStatsConsumer(new ProjectionStore(infra.DataSource), Redis.Writer(infra), producer, topics, group, NullLogger<CharacterStatsConsumer>.Instance);

        await publisher.PublishOnceAsync(CancellationToken.None);
        await Outbox.ForgetPublishedAsync(infra.ConnectionString, killId);
        await publisher.PublishOnceAsync(CancellationToken.None);  // the kill is on the topic twice
        await consumer.StartAsync(CancellationToken.None);
        Characters.Stats? stats = await EventuallyAsync(() => Characters.StatsAsync(infra.ConnectionString, character), s => s is { ItemsAcquired: 1 } && consumer.Duplicates >= 1);
        await consumer.StopAsync(CancellationToken.None);

        Assert.Equal(new Characters.Stats(Kills: 1, ItemsAcquired: 1, GoldEarned: 7, LastEventSeq: 2), stats);
        Assert.Equal((1L, 1L), await Redis.PlaceAsync(infra, Outbox.World, character) is { } place ? (1L, place.Kills) : (0L, 0L));
        Assert.Equal(2, consumer.Applied);
        Assert.Equal(1, consumer.Duplicates);
        Assert.Equal(1, await Characters.InboxCountAsync(infra.ConnectionString, group.Value.GroupId, killId));
    }

    [Fact]
    public async Task Undecodable_messages_go_to_the_failed_topic_and_the_partition_moves_on()
    {
        IOptions<KafkaOptions> topics = Kafka.FreshTopics(infra.BootstrapServers);
        await Kafka.ProvisionAsync(topics);
        long character = await Characters.CreateAsync(infra.ConnectionString);
        using var producer = new KafkaEventProducer(infra.BootstrapServers, PublisherOptions.PublishTimeout);
        IOptions<ConsumerOptions> group = FreshGroup();
        using var consumer = new CharacterStatsConsumer(new ProjectionStore(infra.DataSource), Redis.Writer(infra), producer, topics, group, NullLogger<CharacterStatsConsumer>.Instance);

        // Garbage first, then a valid kill behind it in the same partition (same key).
        string key = $"{Outbox.World}:{character}";
        using (IProducer<string, byte[]> raw = new ProducerBuilder<string, byte[]>(new ProducerConfig { BootstrapServers = infra.BootstrapServers }).Build())
        {
            await raw.ProduceAsync(topics.Value.CharacterEvents.Name, new Message<string, byte[]> { Key = key, Value = [0xFF, 0xFF, 0xFF, 0xFF] });
        }

        await producer.PublishAsync(topics.Value.CharacterEvents.Name, key, Outbox.Envelope(character, 1, Outbox.Kill(character, 2)), CancellationToken.None);

        await consumer.StartAsync(CancellationToken.None);
        Characters.Stats? stats = await EventuallyAsync(() => Characters.StatsAsync(infra.ConnectionString, character), s => s is { Kills: 1 });
        await consumer.StopAsync(CancellationToken.None);

        Assert.Equal(1, stats!.Kills);
        Assert.Equal(1, consumer.Rejected);
        (string Key, EventEnvelope Envelope) rejected = Assert.Single(Kafka.ReadEnvelopes(infra.BootstrapServers, topics.Value.FailedEvents.Name, 1));
        Assert.Equal(key, rejected.Key);
        Assert.Equal(CharacterStatsConsumer.UndecodableEventType, rejected.Envelope.EventType);
    }

    [Fact]
    public async Task An_event_the_database_refuses_is_sent_to_the_failed_topic_instead_of_being_retried_forever()
    {
        // A character that does not exist: the read model's foreign key refuses the row on every attempt.
        IOptions<KafkaOptions> topics = Kafka.FreshTopics(infra.BootstrapServers);
        long ghost = Outbox.FreshAggregate();
        var producer = new FakeProducer();
        IOptions<ConsumerOptions> group = FreshGroup();
        using var consumer = new CharacterStatsConsumer(new ProjectionStore(infra.DataSource), Redis.Writer(infra), producer, topics, group, NullLogger<CharacterStatsConsumer>.Instance);
        EventEnvelope kill = Outbox.Envelope(ghost, 1, Outbox.Kill(ghost, 4));

        ApplyOutcome outcome = await consumer.HandleAsync(new Message<string, byte[]> { Key = $"{Outbox.World}:{ghost}", Value = kill.ToByteArray() }, CancellationToken.None);

        Assert.Equal(ApplyOutcome.Rejected, outcome);
        (string topic, _, EventEnvelope sent) = Assert.Single(producer.Published);
        Assert.Equal(topics.Value.FailedEvents.Name, topic);
        Assert.Equal(kill.EventId, sent.EventId);
        Assert.Equal(0, await Characters.InboxCountAsync(infra.ConnectionString, group.Value.GroupId, kill.EventId.ToByteArray()));
    }
}

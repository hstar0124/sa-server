using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Sa.Events.V1;
using Sa.EventWorker.Publishing;
using Sa.EventWorker.Tests.Harness;
using Sa.EventWorker.Topics;
using Sa.Shared.Events;

namespace Sa.EventWorker.Tests.Publishing;

/// <summary>A broker double: records what was published and fails where the test says.</summary>
internal sealed class FakeProducer : IEventProducer
{
    public List<(string Topic, string Key, EventEnvelope Envelope)> Published { get; } = [];

    public Func<EventEnvelope, bool> FailWhen { get; set; } = _ => false;

    public Task PublishAsync(string topic, string key, EventEnvelope envelope, CancellationToken ct)
    {
        if (FailWhen(envelope))
        {
            throw new InvalidOperationException("broker unavailable");
        }

        Published.Add((topic, key, envelope));
        return Task.CompletedTask;
    }

    public List<ulong> SequencesOf(long aggregateId)
        => Published.Where(p => p.Envelope.AggregateId == (ulong)aggregateId).Select(p => p.Envelope.AggregateSeq).ToList();
}

/// <summary>Aggregate order is kept, a failure stalls only its aggregate, nothing is lost, one instance publishes.</summary>
[Collection(SharedInfrastructure.Name)]
public sealed class OutboxPublisherTests(InfrastructureFixture infra)
{
    private const int Batch = 100;

    private OutboxPublisher Publisher(FakeProducer producer, int batchSize = Batch)
        => new(new OutboxRepository(infra.DataSource), producer, new TopicRouting(Kafka.FreshTopics(infra.BootstrapServers)),
            Options.Create(new OutboxPublisherOptions { BatchSize = batchSize }), NullLogger<OutboxPublisher>.Instance);

    [Fact]
    public async Task Publishes_each_aggregate_in_sequence_order_and_marks_rows_after_the_ack()
    {
        long a = Outbox.FreshAggregate();
        long b = Outbox.FreshAggregate();
        await Outbox.AppendAsync(infra.ConnectionString, a, 1, Outbox.Kill(a, 3));
        await Outbox.AppendAsync(infra.ConnectionString, b, 1, Outbox.Kill(b, 8));
        await Outbox.AppendAsync(infra.ConnectionString, a, 2, Outbox.Acquired(a, 1));
        var producer = new FakeProducer();

        PublishPass pass = await Publisher(producer).PublishOnceAsync(CancellationToken.None);

        Assert.Equal(0, pass.Failed);
        var ofA = producer.Published.Where(p => p.Envelope.AggregateId == (ulong)a).ToList();
        Assert.Equal(new ulong[] { 1, 2 }, ofA.Select(p => p.Envelope.AggregateSeq));
        Assert.Equal(new[] { KillRecorded.Descriptor.Name, InventoryItemAcquired.Descriptor.Name }, ofA.Select(p => p.Envelope.EventType));
        Assert.All(ofA, p => Assert.Equal($"{Outbox.World}:{a}", p.Key));
        Assert.Equal(new ulong[] { 1 }, producer.SequencesOf(b));
        Assert.Equal(new[] { true, true }, await Outbox.PublishedFlagsAsync(infra.ConnectionString, a));
        Assert.Equal(new[] { true }, await Outbox.PublishedFlagsAsync(infra.ConnectionString, b));
    }

    [Fact]
    public async Task A_failed_event_holds_back_the_rest_of_its_aggregate_but_not_others()
    {
        long a = Outbox.FreshAggregate();
        long b = Outbox.FreshAggregate();
        await Outbox.AppendAsync(infra.ConnectionString, a, 1, Outbox.Kill(a, 3));
        await Outbox.AppendAsync(infra.ConnectionString, a, 2, Outbox.Acquired(a, 1));
        await Outbox.AppendAsync(infra.ConnectionString, a, 3, Outbox.Acquired(a, 2));
        await Outbox.AppendAsync(infra.ConnectionString, b, 1, Outbox.Kill(b, 8));
        var producer = new FakeProducer { FailWhen = e => e.AggregateId == (ulong)a && e.AggregateSeq == 2 };
        OutboxPublisher publisher = Publisher(producer);

        PublishPass first = await publisher.PublishOnceAsync(CancellationToken.None);
        IReadOnlyList<bool> afterFirst = await Outbox.PublishedFlagsAsync(infra.ConnectionString, a);
        producer.FailWhen = _ => false;
        PublishPass second = await publisher.PublishOnceAsync(CancellationToken.None);

        Assert.Equal(1, first.Failed);
        Assert.Equal(new[] { true, false, false }, afterFirst);
        Assert.Equal(0, second.Failed);
        Assert.Equal(new ulong[] { 1, 2, 3 }, producer.SequencesOf(a));
        Assert.Equal(new[] { true, true, true }, await Outbox.PublishedFlagsAsync(infra.ConnectionString, a));
        Assert.Equal(new[] { true }, await Outbox.PublishedFlagsAsync(infra.ConnectionString, b));
    }

    [Fact]
    public async Task A_batch_limit_never_lets_a_later_sequence_out_first_even_when_its_transaction_started_earlier()
    {
        // Two transactions of one character: the one that started first took the sequence lock
        // second, so its row is number 2 but carries the earlier start time.
        long a = Outbox.FreshAggregate();
        DateTime now = DateTime.UtcNow;
        await Outbox.AppendAsync(infra.ConnectionString, a, 1, Outbox.Kill(a, 3), occurredAt: now.AddDays(-1));
        await Outbox.AppendAsync(infra.ConnectionString, a, 2, Outbox.Acquired(a, 1), occurredAt: now.AddDays(-1).AddSeconds(-1));
        var producer = new FakeProducer();
        OutboxPublisher onePerPass = Publisher(producer, batchSize: 1);

        await onePerPass.PublishOnceAsync(CancellationToken.None);
        IReadOnlyList<bool> afterFirst = await Outbox.PublishedFlagsAsync(infra.ConnectionString, a);
        await onePerPass.PublishOnceAsync(CancellationToken.None);

        Assert.Equal(new[] { true, false }, afterFirst);
        Assert.Equal(new ulong[] { 1, 2 }, producer.SequencesOf(a));
    }

    [Fact]
    public async Task Rows_of_an_aggregate_type_without_a_topic_are_not_sent_and_do_not_fill_the_batch()
    {
        long guild = Outbox.FreshAggregate();
        long a = Outbox.FreshAggregate();
        DateTime longAgo = DateTime.UtcNow.AddDays(-2);
        await Outbox.AppendAsync(infra.ConnectionString, guild, 1, Outbox.Kill(guild, 1), aggregateType: "guild", occurredAt: longAgo);
        await Outbox.AppendAsync(infra.ConnectionString, guild, 2, Outbox.Kill(guild, 1), aggregateType: "guild", occurredAt: longAgo);
        await Outbox.AppendAsync(infra.ConnectionString, a, 1, Outbox.Kill(a, 3));
        var producer = new FakeProducer();
        var repository = new OutboxRepository(infra.DataSource);

        await Publisher(producer, batchSize: 1).PublishOnceAsync(CancellationToken.None);

        Assert.Equal(new ulong[] { 1 }, producer.SequencesOf(a));
        Assert.Empty(producer.SequencesOf(guild));
        Assert.True(await repository.CountUnroutableAsync([AggregateTypes.Character], CancellationToken.None) >= 2);
    }

    [Fact]
    public async Task One_instance_holds_the_publisher_lock_and_another_takes_it_once_released_or_lost()
    {
        var repository = new OutboxRepository(infra.DataSource);

        PublisherLock? first = await repository.TryAcquirePublisherLockAsync(CancellationToken.None);
        PublisherLock? second = await repository.TryAcquirePublisherLockAsync(CancellationToken.None);
        Assert.NotNull(first);
        Assert.Null(second);
        Assert.True(await first.StillHeldAsync(CancellationToken.None));

        await first.DisposeAsync();
        PublisherLock? next = await repository.TryAcquirePublisherLockAsync(CancellationToken.None);
        Assert.NotNull(next);

        // The database drops the holder's session (a restart, an administrator): the holder must
        // notice rather than keep publishing next to whoever takes the lock now.
        await Outbox.TerminatePublisherLockHolderAsync(infra.ConnectionString);
        Assert.False(await next.StillHeldAsync(CancellationToken.None));
        await next.DisposeAsync();
        await using PublisherLock? after = await repository.TryAcquirePublisherLockAsync(CancellationToken.None);
        Assert.NotNull(after);
    }
}

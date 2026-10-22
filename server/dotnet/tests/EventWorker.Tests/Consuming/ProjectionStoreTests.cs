using Google.Protobuf;
using Sa.Events.V1;
using Sa.EventWorker.Consuming;
using Sa.EventWorker.Tests.Harness;

namespace Sa.EventWorker.Tests.Consuming;

/// <summary>The same event applied twice changes the read model once; the consumer may die between its commit and Kafka's.</summary>
[Collection(SharedInfrastructure.Name)]
public sealed class ProjectionStoreTests(InfrastructureFixture infra)
{
    private const string Consumer = "character-stats-test";

    [Fact]
    public async Task Kills_and_acquisitions_accumulate_and_a_redelivered_event_is_applied_once()
    {
        long character = await Characters.CreateAsync(infra.ConnectionString);
        var store = new ProjectionStore(infra.DataSource);
        EventEnvelope kill = Outbox.Envelope(character, 1, Outbox.Kill(character, 3));
        EventEnvelope pickup = Outbox.Envelope(character, 2, Outbox.Acquired(character, 1));

        Assert.Equal(ApplyOutcome.Applied, (await store.ApplyAsync(Consumer, kill, CancellationToken.None)).Outcome);
        Assert.Equal(ApplyOutcome.Applied, (await store.ApplyAsync(Consumer, pickup, CancellationToken.None)).Outcome);
        // The consumer died after its database commit and before the offset commit: Kafka hands the kill over again.
        Assert.Equal(ApplyOutcome.Duplicate, (await store.ApplyAsync(Consumer, kill, CancellationToken.None)).Outcome);
        Assert.Equal(ApplyOutcome.Duplicate, (await store.ApplyAsync(Consumer, kill, CancellationToken.None)).Outcome);

        Assert.Equal(new Characters.Stats(Kills: 1, ItemsAcquired: 1, GoldEarned: 3, LastEventSeq: 2), await Characters.StatsAsync(infra.ConnectionString, character));
        Assert.Equal(1, await Characters.InboxCountAsync(infra.ConnectionString, Consumer, kill.EventId.ToByteArray()));
    }

    [Fact]
    public async Task Another_consumer_name_keeps_its_own_inbox_and_unknown_events_are_recorded_but_ignored()
    {
        long character = await Characters.CreateAsync(infra.ConnectionString);
        var store = new ProjectionStore(infra.DataSource);
        EventEnvelope kill = Outbox.Envelope(character, 1, Outbox.Kill(character, 5));
        var unknown = new EventEnvelope(Outbox.Envelope(character, 2, Outbox.Kill(character, 0))) { EventType = "GuildJoined" };

        Assert.Equal(ApplyOutcome.Applied, (await store.ApplyAsync(Consumer, kill, CancellationToken.None)).Outcome);
        // A rebuild under a new consumer name replays everything.
        Assert.Equal(ApplyOutcome.Applied, (await store.ApplyAsync("rebuild-2", kill, CancellationToken.None)).Outcome);
        Assert.Equal(ApplyOutcome.Ignored, (await store.ApplyAsync(Consumer, unknown, CancellationToken.None)).Outcome);
        Assert.Equal(ApplyOutcome.Duplicate, (await store.ApplyAsync(Consumer, unknown, CancellationToken.None)).Outcome);

        Characters.Stats? stats = await Characters.StatsAsync(infra.ConnectionString, character);
        Assert.Equal(2, stats!.Kills);  // both consumer names counted the kill into the shared table
    }

    [Fact]
    public async Task A_payload_that_does_not_decode_is_refused_before_anything_is_written()
    {
        long character = await Characters.CreateAsync(infra.ConnectionString);
        var store = new ProjectionStore(infra.DataSource);
        var broken = new EventEnvelope(Outbox.Envelope(character, 1, Outbox.Kill(character, 1))) { Payload = ByteString.CopyFrom(0xFF, 0xFF, 0xFF) };

        await Assert.ThrowsAsync<InvalidProtocolBufferException>(() => store.ApplyAsync(Consumer, broken, CancellationToken.None));

        Assert.Null(await Characters.StatsAsync(infra.ConnectionString, character));
        Assert.Equal(0, await Characters.InboxCountAsync(infra.ConnectionString, Consumer, broken.EventId.ToByteArray()));
    }
}

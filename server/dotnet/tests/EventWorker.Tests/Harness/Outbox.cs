using Dapper;
using Google.Protobuf;
using Npgsql;
using Sa.Events.V1;
using Sa.EventWorker.Publishing;
using Sa.Shared.Events;

namespace Sa.EventWorker.Tests.Harness;

/// <summary>Writes and inspects outbox rows directly, standing in for the Backend's transactions.</summary>
internal static class Outbox
{
    public const long World = 1;  // seeded by the migrations

    private const uint Slime = 101;
    private const uint SlimeGel = 2001;

    private static long _nextAggregate = DateTime.UtcNow.Ticks % 1_000_000_000;

    /// <summary>A character id no other test in this run uses.</summary>
    public static long FreshAggregate() => Interlocked.Increment(ref _nextAggregate);

    /// <summary>
    /// Appends one event and returns its event id. <paramref name="occurredAt"/> stands in for the
    /// writing transaction's start time; the Backend's rows take <c>now()</c>.
    /// </summary>
    public static async Task<byte[]> AppendAsync(string connectionString, long aggregateId, long seq, IMessage payload,
        string aggregateType = AggregateTypes.Character, DateTime? occurredAt = null)
    {
        byte[] eventId = Guid.NewGuid().ToByteArray();
        await using var connection = new NpgsqlConnection(connectionString);
        await connection.ExecuteAsync("""
            INSERT INTO game.outbox_events (event_id, world_id, aggregate_type, aggregate_id, aggregate_seq, event_type, schema_version, request_id, payload, occurred_at)
            VALUES (@eventId, @world, @aggregateType, @aggregateId, @seq, @eventType, 1, NULL, @payload, COALESCE(@occurredAt, now()))
            """, new { eventId, world = World, aggregateType, aggregateId, seq, eventType = payload.Descriptor.Name, payload = payload.ToByteArray(), occurredAt });
        return eventId;
    }

    public static async Task<IReadOnlyList<bool>> PublishedFlagsAsync(string connectionString, long aggregateId)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return (await connection.QueryAsync<bool>(
            "SELECT published_at IS NOT NULL FROM game.outbox_events WHERE aggregate_id = @aggregateId ORDER BY aggregate_seq", new { aggregateId })).ToList();
    }

    /// <summary>Undoes the publish mark: what the row looks like after a publisher died between the broker's ack and the UPDATE.</summary>
    public static async Task ForgetPublishedAsync(string connectionString, byte[] eventId)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        await connection.ExecuteAsync("UPDATE game.outbox_events SET published_at = NULL WHERE event_id = @eventId", new { eventId });
    }

    /// <summary>Ends the database session that holds the publisher lock, as a database restart would.</summary>
    public static async Task TerminatePublisherLockHolderAsync(string connectionString)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        await connection.ExecuteAsync("""
            SELECT pg_terminate_backend(pid) FROM pg_locks
            WHERE locktype = 'advisory' AND classid::bigint = @Space AND objid::bigint = @Key AND objsubid = 2 AND granted
            """, OutboxRepository.LockKey);
    }

    /// <summary>An envelope as the publisher would build it, for tests that skip the Outbox.</summary>
    public static EventEnvelope Envelope(long characterId, ulong seq, IMessage payload) => new()
    {
        EventId = ByteString.CopyFrom(Guid.NewGuid().ToByteArray()),
        EventType = payload.Descriptor.Name,
        SchemaVersion = 1,
        WorldId = World,
        AggregateType = AggregateTypes.Character,
        AggregateId = (ulong)characterId,
        AggregateSeq = seq,
        Payload = payload.ToByteString(),
    };

    public static KillRecorded Kill(long characterId, ulong gold) => new()
    {
        CharacterId = (ulong)characterId,
        KillId = ByteString.CopyFrom(Guid.NewGuid().ToByteArray()),
        MonsterTemplateId = Slime,
        GoldGranted = gold,
        GoldBalance = gold,
    };

    public static InventoryItemAcquired Acquired(long characterId, uint version) => new()
    {
        CharacterId = (ulong)characterId,
        ItemInstanceId = ByteString.CopyFrom(Guid.NewGuid().ToByteArray()),
        ItemTemplateId = SlimeGel,
        InventoryVersion = version,
    };

    public static TradeCompleted Traded(long characterId, ulong goldReceived) => new()
    {
        TradeId = ByteString.CopyFrom(Guid.NewGuid().ToByteArray()),
        CharacterId = (ulong)characterId,
        CounterpartyId = (ulong)characterId + 1,
        GoldReceived = goldReceived,
        InventoryVersion = 1,
    };
}

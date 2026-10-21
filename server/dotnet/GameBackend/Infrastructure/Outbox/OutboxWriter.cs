using Dapper;
using Google.Protobuf;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Infrastructure.Outbox;

/// <summary>
/// The stream an event belongs to. Its sequence numbers the aggregate's events in commit order, so a
/// consumer can tell whether it has seen them all.
/// </summary>
internal readonly record struct EventAggregate(string Type, long Id)
{
    public static EventAggregate Character(CharacterId id) => new("character", id.Value);
}

/// <summary>
/// A confirmed business fact to publish. The event type is the protobuf message name, so a consumer
/// picks the parser from the envelope alone. <paramref name="Cause"/> is the idempotency key of the
/// request that produced it (a kill id, a pickup request key).
/// </summary>
internal sealed record OutboxEvent(WorldId WorldId, EventAggregate Aggregate, IMessage Payload, byte[] Cause);

/// <summary>
/// Writes <c>game.outbox_events</c> rows inside the caller's transaction, so the fact and its event
/// commit or roll back together (see README "자산과 멱등성"). The per-aggregate sequence is allocated
/// here too: the upsert locks the aggregate's <c>aggregate_sequences</c> row until commit, which
/// orders concurrent writers of one aggregate.
/// </summary>
internal static class OutboxWriter
{
    public const int SchemaVersion = 1;

    // An aggregate's first event is number 1: a new row stores 2 as the next one.
    private const string NextSequenceSql = """
        INSERT INTO game.aggregate_sequences (world_id, aggregate_type, aggregate_id, next_seq)
        VALUES (@WorldId, @AggregateType, @AggregateId, 2)
        ON CONFLICT (world_id, aggregate_type, aggregate_id) DO UPDATE
            SET next_seq = game.aggregate_sequences.next_seq + 1
        RETURNING next_seq - 1
        """;

    private const string InsertSql = """
        INSERT INTO game.outbox_events
            (event_id, world_id, aggregate_type, aggregate_id, aggregate_seq, event_type, schema_version, request_id, payload)
        VALUES (@EventId, @WorldId, @AggregateType, @AggregateId, @AggregateSeq, @EventType, @SchemaVersion, @RequestId, @Payload)
        """;

    public static async Task AppendAsync(NpgsqlConnection connection, NpgsqlTransaction tx, OutboxEvent outboxEvent, CancellationToken ct)
    {
        var aggregate = new { outboxEvent.WorldId, AggregateType = outboxEvent.Aggregate.Type, AggregateId = outboxEvent.Aggregate.Id };
        long seq = await connection.ExecuteScalarAsync<long>(new CommandDefinition(NextSequenceSql, aggregate, tx, cancellationToken: ct));
        await connection.ExecuteAsync(new CommandDefinition(InsertSql, new
        {
            EventId = OpaqueId.New(),
            aggregate.WorldId,
            aggregate.AggregateType,
            aggregate.AggregateId,
            AggregateSeq = seq,
            EventType = outboxEvent.Payload.Descriptor.Name,
            SchemaVersion,
            RequestId = outboxEvent.Cause,
            Payload = outboxEvent.Payload.ToByteArray(),
        }, tx, cancellationToken: ct));
    }
}

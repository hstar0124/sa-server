using System.Data;
using Dapper;
using Npgsql;
using Sa.Events.V1;

namespace Sa.EventWorker.Consuming;

internal enum ApplyOutcome
{
    Applied,    // first sight of the event: the read model moved
    Duplicate,  // the Inbox already had it: nothing changed
    Ignored,    // not an event this model reads; recorded so a redelivery is cheap
    Rejected,   // cannot be applied, ever: sent to the failed-events topic
}

/// <summary>The outcome and, when the read model moved, the character's kill total after the change.</summary>
internal sealed record ApplyResult(ApplyOutcome Outcome, long? Kills)
{
    public static ApplyResult Duplicate { get; } = new(ApplyOutcome.Duplicate, null);

    public static ApplyResult Ignored { get; } = new(ApplyOutcome.Ignored, null);
}

/// <summary>
/// Applies one event to <c>character_projections</c> exactly once: the Inbox insert and the read
/// model update share a transaction, and the Inbox primary key refuses a second application of the
/// same event id, however often Kafka delivers it again.
/// </summary>
internal sealed class ProjectionStore(NpgsqlDataSource dataSource)
{
    private const string ClaimSql = """
        INSERT INTO game.inbox_events (consumer_name, event_id) VALUES (@ConsumerName, @EventId)
        ON CONFLICT (consumer_name, event_id) DO NOTHING
        """;

    private const string UpsertSql = """
        INSERT INTO game.character_projections (world_id, character_id, kills, items_acquired, gold_earned, trades_completed, last_event_seq)
        VALUES (@WorldId, @CharacterId, @Kills, @ItemsAcquired, @GoldEarned, @TradesCompleted, @Seq)
        ON CONFLICT (world_id, character_id) DO UPDATE
            SET kills = game.character_projections.kills + EXCLUDED.kills,
                items_acquired = game.character_projections.items_acquired + EXCLUDED.items_acquired,
                gold_earned = game.character_projections.gold_earned + EXCLUDED.gold_earned,
                trades_completed = game.character_projections.trades_completed + EXCLUDED.trades_completed,
                last_event_seq = GREATEST(game.character_projections.last_event_seq, EXCLUDED.last_event_seq),
                updated_at = now()
        RETURNING kills
        """;

    /// <summary>
    /// Throws InvalidProtocolBufferException for a payload that does not decode, a PostgresException
    /// for a row the database refuses, and NpgsqlException when the database is unreachable.
    /// </summary>
    public async Task<ApplyResult> ApplyAsync(string consumerName, EventEnvelope envelope, CancellationToken ct)
    {
        ProjectionDelta? delta = CharacterStatsProjection.DeltaOf(envelope);  // decode before any write

        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        await using NpgsqlTransaction tx = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, ct);
        int claimed = await connection.ExecuteAsync(new CommandDefinition(ClaimSql,
            new { ConsumerName = consumerName, EventId = envelope.EventId.ToByteArray() }, tx, cancellationToken: ct));
        if (claimed == 0)
        {
            return ApplyResult.Duplicate;
        }

        long? kills = null;
        if (delta is not null)
        {
            kills = await connection.ExecuteScalarAsync<long>(new CommandDefinition(UpsertSql, new
            {
                WorldId = (long)envelope.WorldId,
                CharacterId = (long)envelope.AggregateId,
                delta.Kills,
                delta.ItemsAcquired,
                delta.GoldEarned,
                delta.TradesCompleted,
                Seq = (long)envelope.AggregateSeq,
            }, tx, cancellationToken: ct));
        }

        await tx.CommitAsync(ct);
        return delta is null ? ApplyResult.Ignored : new ApplyResult(ApplyOutcome.Applied, kills);
    }
}

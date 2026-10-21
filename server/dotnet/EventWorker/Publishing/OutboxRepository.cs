using Dapper;
using Npgsql;

namespace Sa.EventWorker.Publishing;

/// <summary>One unpublished <c>game.outbox_events</c> row.</summary>
internal sealed record OutboxRow(
    byte[] EventId,
    long WorldId,
    string AggregateType,
    long AggregateId,
    long AggregateSeq,
    string EventType,
    int SchemaVersion,
    byte[]? RequestId,
    byte[] Payload,
    DateTime OccurredAt);

/// <summary>
/// The publisher's lock, held on its own connection for as long as this process publishes. A crashed
/// process loses its session and with it the lock, so it never blocks the next publisher.
/// </summary>
internal sealed class PublisherLock(NpgsqlConnection connection) : IAsyncDisposable
{
    private const string UnlockSql = "SELECT pg_advisory_unlock(@Space::int, @Key::int)";

    private const string HeldSql = """
        SELECT EXISTS (
            SELECT 1 FROM pg_locks
            WHERE locktype = 'advisory' AND classid::bigint = @Space AND objid::bigint = @Key AND objsubid = 2
              AND pid = pg_backend_pid() AND granted)
        """;

    /// <summary>
    /// False once the lock is gone: a session lock disappears silently with its connection (a
    /// database restart), and another instance may have taken over since.
    /// </summary>
    public async Task<bool> StillHeldAsync(CancellationToken ct)
    {
        try
        {
            return await connection.ExecuteScalarAsync<bool>(new CommandDefinition(HeldSql, OutboxRepository.LockKey, cancellationToken: ct));
        }
        catch (NpgsqlException)
        {
            return false;
        }
    }

    /// <summary>
    /// Unlocks before closing: a closed connection goes back to the pool with its session, and the
    /// session's lock, still alive.
    /// </summary>
    public async ValueTask DisposeAsync()
    {
        try
        {
            await connection.ExecuteScalarAsync<bool>(UnlockSql, OutboxRepository.LockKey);
        }
        catch (NpgsqlException)
        {
            // The session is gone, and the lock with it.
        }

        await connection.DisposeAsync();
    }
}

/// <summary>Reads the publish queue, records what has been delivered, and decides which instance publishes.</summary>
internal sealed class OutboxRepository(NpgsqlDataSource dataSource)
{
    /// <summary>The two-part advisory lock key: this project's key space and the publisher's key within it.</summary>
    public static readonly object LockKey = new { Space = 0x5341L, Key = 1L };

    // Rows come aggregate by aggregate in sequence order, and aggregates by their oldest unpublished
    // event. A batch limit therefore only cuts an aggregate's tail, never its head, so a later
    // sequence can never be sent before an earlier one. Ordering by occurred_at alone would not be
    // enough: it is the transaction's start, and the transaction that started first may have taken
    // the sequence lock second. Only aggregate types the publisher routes are fetched, so rows of an
    // unknown type cannot fill every batch.
    private const string FetchSql = """
        SELECT event_id AS "EventId", world_id AS "WorldId", aggregate_type AS "AggregateType",
               aggregate_id AS "AggregateId", aggregate_seq AS "AggregateSeq", event_type AS "EventType",
               schema_version AS "SchemaVersion", request_id AS "RequestId", payload AS "Payload",
               occurred_at AS "OccurredAt"
        FROM (
            SELECT e.*, min(occurred_at) OVER aggregate AS aggregate_oldest
            FROM game.outbox_events e
            WHERE published_at IS NULL AND aggregate_type = ANY(@AggregateTypes)
            WINDOW aggregate AS (PARTITION BY world_id, aggregate_type, aggregate_id)
        ) unpublished
        ORDER BY aggregate_oldest, world_id, aggregate_type, aggregate_id, aggregate_seq
        LIMIT @Batch
        """;

    private const string CountUnroutableSql = """
        SELECT count(*) FROM game.outbox_events WHERE published_at IS NULL AND aggregate_type <> ALL(@AggregateTypes)
        """;

    private const string MarkPublishedSql = """
        UPDATE game.outbox_events SET published_at = now() WHERE event_id = @EventId AND published_at IS NULL
        """;

    private const string TryLockSql = "SELECT pg_try_advisory_lock(@Space::int, @Key::int)";

    public async Task<IReadOnlyList<OutboxRow>> FetchUnpublishedAsync(IReadOnlyCollection<string> aggregateTypes, int batch, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        return (await connection.QueryAsync<OutboxRow>(new CommandDefinition(FetchSql, new { AggregateTypes = aggregateTypes.ToArray(), Batch = batch }, cancellationToken: ct))).ToList();
    }

    /// <summary>Rows no route exists for. They are a programming error in whoever wrote them and stay where they are.</summary>
    public async Task<long> CountUnroutableAsync(IReadOnlyCollection<string> aggregateTypes, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        return await connection.ExecuteScalarAsync<long>(new CommandDefinition(CountUnroutableSql, new { AggregateTypes = aggregateTypes.ToArray() }, cancellationToken: ct));
    }

    /// <summary>Called after the broker acknowledged the event. A crash between the two republishes it: consumers deduplicate by event id.</summary>
    public async Task MarkPublishedAsync(byte[] eventId, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(new CommandDefinition(MarkPublishedSql, new { EventId = eventId }, cancellationToken: ct));
    }

    /// <summary>
    /// Takes the publisher lock without waiting, or returns null when another instance holds it. Two
    /// publishers would each keep their own aggregate order but interleave with each other, so only
    /// one publishes; the other waits as a standby, for example while a deployment overlaps two.
    /// </summary>
    public async Task<PublisherLock?> TryAcquirePublisherLockAsync(CancellationToken ct)
    {
        NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        try
        {
            if (await connection.ExecuteScalarAsync<bool>(new CommandDefinition(TryLockSql, LockKey, cancellationToken: ct)))
            {
                return new PublisherLock(connection);
            }
        }
        catch
        {
            await connection.DisposeAsync();
            throw;
        }

        await connection.DisposeAsync();
        return null;
    }
}

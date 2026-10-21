using Dapper;
using Npgsql;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Direct database access for assertions and arrangement: tests check rows, not only status codes (see tests/integration/README.md).</summary>
internal static class Db
{
    public static async Task<T> ScalarAsync<T>(string connectionString, string sql, object? parameters = null)
        where T : notnull
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return await connection.ExecuteScalarAsync<T>(sql, parameters)
               ?? throw new InvalidOperationException("scalar query returned NULL: " + sql);
    }

    /// <summary>Arranges a state the API cannot reach quickly, such as an already expired lease or ticket.</summary>
    public static async Task ExecuteAsync(string connectionString, string sql, object? parameters = null)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        await connection.ExecuteAsync(sql, parameters);
    }

    public static async Task<IReadOnlyList<T>> QueryAsync<T>(string connectionString, string sql, object? parameters = null)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return (await connection.QueryAsync<T>(sql, parameters)).ToList();
    }

    /// <summary>The outbox rows of one character aggregate in sequence order.</summary>
    public static Task<IReadOnlyList<OutboxRow>> OutboxOfCharacterAsync(string connectionString, long worldId, long characterId)
        => QueryAsync<OutboxRow>(connectionString, """
            SELECT event_id AS "EventId", aggregate_seq AS "AggregateSeq", event_type AS "EventType", request_id AS "RequestId", payload AS "Payload"
            FROM game.outbox_events WHERE world_id = @worldId AND aggregate_type = 'character' AND aggregate_id = @characterId
            ORDER BY aggregate_seq
            """, new { worldId, characterId });

    public sealed record OutboxRow(byte[] EventId, long AggregateSeq, string EventType, byte[]? RequestId, byte[] Payload);

    private static readonly TimeSpan LockWaitPoll = TimeSpan.FromMilliseconds(20);
    private static readonly TimeSpan LockWaitTimeout = TimeSpan.FromSeconds(10);

    /// <summary>Returns once some session of this database is blocked on a row or index lock: the race a test arranged is in place.</summary>
    public static async Task WaitForLockWaitAsync(string connectionString)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        using var timeout = new CancellationTokenSource(LockWaitTimeout);
        while (!await connection.ExecuteScalarAsync<bool>(
                   "SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE datname = current_database() AND wait_event_type = 'Lock')"))
        {
            await Task.Delay(LockWaitPoll, timeout.Token);
        }
    }
}

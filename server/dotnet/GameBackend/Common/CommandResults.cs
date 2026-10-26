using Dapper;
using Npgsql;

namespace Sa.GameBackend.Common;

/// <summary>The idempotency key of one command: a scope ('pickup', 'trade') and the request id the zone sent.</summary>
internal sealed record RequestKey(WorldId WorldId, string Scope, byte[] RequestId);

/// <summary>A command's stored row: the hash of the body it was claimed with and the outcome decided for it.</summary>
internal sealed record StoredCommand(byte[] RequestHash, string Outcome, string Payload)
{
    /// <summary>Claimed by a transaction that has not decided yet; only a committed outcome may be replayed.</summary>
    public bool IsDecided => !string.Equals(Outcome, CommandResults.PendingOutcome, StringComparison.Ordinal);

    /// <summary>A retry carries the same body; a different command under the same id does not.</summary>
    public bool Matches(ReadOnlySpan<byte> requestHash) => RequestHash.AsSpan().SequenceEqual(requestHash);
}

/// <summary>
/// <c>game.command_results</c>: one effect per request id. Claiming the id is a command's first write,
/// so a concurrent duplicate blocks on the conflict until the first transaction ends and then reads
/// its stored outcome instead of acting again. The outcome is stored in the same transaction as the
/// effect, so a retry after a lost response gets the answer that was committed.
/// </summary>
internal static class CommandResults
{
    public const string PendingOutcome = "pending";

    private const string ClaimSql = """
        INSERT INTO game.command_results (world_id, scope, request_id, request_hash, outcome)
        VALUES (@WorldId, @Scope, @RequestId, @RequestHash, @Outcome)
        ON CONFLICT (world_id, scope, request_id) DO NOTHING
        """;

    private const string StoredSql = """
        SELECT request_hash AS "RequestHash", outcome AS "Outcome", payload::text AS "Payload"
        FROM game.command_results
        WHERE world_id = @WorldId AND scope = @Scope AND request_id = @RequestId
        """;

    private const string StoreSql = """
        UPDATE game.command_results
        SET outcome = @Outcome, payload = CAST(@Payload AS jsonb)
        WHERE world_id = @WorldId AND scope = @Scope AND request_id = @RequestId
        """;

    /// <summary>The stored row of an earlier command under this key, if any.</summary>
    public static async Task<StoredCommand?> ReadAsync(NpgsqlConnection connection, NpgsqlTransaction tx, RequestKey key, CancellationToken ct)
        => await connection.QuerySingleOrDefaultAsync<StoredCommand>(new CommandDefinition(StoredSql, key, tx, cancellationToken: ct));

    /// <summary>
    /// Claims the key for this transaction. Returns null when the claim is now held here, or the row
    /// of the command that claimed it first (read after that transaction ended).
    /// </summary>
    public static async Task<StoredCommand?> ClaimAsync(NpgsqlConnection connection, NpgsqlTransaction tx, RequestKey key, byte[] requestHash, CancellationToken ct)
    {
        int claimed = await connection.ExecuteAsync(new CommandDefinition(ClaimSql,
            new { key.WorldId, key.Scope, key.RequestId, RequestHash = requestHash, Outcome = PendingOutcome }, tx, cancellationToken: ct));
        return claimed == 1
            ? null
            : await connection.QuerySingleAsync<StoredCommand>(new CommandDefinition(StoredSql, key, tx, cancellationToken: ct));
    }

    /// <summary>Records the decided outcome and the jsonb payload a retry is answered from.</summary>
    public static async Task StoreAsync(NpgsqlConnection connection, NpgsqlTransaction tx, RequestKey key, string outcome, string payload, CancellationToken ct)
        => await connection.ExecuteAsync(new CommandDefinition(StoreSql,
            new { Outcome = outcome, Payload = payload, key.WorldId, key.Scope, key.RequestId }, tx, cancellationToken: ct));
}

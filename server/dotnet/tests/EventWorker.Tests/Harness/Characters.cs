using Dapper;
using Npgsql;

namespace Sa.EventWorker.Tests.Harness;

/// <summary>Seeds the rows the read model's foreign key points at and reads the read model back.</summary>
internal static class Characters
{
    public sealed record Stats(long Kills, long ItemsAcquired, long GoldEarned, long TradesCompleted, long LastEventSeq);

    private static long _nextWorld = 1_000 + (DateTime.UtcNow.Ticks % 1_000_000);

    /// <summary>A world of its own, so a test that counts or orders a world's ranking sees only its own characters.</summary>
    public static async Task<long> FreshWorldAsync(string connectionString)
    {
        long world = Interlocked.Increment(ref _nextWorld);
        await using var connection = new NpgsqlConnection(connectionString);
        await connection.ExecuteAsync("INSERT INTO game.worlds (world_id, name) VALUES (@world, 'test-' || @world)", new { world });
        return world;
    }

    /// <summary>A real account and character, so character_projections can reference it. Returns the character id.</summary>
    public static async Task<long> CreateAsync(string connectionString, long world = Outbox.World)
    {
        string suffix = Guid.NewGuid().ToString("N")[..12];
        await using var connection = new NpgsqlConnection(connectionString);
        long account = await connection.ExecuteScalarAsync<long>(
            "INSERT INTO game.accounts (username, password_hash) VALUES (@u, 'x') RETURNING account_id", new { u = "ev_" + suffix });
        return await connection.ExecuteScalarAsync<long>(
            "INSERT INTO game.characters (world_id, account_id, name, class_id) VALUES (@w, @a, @n, 1) RETURNING character_id",
            new { w = world, a = account, n = "Ev" + suffix[..8] });
    }

    public static async Task<Stats?> StatsAsync(string connectionString, long characterId, long world = Outbox.World)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return await connection.QuerySingleOrDefaultAsync<Stats>("""
            SELECT kills AS "Kills", items_acquired AS "ItemsAcquired", gold_earned AS "GoldEarned", trades_completed AS "TradesCompleted", last_event_seq AS "LastEventSeq"
            FROM game.character_projections WHERE world_id = @w AND character_id = @c
            """, new { w = world, c = characterId });
    }

    public static async Task<long> InboxCountAsync(string connectionString, string consumerName, byte[] eventId)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return await connection.ExecuteScalarAsync<long>(
            "SELECT count(*) FROM game.inbox_events WHERE consumer_name = @consumerName AND event_id = @eventId", new { consumerName, eventId });
    }
}

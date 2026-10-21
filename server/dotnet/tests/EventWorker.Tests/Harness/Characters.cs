using Dapper;
using Npgsql;

namespace Sa.EventWorker.Tests.Harness;

/// <summary>Seeds the rows the read model's foreign key points at and reads the read model back.</summary>
internal static class Characters
{
    public sealed record Stats(long Kills, long ItemsAcquired, long GoldEarned, long LastEventSeq);

    /// <summary>A real account and character in world 1, so character_projections can reference it. Returns the character id.</summary>
    public static async Task<long> CreateAsync(string connectionString)
    {
        string suffix = Guid.NewGuid().ToString("N")[..12];
        await using var connection = new NpgsqlConnection(connectionString);
        long account = await connection.ExecuteScalarAsync<long>(
            "INSERT INTO game.accounts (username, password_hash) VALUES (@u, 'x') RETURNING account_id", new { u = "ev_" + suffix });
        return await connection.ExecuteScalarAsync<long>(
            "INSERT INTO game.characters (world_id, account_id, name, class_id) VALUES (@w, @a, @n, 1) RETURNING character_id",
            new { w = Outbox.World, a = account, n = "Ev" + suffix[..8] });
    }

    public static async Task<Stats?> StatsAsync(string connectionString, long characterId)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return await connection.QuerySingleOrDefaultAsync<Stats>("""
            SELECT kills AS "Kills", items_acquired AS "ItemsAcquired", gold_earned AS "GoldEarned", last_event_seq AS "LastEventSeq"
            FROM game.character_projections WHERE world_id = @w AND character_id = @c
            """, new { w = Outbox.World, c = characterId });
    }

    public static async Task<long> InboxCountAsync(string connectionString, string consumerName, byte[] eventId)
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return await connection.ExecuteScalarAsync<long>(
            "SELECT count(*) FROM game.inbox_events WHERE consumer_name = @consumerName AND event_id = @eventId", new { consumerName, eventId });
    }
}

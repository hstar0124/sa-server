using Dapper;
using Npgsql;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Direct database reads for assertions: tests check rows, not only status codes (see tests/integration/README.md).</summary>
internal static class Db
{
    public static async Task<T> ScalarAsync<T>(string connectionString, string sql, object? parameters = null)
        where T : notnull
    {
        await using var connection = new NpgsqlConnection(connectionString);
        return await connection.ExecuteScalarAsync<T>(sql, parameters)
               ?? throw new InvalidOperationException("scalar query returned NULL: " + sql);
    }
}

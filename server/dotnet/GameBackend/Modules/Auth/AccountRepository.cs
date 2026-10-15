using Dapper;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Auth;

internal sealed class AccountRepository(NpgsqlDataSource dataSource)
{
    private const string UsernameUniqueConstraint = "accounts_username_unique";

    private const string InsertSql = """
        INSERT INTO game.accounts (username, password_hash)
        VALUES (@Username, @PasswordHash)
        RETURNING account_id
        """;

    private const string SelectByUsernameSql = """
        SELECT account_id AS "Id", username AS "Username", password_hash AS "PasswordHash"
        FROM game.accounts
        WHERE username = @Username
        """;

    private const string UpdatePasswordHashSql = """
        UPDATE game.accounts SET password_hash = @PasswordHash WHERE account_id = @Id
        """;

    /// <summary>Inserts a new account; null when the username is already taken (decided by the UNIQUE constraint, not a prior SELECT).</summary>
    public async Task<AccountId?> TryInsertAsync(string username, string passwordHash, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        try
        {
            long id = await connection.ExecuteScalarAsync<long>(
                new CommandDefinition(InsertSql, new { Username = username, PasswordHash = passwordHash }, cancellationToken: cancellationToken));
            return new AccountId(id);
        }
        catch (PostgresException ex) when (ex.IsUniqueViolation(UsernameUniqueConstraint))
        {
            return null;
        }
    }

    public async Task<Account?> FindByUsernameAsync(string username, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        return await connection.QuerySingleOrDefaultAsync<Account>(
            new CommandDefinition(SelectByUsernameSql, new { Username = username }, cancellationToken: cancellationToken));
    }

    public async Task UpdatePasswordHashAsync(AccountId id, string passwordHash, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            new CommandDefinition(UpdatePasswordHashSql, new { Id = id, PasswordHash = passwordHash }, cancellationToken: cancellationToken));
    }
}

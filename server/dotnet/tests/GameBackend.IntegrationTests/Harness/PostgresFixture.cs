using DbUp;
using DbUp.Engine;
using Testcontainers.PostgreSql;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>
/// One PostgreSQL container per test collection with the repository migrations applied, exactly
/// as the Migrator does in deployment. Tests share the database; they isolate by unique usernames.
/// </summary>
public sealed class PostgresFixture : IAsyncLifetime
{
    private const string Image = "postgres:17.6-alpine";

    private readonly PostgreSqlContainer _container = new PostgreSqlBuilder(Image)
        .WithDatabase("sa_game_test")
        .WithUsername("sa")
        .WithPassword("test-only")
        .Build();

    public string ConnectionString => _container.GetConnectionString();

    public async Task InitializeAsync()
    {
        await _container.StartAsync();

        UpgradeEngine upgrader = DeployChanges.To
            .PostgresqlDatabase(ConnectionString)
            .WithScriptsFromFileSystem(RepositoryPaths.Migrations)
            .WithTransactionPerScript()
            .JournalToPostgresqlTable("public", "schema_migrations")
            .LogToNowhere()
            .Build();

        DatabaseUpgradeResult result = upgrader.PerformUpgrade();
        if (!result.Successful)
        {
            throw new InvalidOperationException($"migrations failed at {result.ErrorScript?.Name}", result.Error);
        }
    }

    public async Task DisposeAsync() => await _container.DisposeAsync();
}

[CollectionDefinition(Name)]
public sealed class SharedPostgres : ICollectionFixture<PostgresFixture>
{
    public const string Name = "postgres";
}

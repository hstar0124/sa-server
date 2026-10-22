using DbUp;
using DbUp.Engine;
using Npgsql;
using Testcontainers.Kafka;
using Testcontainers.PostgreSql;
using Testcontainers.Redis;

namespace Sa.EventWorker.Tests.Harness;

/// <summary>
/// One PostgreSQL (with the repository migrations applied), one KRaft Kafka and one Redis per test
/// collection, the same images the compose file runs. Tests isolate by fresh aggregate ids, topics
/// and worlds.
/// </summary>
public sealed class InfrastructureFixture : IAsyncLifetime
{
    private const string PostgresImage = "postgres:17.6-alpine";
    private const string KafkaImage = "apache/kafka:3.9.0";
    private const string RedisImage = "redis:7.4-alpine";

    private readonly PostgreSqlContainer _postgres = new PostgreSqlBuilder(PostgresImage)
        .WithDatabase("sa_events_test")
        .WithUsername("sa")
        .WithPassword("test-only")
        .Build();

    private readonly KafkaContainer _kafka = new KafkaBuilder(KafkaImage).Build();

    private readonly RedisContainer _redis = new RedisBuilder(RedisImage).Build();

    private NpgsqlDataSource? _dataSource;

    public string ConnectionString => _postgres.GetConnectionString();

    public string BootstrapServers => _kafka.GetBootstrapAddress();

    public string RedisConnectionString => _redis.GetConnectionString();

    public NpgsqlDataSource DataSource => _dataSource ?? throw new InvalidOperationException("fixture not initialised");

    public async Task InitializeAsync()
    {
        await Task.WhenAll(_postgres.StartAsync(), _kafka.StartAsync(), _redis.StartAsync());

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

        _dataSource = NpgsqlDataSource.Create(ConnectionString);
    }

    public async Task DisposeAsync()
    {
        if (_dataSource is not null)
        {
            await _dataSource.DisposeAsync();
        }

        await Task.WhenAll(_postgres.DisposeAsync().AsTask(), _kafka.DisposeAsync().AsTask(), _redis.DisposeAsync().AsTask());
    }
}

[CollectionDefinition(Name)]
public sealed class SharedInfrastructure : ICollectionFixture<InfrastructureFixture>
{
    public const string Name = "infrastructure";
}

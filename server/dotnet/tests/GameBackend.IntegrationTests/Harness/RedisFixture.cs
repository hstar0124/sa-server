using StackExchange.Redis;
using Testcontainers.Redis;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>A Redis of the ranking tests' own, the image the compose file runs, so nothing reaches a Redis on this machine.</summary>
public sealed class RedisFixture : IAsyncLifetime
{
    private const string Image = "redis:7.4-alpine";

    private readonly RedisContainer _container = new RedisBuilder(Image).Build();

    private ConnectionMultiplexer? _connection;

    public string ConnectionString => _container.GetConnectionString();

    public IDatabase Database => (_connection ?? throw new InvalidOperationException("fixture not initialised")).GetDatabase();

    public async Task InitializeAsync()
    {
        await _container.StartAsync();
        _connection = await ConnectionMultiplexer.ConnectAsync(ConnectionString);
    }

    public async Task DisposeAsync()
    {
        if (_connection is not null)
        {
            await _connection.DisposeAsync();
        }

        await _container.DisposeAsync();
    }
}

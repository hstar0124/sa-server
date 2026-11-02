using System.Net;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Auth;

/// <summary>
/// The credential attempt limit counts per client address. Behind a reverse proxy the client's
/// address is in a header; it is believed only when the connection itself comes from a trusted network.
/// </summary>
[Collection(SharedPostgres.Name)]
public sealed class CredentialRateLimitTests(PostgresFixture postgres)
{
    private const int Attempts = 2;
    private const string Header = "CF-Connecting-IP";
    private const string TrustedNetwork = "10.0.0.0/8";
    private const string ClientA = "203.0.113.10";
    private const string ClientB = "203.0.113.11";
    private const string ClientC = "203.0.113.12";

    [Fact]
    public async Task Behind_a_trusted_proxy_attempts_count_per_forwarded_client_address()
    {
        using var factory = new BackendFactory(postgres.ConnectionString, new BackendSettings(
            CredentialAttemptsPerMinute: Attempts, TrustedProxyNetworks: [TrustedNetwork], ClientAddressHeader: Header,
            ConnectionAddress: IPAddress.Parse("10.42.0.7")));
        var api = new ApiClient(factory.CreateClient());

        for (int i = 0; i < Attempts; i++)
        {
            Assert.Equal(HttpStatusCode.Unauthorized, (await api.LoginAsync("nobody", "wrong password", Header, ClientA)).StatusCode);
        }

        Assert.Equal(HttpStatusCode.TooManyRequests, (await api.LoginAsync("nobody", "wrong password", Header, ClientA)).StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await api.LoginAsync("nobody", "wrong password", Header, ClientB)).StatusCode);
    }

    [Theory]
    [InlineData("192.168.1.1")]  // a connection outside the trusted network
    [InlineData(null)]           // no proxy configured at all
    public async Task A_forwarded_address_is_ignored_unless_the_connection_is_trusted(string? connectionAddress)
    {
        bool proxyConfigured = connectionAddress is not null;
        using var factory = new BackendFactory(postgres.ConnectionString, new BackendSettings(
            CredentialAttemptsPerMinute: Attempts,
            TrustedProxyNetworks: proxyConfigured ? [TrustedNetwork] : null,
            ClientAddressHeader: proxyConfigured ? Header : null,
            ConnectionAddress: connectionAddress is null ? null : IPAddress.Parse(connectionAddress)));
        var api = new ApiClient(factory.CreateClient());

        Assert.Equal(HttpStatusCode.Unauthorized, (await api.LoginAsync("nobody", "wrong password", Header, ClientA)).StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await api.LoginAsync("nobody", "wrong password", Header, ClientB)).StatusCode);
        // Three different forwarded addresses, one real one: the limit is the connection's.
        Assert.Equal(HttpStatusCode.TooManyRequests, (await api.LoginAsync("nobody", "wrong password", Header, ClientC)).StatusCode);
    }
}

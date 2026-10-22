using System.Security.Cryptography;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.Configuration;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Knobs a test may turn; everything else keeps the production default.</summary>
public sealed record BackendSettings(
    int MaxCharactersPerWorld = 4,
    TimeSpan? GatewayLeaseDuration = null,
    TimeSpan? ZoneLeaseDuration = null,
    TimeSpan? TicketLifetime = null,
    int? CredentialAttemptsPerMinute = null,
    int? RankingsMaxTop = null,
    int? RankingsDefaultTop = null,
    string? RedisConnectionString = null);

/// <summary>In-process GameBackend bound to the fixture database. Settings mirror production keys.</summary>
public sealed class BackendFactory(string connectionString, BackendSettings? settings = null) : WebApplicationFactory<Program>
{
    private readonly BackendSettings _settings = settings ?? new BackendSettings();

    // Fresh keys per factory: two factories therefore never trust each other's tokens or tickets.
    private readonly string _jwtKey = Convert.ToBase64String(RandomNumberGenerator.GetBytes(32));

    private readonly byte[] _ticketKey = RandomNumberGenerator.GetBytes(32);

    /// <summary>The ticket HMAC key this backend signs with; tests verify tickets against it.</summary>
    public ReadOnlySpan<byte> TicketSigningKey => _ticketKey;

    protected override void ConfigureWebHost(IWebHostBuilder builder)
    {
        ArgumentNullException.ThrowIfNull(builder);
        builder.UseEnvironment("Testing");
        var values = new Dictionary<string, string?>(StringComparer.Ordinal)
        {
            ["Database:ConnectionString"] = connectionString,
            ["Auth:SigningKey"] = _jwtKey,
            ["WorldDirectory:TicketSigningKey"] = Convert.ToBase64String(_ticketKey),
            ["GameData:ContentDir"] = RepositoryPaths.GameData,
            ["Characters:MaxCharactersPerWorld"] = _settings.MaxCharactersPerWorld.ToString(System.Globalization.CultureInfo.InvariantCulture),
            ["Logging:LogLevel:Default"] = "Warning",
            // Tests read the ranking from PostgreSQL unless they bring their own Redis: the default
            // would reach whatever Redis runs on this machine.
            ["Rankings:Backend"] = _settings.RedisConnectionString is null ? "Postgres" : "Redis",
            ["Redis:ConnectionString"] = _settings.RedisConnectionString ?? string.Empty,
        };
        if (_settings.GatewayLeaseDuration is { } gateway)
        {
            values["WorldDirectory:GatewayLeaseDuration"] = gateway.ToString();
        }

        if (_settings.ZoneLeaseDuration is { } zone)
        {
            values["WorldDirectory:ZoneLeaseDuration"] = zone.ToString();
        }

        if (_settings.TicketLifetime is { } ticket)
        {
            values["WorldDirectory:TicketLifetime"] = ticket.ToString();
        }

        if (_settings.RankingsMaxTop is { } maxTop)
        {
            values["Rankings:MaxTop"] = maxTop.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        if (_settings.RankingsDefaultTop is { } defaultTop)
        {
            values["Rankings:DefaultTop"] = defaultTop.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        if (_settings.CredentialAttemptsPerMinute is { } attempts)
        {
            values["Auth:CredentialAttemptsPerMinutePerAddress"] = attempts.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        builder.ConfigureAppConfiguration((_, config) => config.AddInMemoryCollection(values));
    }
}

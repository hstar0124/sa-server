using Microsoft.Extensions.Options;
using Sa.GameBackend.Modules.WorldDirectory;

namespace Sa.GameBackend.Tests.Infrastructure;

public sealed class WorldDirectoryOptionsTests
{
    private static readonly WorldDirectoryOptionsValidator Validator = new();
    private static readonly string Key = Convert.ToBase64String(new byte[WorldDirectoryOptions.MinTicketKeyBytes]);

    [Theory]
    [InlineData("")]
    [InlineData("ws://localhost:7778/game")]
    [InlineData("wss://play.example.test/game")]
    public void Accepts_an_empty_or_a_WebSocket_address(string configured)
    {
        var options = new WorldDirectoryOptions { TicketSigningKey = Key, GatewayWebSocketUrl = configured };

        Assert.True(Validator.Validate(null, options).Succeeded);
        Assert.Equal(configured, options.GatewayWebSocketUrlOrNull()?.ToString() ?? string.Empty);
    }

    [Theory]
    [InlineData("https://play.example.test/game")]  // not a WebSocket scheme
    [InlineData("/game")]                            // relative
    [InlineData("play.example.test:7778")]
    public void Rejects_anything_but_an_absolute_WebSocket_address(string configured)
    {
        var options = new WorldDirectoryOptions { TicketSigningKey = Key, GatewayWebSocketUrl = configured };

        ValidateOptionsResult result = Validator.Validate(null, options);

        Assert.True(result.Failed);
        Assert.Contains("GatewayWebSocketUrl", result.FailureMessage, StringComparison.Ordinal);
    }
}

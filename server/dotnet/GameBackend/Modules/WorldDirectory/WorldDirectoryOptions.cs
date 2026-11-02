using System.ComponentModel.DataAnnotations;
using Microsoft.Extensions.Options;

namespace Sa.GameBackend.Modules.WorldDirectory;

/// <summary>Configuration section "WorldDirectory". The ticket key is shared with the gateways and never checked in.</summary>
internal sealed class WorldDirectoryOptions
{
    public const string SectionName = "WorldDirectory";
    public const int MinTicketKeyBytes = 32;

    /// <summary>Base64 HMAC-SHA256 key for entry tickets; distinct from the JWT key so the two rotate independently.</summary>
    [Required(AllowEmptyStrings = false)]
    public string TicketSigningKey { get; init; } = string.Empty;

    [Range(1, int.MaxValue)]
    public int TicketKeyId { get; init; } = 1;

    [Range(typeof(TimeSpan), "00:00:05", "00:05:00")]
    public TimeSpan TicketLifetime { get; init; } = TimeSpan.FromSeconds(30);

    [Range(typeof(TimeSpan), "00:00:01", "00:05:00")]
    public TimeSpan ZoneLeaseDuration { get; init; } = TimeSpan.FromSeconds(15);

    [Range(typeof(TimeSpan), "00:00:01", "00:05:00")]
    public TimeSpan GatewayLeaseDuration { get; init; } = TimeSpan.FromSeconds(15);

    /// <summary>Address handed to clients together with their ticket.</summary>
    [Required(AllowEmptyStrings = false)]
    public string GatewayHost { get; init; } = "localhost";

    [Range(1, 65535)]
    public int GatewayPort { get; init; } = 7777;

    /// <summary>WebSocket port of the same gateway, for browser clients (ADR-0004).</summary>
    [Range(1, 65535)]
    public int GatewayWebSocketPort { get; init; } = 7778;

    /// <summary>
    /// Complete WebSocket URL browsers connect to (scheme, host and path), for a gateway behind a
    /// TLS-terminating proxy where host and port say nothing about the public address. Empty means
    /// clients build ws://host:port themselves from the two values above.
    /// </summary>
    public string GatewayWebSocketUrl { get; init; } = string.Empty;

    public byte[] DecodedTicketSigningKey() => Convert.FromBase64String(TicketSigningKey);

    public Uri? GatewayWebSocketUrlOrNull() => string.IsNullOrEmpty(GatewayWebSocketUrl) ? null : new Uri(GatewayWebSocketUrl, UriKind.Absolute);

    public static bool IsWebSocketUrl(string value)
        => Uri.TryCreate(value, UriKind.Absolute, out Uri? uri) && (uri.Scheme == Uri.UriSchemeWs || uri.Scheme == Uri.UriSchemeWss);
}

internal sealed class WorldDirectoryOptionsValidator : IValidateOptions<WorldDirectoryOptions>
{
    public ValidateOptionsResult Validate(string? name, WorldDirectoryOptions options)
    {
        byte[] key;
        try
        {
            key = options.DecodedTicketSigningKey();
        }
        catch (FormatException)
        {
            return ValidateOptionsResult.Fail("WorldDirectory:TicketSigningKey must be base64");
        }

        if (key.Length < WorldDirectoryOptions.MinTicketKeyBytes)
        {
            return ValidateOptionsResult.Fail($"WorldDirectory:TicketSigningKey must decode to at least {WorldDirectoryOptions.MinTicketKeyBytes} bytes");
        }

        return options.GatewayWebSocketUrl.Length > 0 && !WorldDirectoryOptions.IsWebSocketUrl(options.GatewayWebSocketUrl)
            ? ValidateOptionsResult.Fail("WorldDirectory:GatewayWebSocketUrl must be empty or an absolute ws:// or wss:// URL")
            : ValidateOptionsResult.Success;
    }
}

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

    public byte[] DecodedTicketSigningKey() => Convert.FromBase64String(TicketSigningKey);
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

        return key.Length < WorldDirectoryOptions.MinTicketKeyBytes
            ? ValidateOptionsResult.Fail($"WorldDirectory:TicketSigningKey must decode to at least {WorldDirectoryOptions.MinTicketKeyBytes} bytes")
            : ValidateOptionsResult.Success;
    }
}

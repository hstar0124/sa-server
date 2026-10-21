using System.ComponentModel.DataAnnotations;
using Microsoft.Extensions.Options;

namespace Sa.GameBackend.Modules.Auth;

/// <summary>Configuration section "Auth". The signing key is a secret and is never checked in.</summary>
internal sealed class AuthOptions
{
    public const string SectionName = "Auth";
    public const int MinSigningKeyBytes = 32;

    /// <summary>Base64 HMAC-SHA256 key, at least 32 bytes once decoded.</summary>
    [Required(AllowEmptyStrings = false)]
    public string SigningKey { get; init; } = string.Empty;

    [Required(AllowEmptyStrings = false)]
    public string Issuer { get; init; } = "sa-backend";

    [Required(AllowEmptyStrings = false)]
    public string Audience { get; init; } = "sa-game";

    [Range(typeof(TimeSpan), "00:01:00", "1.00:00:00")]
    public TimeSpan AccessTokenLifetime { get; init; } = TimeSpan.FromHours(1);

    [Range(8, 128)]
    public int MinPasswordLength { get; init; } = 8;

    /// <summary>
    /// Register/login attempts allowed per client address per minute. Raise it for load generation
    /// from one host; production keeps it low.
    /// </summary>
    [Range(1, 100_000)]
    public int CredentialAttemptsPerMinutePerAddress { get; init; } = 10;

    /// <summary>How far a token's issue and expiry times may be off the server clock and still be accepted.</summary>
    [Range(typeof(TimeSpan), "00:00:00", "00:05:00")]
    public TimeSpan TokenClockSkew { get; init; } = TimeSpan.FromSeconds(30);

    public byte[] DecodedSigningKey() => Convert.FromBase64String(SigningKey);
}

internal sealed class AuthOptionsValidator : IValidateOptions<AuthOptions>
{
    public ValidateOptionsResult Validate(string? name, AuthOptions options)
    {
        byte[] key;
        try
        {
            key = options.DecodedSigningKey();
        }
        catch (FormatException)
        {
            return ValidateOptionsResult.Fail("Auth:SigningKey must be base64");
        }

        return key.Length < AuthOptions.MinSigningKeyBytes
            ? ValidateOptionsResult.Fail($"Auth:SigningKey must decode to at least {AuthOptions.MinSigningKeyBytes} bytes")
            : ValidateOptionsResult.Success;
    }
}

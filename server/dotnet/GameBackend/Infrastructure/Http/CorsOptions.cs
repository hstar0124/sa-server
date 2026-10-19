namespace Sa.GameBackend.Infrastructure.Http;

/// <summary>
/// Browser origins allowed to call the REST API (configuration section "Cors"). Native clients
/// and bots do not need this; it exists for the web viewer. Empty means no browser access.
/// </summary>
internal sealed class CorsOptions
{
    public const string SectionName = "Cors";
    public const string PolicyName = "viewer";

    public IReadOnlyList<string> AllowedOrigins { get; init; } = [];
}

using System.ComponentModel.DataAnnotations;

namespace Sa.GameBackend.Infrastructure.Database;

/// <summary>Connection settings for the authoritative PostgreSQL (configuration section "Database").</summary>
internal sealed class DatabaseOptions
{
    public const string SectionName = "Database";

    [Required(AllowEmptyStrings = false)]
    public string ConnectionString { get; init; } = string.Empty;

    /// <summary>
    /// Upper bound on pooled connections for this process. The total across Backend instances to stay below PostgreSQL's max_connections.
    /// </summary>
    [Range(1, 200)]
    public int MaxPoolSize { get; init; } = 20;
}

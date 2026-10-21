using System.ComponentModel.DataAnnotations;

namespace Sa.EventWorker.Infrastructure;

/// <summary>
/// Connection to the authoritative PostgreSQL (configuration section "Database"): the worker reads
/// the Outbox there. The connection string carries a password, so it comes from the environment
/// (<c>Database__ConnectionString</c>) or user-secrets, never from appsettings.json.
/// </summary>
internal sealed class DatabaseOptions
{
    public const string SectionName = "Database";

    [Required(AllowEmptyStrings = false)]
    public string ConnectionString { get; init; } = string.Empty;

    /// <summary>The publisher holds one connection for its lock and opens one at a time for its queries.</summary>
    [Range(2, 50)]
    public int MaxPoolSize { get; init; } = 5;
}

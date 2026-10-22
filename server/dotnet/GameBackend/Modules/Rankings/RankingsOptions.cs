using System.ComponentModel.DataAnnotations;

namespace Sa.GameBackend.Modules.Rankings;

/// <summary>Where the leaderboard is read from. Both give the same order; they differ in cost (docs/reports/01-ranking.md).</summary>
internal enum RankingBackend
{
    Postgres,
    Redis,
}

/// <summary>Configuration section "Rankings".</summary>
internal sealed class RankingsOptions : IValidatableObject
{
    public const string SectionName = "Rankings";

    public RankingBackend Backend { get; init; } = RankingBackend.Redis;

    /// <summary>Largest leaderboard page a client may ask for; a larger request gets this many.</summary>
    [Range(1, 1000)]
    public int MaxTop { get; init; } = 100;

    /// <summary>The page a client gets when it does not ask for a size.</summary>
    [Range(1, 1000)]
    public int DefaultTop { get; init; } = 20;

    public IEnumerable<ValidationResult> Validate(ValidationContext validationContext)
    {
        if (DefaultTop > MaxTop)
        {
            yield return new ValidationResult("Rankings:DefaultTop must not exceed Rankings:MaxTop", [nameof(DefaultTop)]);
        }
    }
}

/// <summary>Configuration section "Redis"; required when Rankings:Backend is Redis.</summary>
internal sealed class RedisOptions
{
    public const string SectionName = "Redis";

    /// <summary>StackExchange.Redis configuration string, e.g. "localhost:6379,abortConnect=false".</summary>
    public string ConnectionString { get; init; } = string.Empty;
}

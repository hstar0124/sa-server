using System.ComponentModel.DataAnnotations;

namespace Sa.EventWorker.Ranking;

/// <summary>Configuration section "Redis".</summary>
internal sealed class RedisOptions
{
    public const string SectionName = "Redis";

    /// <summary>StackExchange.Redis configuration string, e.g. "localhost:6379,abortConnect=false".</summary>
    [Required(AllowEmptyStrings = false)]
    public string ConnectionString { get; init; } = string.Empty;
}

/// <summary>Configuration section "Ranking".</summary>
internal sealed class RankingOptions
{
    public const string SectionName = "Ranking";

    /// <summary>How often the reconciler looks for a world whose ranking is missing or known to be behind.</summary>
    [Range(typeof(TimeSpan), "00:00:01", "01:00:00")]
    public TimeSpan ReconcileInterval { get; init; } = TimeSpan.FromSeconds(30);

    /// <summary>Statistics rows read and written per round while refilling a ranking.</summary>
    [Range(100, 100_000)]
    public int RebuildBatchSize { get; init; } = 5_000;
}

using System.ComponentModel.DataAnnotations;

namespace Sa.GameBackend.Modules.Trading;

/// <summary>Configuration section "Trading".</summary>
internal sealed class TradingOptions
{
    public const string SectionName = "Trading";

    /// <summary>
    /// An open trade nobody changed for this long is closed by the next operation that meets it
    /// (ADR-0013): a cancel the zone never sent cannot hold the two characters and their items forever.
    /// </summary>
    [Range(1, 3600)]
    public int ExpirySeconds { get; init; } = 60;

    public TimeSpan Expiry => TimeSpan.FromSeconds(ExpirySeconds);
}

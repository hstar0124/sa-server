using System.ComponentModel.DataAnnotations;

namespace Sa.GameBackend.Modules.Economy;

/// <summary>Configuration section "Economy".</summary>
internal sealed class EconomyOptions
{
    public const string SectionName = "Economy";

    /// <summary>
    /// Root of the game-data tree (items, drop tables, economy rules, content version). The Backend
    /// and the zones must load the same revision; a kill reported under another version is refused.
    /// </summary>
    [Required(AllowEmptyStrings = false)]
    public string ContentDir { get; init; } = string.Empty;
}

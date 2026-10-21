using System.ComponentModel.DataAnnotations;

namespace Sa.GameBackend.Infrastructure.GameData;

/// <summary>Configuration section "GameData".</summary>
internal sealed class GameDataOptions
{
    public const string SectionName = "GameData";

    /// <summary>
    /// Root of the game-data tree. The Backend and the zones must load the same revision: a kill
    /// reported under another content version is refused.
    /// </summary>
    [Required(AllowEmptyStrings = false)]
    public string ContentDir { get; init; } = string.Empty;
}

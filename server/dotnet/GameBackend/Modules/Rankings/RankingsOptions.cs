using System.ComponentModel.DataAnnotations;

namespace Sa.GameBackend.Modules.Rankings;

/// <summary>Configuration section "Rankings".</summary>
internal sealed class RankingsOptions : IValidatableObject
{
    public const string SectionName = "Rankings";

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

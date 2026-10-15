using System.ComponentModel.DataAnnotations;

namespace Sa.GameBackend.Modules.Characters;

internal sealed class CharactersOptions
{
    public const string SectionName = "Characters";

    [Range(1, 50)]
    public int MaxCharactersPerWorld { get; init; } = 4;
}

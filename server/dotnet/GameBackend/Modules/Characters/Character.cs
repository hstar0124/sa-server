using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Characters;

internal sealed record Character(CharacterId Id, WorldId WorldId, AccountId AccountId, string Name, ClassId ClassId);

/// <summary>Mirrored by the characters_name_format CHECK constraint.</summary>
internal static class CharacterRules
{
    public const string NamePattern = "^[A-Za-z0-9가-힣]{2,16}$";
    public const string NameRuleText = "2-16 letters, digits or Hangul";
}

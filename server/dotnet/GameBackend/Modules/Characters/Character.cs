using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Characters;

internal sealed record Character(CharacterId Id, WorldId WorldId, AccountId AccountId, string Name, ClassId ClassId);

/// <summary>
/// Statistics the EventWorker derives from the character's confirmed events. They trail the ledger
/// by the time the events take to arrive; <see cref="AsOfEventSeq"/> says how far they have come.
/// </summary>
internal sealed record CharacterStats(long Kills, long ItemsAcquired, long GoldEarned, long TradesCompleted, long AsOfEventSeq);

/// <summary>Mirrored by the characters_name_format CHECK constraint.</summary>
internal static class CharacterRules
{
    public const string NamePattern = "^[A-Za-z0-9가-힣]{2,16}$";
    public const string NameRuleText = "2-16 letters, digits or Hangul";
}

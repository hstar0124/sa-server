using System.Diagnostics.CodeAnalysis;
using System.Globalization;

namespace Sa.GameBackend.Common;

// Strongly typed identifiers. Each wraps the column it names, so that an AccountId can never be
// passed where a CharacterId is expected, nor a zone id where a class id is. Wire formats (JSON
// bodies, protobuf fields) carry plain numbers and are converted at the module boundary.

public readonly record struct AccountId(long Value)
{
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);
}

/// <summary>Also bound from a route segment ({worldId}) through <see cref="TryParse"/>.</summary>
public readonly record struct WorldId(long Value) : IParsable<WorldId>
{
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);

    public static WorldId Parse(string s, IFormatProvider? provider) => new(long.Parse(s, provider));

    public static bool TryParse([NotNullWhen(true)] string? s, IFormatProvider? provider, out WorldId result)
    {
        bool parsed = long.TryParse(s, NumberStyles.None, provider ?? CultureInfo.InvariantCulture, out long value);
        result = new WorldId(value);
        return parsed;
    }
}

/// <summary>Also bound from a route segment ({characterId}) through <see cref="TryParse"/>.</summary>
public readonly record struct CharacterId(long Value) : IParsable<CharacterId>
{
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);

    public static CharacterId Parse(string s, IFormatProvider? provider) => new(long.Parse(s, provider));

    public static bool TryParse([NotNullWhen(true)] string? s, IFormatProvider? provider, out CharacterId result)
    {
        bool parsed = long.TryParse(s, NumberStyles.None, provider ?? CultureInfo.InvariantCulture, out long value);
        result = new CharacterId(value);
        return parsed;
    }
}

public readonly record struct ZoneId(int Value)
{
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);
}

/// <summary>Monotonic ownership generation of a character; it fences off stale owners (see README "소유권과 lease").</summary>
public readonly record struct OwnerEpoch(long Value)
{
    /// <summary>What a character's first entry gets; every later entry raises it by one.</summary>
    public static readonly OwnerEpoch First = new(1);

    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);
}

// Content ids from game-data.

public readonly record struct ClassId(int Value)
{
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);
}

public readonly record struct ItemTemplateId(int Value)
{
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);
}

public readonly record struct MonsterTemplateId(int Value)
{
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);
}

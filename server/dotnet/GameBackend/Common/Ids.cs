namespace Sa.GameBackend.Common;

// Strongly typed identifiers (CLAUDE.md rule 2). Each wraps the bigint primary key of its table so
// that an AccountId can never be passed where a CharacterId is expected.

public readonly record struct AccountId(long Value)
{
    public override string ToString() => Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}

public readonly record struct WorldId(long Value)
{
    public override string ToString() => Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}

public readonly record struct CharacterId(long Value)
{
    public override string ToString() => Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}

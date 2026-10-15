using System.Globalization;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Unique, rule-conforming names so tests sharing one database never collide.</summary>
internal static class Unique
{
    public static string Username(string prefix = "u")
        => $"{prefix}_{Guid.NewGuid():N}"[..Math.Min(24, prefix.Length + 1 + 32)].ToLower(CultureInfo.InvariantCulture);

    public static string CharacterName(string prefix = "c")
        => $"{prefix}{Guid.NewGuid():N}"[..12];
}

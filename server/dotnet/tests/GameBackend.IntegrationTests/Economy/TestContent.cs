using Sa.GameBackend.IntegrationTests.Harness;
using Tomlyn;
using Tomlyn.Serialization;

namespace Sa.GameBackend.IntegrationTests.Economy;

/// <summary>Facts about the shipped game-data that tests must agree with the Backend on, read from the same files.</summary>
internal static class TestContent
{
    public static string Version { get; } = Read<VersionFile>("version.toml").Content.Version;

    public static int InventoryCapacity { get; } = Read<EconomyFile>(Path.Combine("rules", "economy.toml")).Inventory.Capacity;

    private static T Read<T>(string relative)
        where T : class
        => TomlSerializer.Deserialize<T>(File.ReadAllText(Path.Combine(RepositoryPaths.GameData, relative)), TomlSerializerOptions.Default)
           ?? throw new InvalidDataException(relative + " is empty");

    private sealed class VersionFile
    {
        [TomlPropertyName("content")]
        public ContentSection Content { get; set; } = new();
    }

    private sealed class ContentSection
    {
        [TomlPropertyName("version")]
        public string Version { get; set; } = string.Empty;
    }

    private sealed class EconomyFile
    {
        [TomlPropertyName("inventory")]
        public InventorySection Inventory { get; set; } = new();
    }

    private sealed class InventorySection
    {
        [TomlPropertyName("capacity")]
        public int Capacity { get; set; }
    }
}

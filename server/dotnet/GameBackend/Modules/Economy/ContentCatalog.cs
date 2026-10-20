using Tomlyn;
using Tomlyn.Serialization;

namespace Sa.GameBackend.Modules.Economy;

/// <summary>What a monster may yield: the gold credited to the killer and the items the zone is allowed to roll.</summary>
internal sealed record DropTable(int MonsterTemplateId, long Gold, IReadOnlySet<int> ItemTemplateIds)
{
    public bool Allows(int itemTemplateId) => ItemTemplateIds.Contains(itemTemplateId);
}

/// <summary>
/// The slice of game-data the Backend needs to judge a zone's economy requests: the content version,
/// known items, drop tables and the inventory rules. Loaded once at start-up from the same files the
/// zones read, so both sides agree on what a kill is worth (see README "전투").
/// </summary>
internal sealed class ContentCatalog
{
    private readonly IReadOnlySet<int> _items;
    private readonly IReadOnlyDictionary<int, DropTable> _drops;

    private ContentCatalog(string version, int inventoryCapacity, string primaryCurrency, IReadOnlySet<int> items, IReadOnlyDictionary<int, DropTable> drops)
    {
        Version = version;
        InventoryCapacity = inventoryCapacity;
        PrimaryCurrency = primaryCurrency;
        _items = items;
        _drops = drops;
    }

    public string Version { get; }

    public int InventoryCapacity { get; }

    public string PrimaryCurrency { get; }

    public bool KnowsItem(int itemTemplateId) => _items.Contains(itemTemplateId);

    public DropTable? DropTableFor(int monsterTemplateId) => _drops.GetValueOrDefault(monsterTemplateId);

    /// <summary>Reads and cross-validates the content tree; a malformed tree is a deployment error and throws.</summary>
    public static ContentCatalog Load(string contentDir)
    {
        VersionFile version = Read<VersionFile>(contentDir, "version.toml");
        ItemsFile items = Read<ItemsFile>(contentDir, Path.Combine("items", "items.toml"));
        MonstersFile monsters = Read<MonstersFile>(contentDir, Path.Combine("monsters", "monsters.toml"));
        DropsFile drops = Read<DropsFile>(contentDir, Path.Combine("drops", "drops.toml"));
        EconomyFile economy = Read<EconomyFile>(contentDir, Path.Combine("rules", "economy.toml"));

        if (string.IsNullOrEmpty(version.Content.Version))
        {
            throw Invalid("content.version is missing");
        }

        HashSet<int> itemIds = items.Items.Select(i => i.Id).ToHashSet();
        HashSet<int> monsterIds = monsters.Monsters.Select(m => m.TemplateId).ToHashSet();
        var dropTables = new Dictionary<int, DropTable>();
        foreach (DropTableFile table in drops.DropTables)
        {
            if (!monsterIds.Contains(table.MonsterTemplateId))
            {
                throw Invalid($"drop table references unknown monster {table.MonsterTemplateId}");
            }

            HashSet<int> allowed = [];
            foreach (DropEntryFile entry in table.Entries)
            {
                if (!itemIds.Contains(entry.ItemTemplateId))
                {
                    throw Invalid($"drop table for monster {table.MonsterTemplateId} references unknown item {entry.ItemTemplateId}");
                }

                allowed.Add(entry.ItemTemplateId);
            }

            if (table.Gold < 0 || !dropTables.TryAdd(table.MonsterTemplateId, new DropTable(table.MonsterTemplateId, table.Gold, allowed)))
            {
                throw Invalid($"drop table for monster {table.MonsterTemplateId} is duplicated or has negative gold");
            }
        }

        if (economy.Inventory.Capacity <= 0 || string.IsNullOrEmpty(economy.Currency.Primary))
        {
            throw Invalid("inventory.capacity must be positive and currency.primary must be set");
        }

        return new ContentCatalog(version.Content.Version, economy.Inventory.Capacity, economy.Currency.Primary, itemIds, dropTables);
    }

    private static T Read<T>(string dir, string relative)
        where T : class
    {
        string path = Path.Combine(dir, relative);
        if (!File.Exists(path))
        {
            throw Invalid($"content file not found: {path}");
        }

        return TomlSerializer.Deserialize<T>(File.ReadAllText(path), TomlSerializerOptions.Default) ?? throw Invalid($"{path} is empty");
    }

    private static InvalidDataException Invalid(string message) => new("game-data: " + message);

    // ---- file shapes: only the keys the Backend reads; the zone's loader validates the rest.

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

    private sealed class ItemsFile
    {
        [TomlPropertyName("items")]
        public List<ItemFile> Items { get; set; } = [];
    }

    private sealed class ItemFile
    {
        [TomlPropertyName("id")]
        public int Id { get; set; }
    }

    private sealed class MonstersFile
    {
        [TomlPropertyName("monsters")]
        public List<MonsterFile> Monsters { get; set; } = [];
    }

    private sealed class MonsterFile
    {
        [TomlPropertyName("template_id")]
        public int TemplateId { get; set; }
    }

    private sealed class DropsFile
    {
        [TomlPropertyName("drop_tables")]
        public List<DropTableFile> DropTables { get; set; } = [];
    }

    private sealed class DropTableFile
    {
        [TomlPropertyName("monster_template_id")]
        public int MonsterTemplateId { get; set; }

        [TomlPropertyName("gold")]
        public long Gold { get; set; }

        [TomlPropertyName("entries")]
        public List<DropEntryFile> Entries { get; set; } = [];
    }

    private sealed class DropEntryFile
    {
        [TomlPropertyName("item_template_id")]
        public int ItemTemplateId { get; set; }
    }

    private sealed class EconomyFile
    {
        [TomlPropertyName("inventory")]
        public InventorySection Inventory { get; set; } = new();

        [TomlPropertyName("currency")]
        public CurrencySection Currency { get; set; } = new();
    }

    private sealed class InventorySection
    {
        [TomlPropertyName("capacity")]
        public int Capacity { get; set; }
    }

    private sealed class CurrencySection
    {
        [TomlPropertyName("primary")]
        public string Primary { get; set; } = string.Empty;
    }
}

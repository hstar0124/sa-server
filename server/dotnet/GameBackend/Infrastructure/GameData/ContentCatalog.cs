using Tomlyn;
using Tomlyn.Serialization;

namespace Sa.GameBackend.Infrastructure.GameData;

/// <summary>
/// What a monster may yield: the gold credited to the killer and, per item template, how many drop
/// entries name it. The zone rolls every entry once per kill, so no kill yields more of an item than
/// the table has entries for it.
/// </summary>
internal sealed record DropTable(int MonsterTemplateId, long Gold, IReadOnlyDictionary<int, int> EntriesPerItem)
{
    /// <summary>True when every rolled template is in the table and none appears more often than its entries.</summary>
    public bool Accepts(IEnumerable<int> rolledItemTemplateIds)
    {
        foreach (IGrouping<int, int> rolled in rolledItemTemplateIds.GroupBy(id => id))
        {
            if (!EntriesPerItem.TryGetValue(rolled.Key, out int entries) || rolled.Count() > entries)
            {
                return false;
            }
        }

        return true;
    }
}

/// <summary>
/// The slice of game-data the Backend judges requests by: the content version, the player classes a
/// character may be created with, drop tables and the inventory rules. Loaded once at start-up from
/// the same files the zones read, so both sides agree on what a class is and what a kill is worth
/// (see README "전투"). Shared by the modules that need it, hence infrastructure.
/// </summary>
internal sealed class ContentCatalog
{
    private readonly IReadOnlySet<int> _classes;
    private readonly IReadOnlyDictionary<int, DropTable> _drops;

    private ContentCatalog(string version, int inventoryCapacity, string primaryCurrency, IReadOnlySet<int> classes, IReadOnlyDictionary<int, DropTable> drops)
    {
        Version = version;
        InventoryCapacity = inventoryCapacity;
        PrimaryCurrency = primaryCurrency;
        _classes = classes;
        _drops = drops;
    }

    public string Version { get; }

    public int InventoryCapacity { get; }

    public string PrimaryCurrency { get; }

    public bool KnowsClass(int classId) => _classes.Contains(classId);

    public DropTable? DropTableFor(int monsterTemplateId) => _drops.GetValueOrDefault(monsterTemplateId);

    /// <summary>Reads and cross-validates the content tree; a malformed tree is a deployment error and throws.</summary>
    public static ContentCatalog Load(string contentDir)
    {
        VersionFile version = Read<VersionFile>(contentDir, "version.toml");
        ClassesFile classes = Read<ClassesFile>(contentDir, Path.Combine("classes", "classes.toml"));
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

            var entriesPerItem = new Dictionary<int, int>();
            foreach (DropEntryFile entry in table.Entries)
            {
                if (!itemIds.Contains(entry.ItemTemplateId))
                {
                    throw Invalid($"drop table for monster {table.MonsterTemplateId} references unknown item {entry.ItemTemplateId}");
                }

                entriesPerItem[entry.ItemTemplateId] = entriesPerItem.GetValueOrDefault(entry.ItemTemplateId) + 1;
            }

            if (table.Gold < 0 || !dropTables.TryAdd(table.MonsterTemplateId, new DropTable(table.MonsterTemplateId, table.Gold, entriesPerItem)))
            {
                throw Invalid($"drop table for monster {table.MonsterTemplateId} is duplicated or has negative gold");
            }
        }

        if (economy.Inventory.Capacity <= 0 || string.IsNullOrEmpty(economy.Currency.Primary))
        {
            throw Invalid("inventory.capacity must be positive and currency.primary must be set");
        }

        HashSet<int> classIds = classes.Classes.Select(c => c.Id).ToHashSet();
        if (classIds.Count == 0 || classIds.Count != classes.Classes.Count || classIds.Any(id => id <= 0))
        {
            throw Invalid("classes must be present, unique and have positive ids");
        }

        return new ContentCatalog(version.Content.Version, economy.Inventory.Capacity, economy.Currency.Primary, classIds, dropTables);
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

    private sealed class ClassesFile
    {
        [TomlPropertyName("classes")]
        public List<ClassFile> Classes { get; set; } = [];
    }

    private sealed class ClassFile
    {
        [TomlPropertyName("id")]
        public int Id { get; set; }
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

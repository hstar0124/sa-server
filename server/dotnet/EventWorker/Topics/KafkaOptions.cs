using System.ComponentModel.DataAnnotations;

namespace Sa.EventWorker.Topics;

/// <summary>One topic: its name and the partition count it is created with.</summary>
internal sealed class TopicOptions
{
    [Required(AllowEmptyStrings = false)]
    public string Name { get; init; } = string.Empty;

    [Range(1, 64)]
    public int Partitions { get; init; } = 1;
}

/// <summary>Configuration section "Kafka". The development broker is a single node without replication.</summary>
internal sealed class KafkaOptions
{
    public const string SectionName = "Kafka";

    [Required(AllowEmptyStrings = false)]
    public string BootstrapServers { get; init; } = string.Empty;

    [Range(1, 3)]
    public short ReplicationFactor { get; init; } = 1;

    /// <summary>Confirmed facts about one character: kills and acquisitions.</summary>
    [Required]
    public TopicOptions CharacterEvents { get; init; } = new() { Name = "game.character-events.v1", Partitions = 3 };

    /// <summary>Events a consumer could not decode or apply; kept for inspection, never retried blindly.</summary>
    [Required]
    public TopicOptions FailedEvents { get; init; } = new() { Name = "game.failed-events.v1", Partitions = 1 };

    public IEnumerable<TopicOptions> AllTopics => [CharacterEvents, FailedEvents];
}

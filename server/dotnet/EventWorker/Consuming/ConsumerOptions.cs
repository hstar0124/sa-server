using System.ComponentModel.DataAnnotations;

namespace Sa.EventWorker.Consuming;

/// <summary>Configuration section "CharacterStatsConsumer".</summary>
internal sealed class ConsumerOptions
{
    public const string SectionName = "CharacterStatsConsumer";

    /// <summary>Kafka consumer group and Inbox consumer name; renaming it starts the read model over from the topic.</summary>
    [Required(AllowEmptyStrings = false)]
    [RegularExpression("^[a-z][a-z0-9-]*$")]  // the inbox_events_consumer_format CHECK
    public string GroupId { get; init; } = "character-stats";

    [Range(typeof(TimeSpan), "00:00:00.100", "00:00:30")]
    public TimeSpan PollTimeout { get; init; } = TimeSpan.FromSeconds(1);

    /// <summary>Pause before reading a message again whose write failed for a reason that may pass (the database or broker unreachable).</summary>
    [Range(typeof(TimeSpan), "00:00:00.100", "00:01:00")]
    public TimeSpan RetryDelay { get; init; } = TimeSpan.FromSeconds(2);
}

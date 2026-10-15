using System.ComponentModel.DataAnnotations;

namespace Sa.EventWorker.Publishing;

internal sealed class OutboxPublisherOptions
{
    public const string SectionName = "OutboxPublisher";

    /// <summary>How often the publisher looks for unpublished outbox rows when idle.</summary>
    [Range(typeof(TimeSpan), "00:00:00.050", "00:01:00")]
    public TimeSpan PollInterval { get; init; } = TimeSpan.FromMilliseconds(500);
}

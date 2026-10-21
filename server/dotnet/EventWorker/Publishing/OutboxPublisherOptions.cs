using System.ComponentModel.DataAnnotations;

namespace Sa.EventWorker.Publishing;

internal sealed class OutboxPublisherOptions
{
    public const string SectionName = "OutboxPublisher";

    /// <summary>How long the publisher waits after a pass that left nothing to do, and between attempts to become the active publisher.</summary>
    [Range(typeof(TimeSpan), "00:00:00.050", "00:01:00")]
    public TimeSpan PollInterval { get; init; } = TimeSpan.FromMilliseconds(500);

    /// <summary>Rows fetched per pass; a full batch is followed by another pass without waiting.</summary>
    [Range(1, 10_000)]
    public int BatchSize { get; init; } = 200;

    /// <summary>How long one event may take to be acknowledged before it counts as failed and its aggregate waits for the next pass.</summary>
    [Range(typeof(TimeSpan), "00:00:00.100", "00:01:00")]
    public TimeSpan PublishTimeout { get; init; } = TimeSpan.FromSeconds(5);
}

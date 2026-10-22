namespace Sa.Shared.Events;

/// <summary>
/// The aggregate types written to <c>outbox_events.aggregate_type</c>: the Backend writes them, the
/// EventWorker routes by them to a topic. A type here without a route is never published.
/// </summary>
internal static class AggregateTypes
{
    public const string Character = "character";
}

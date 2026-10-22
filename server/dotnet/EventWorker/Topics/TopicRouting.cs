using System.Globalization;
using Microsoft.Extensions.Options;
using Sa.Events.V1;
using Aggregate = Sa.Shared.Events.AggregateTypes;

namespace Sa.EventWorker.Topics;

/// <summary>
/// Where an event goes and under which key. The key keeps one aggregate's events in one partition,
/// so a consumer sees them in sequence order.
/// </summary>
internal sealed class TopicRouting(IOptions<KafkaOptions> options)
{
    private readonly Dictionary<string, string> _topics = new(StringComparer.Ordinal)
    {
        [Aggregate.Character] = options.Value.CharacterEvents.Name,
    };

    /// <summary>The aggregate types the publisher sends; rows of any other type are a programming error and stay unpublished.</summary>
    public IReadOnlyCollection<string> AggregateTypes => _topics.Keys;

    public string TopicFor(string aggregateType) => _topics[aggregateType];

    public static string KeyFor(EventEnvelope envelope)
        => string.Create(CultureInfo.InvariantCulture, $"{envelope.WorldId}:{envelope.AggregateId}");
}

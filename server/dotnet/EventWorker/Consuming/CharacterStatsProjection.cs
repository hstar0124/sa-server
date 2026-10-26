using Sa.Events.V1;

namespace Sa.EventWorker.Consuming;

/// <summary>What one event adds to a character's statistics.</summary>
internal sealed record ProjectionDelta(long Kills, long ItemsAcquired, long GoldEarned, long TradesCompleted);

/// <summary>
/// The read model's rule: which events count and how. Pure: given an envelope, the delta, or null
/// when the event type is not this model's concern. A payload that does not decode throws
/// InvalidProtocolBufferException for the caller to route to the failed-events topic.
/// </summary>
internal static class CharacterStatsProjection
{
    public static ProjectionDelta? DeltaOf(EventEnvelope envelope)
    {
        if (envelope.EventType == KillRecorded.Descriptor.Name)
        {
            KillRecorded kill = KillRecorded.Parser.ParseFrom(envelope.Payload);
            return new ProjectionDelta(Kills: 1, ItemsAcquired: 0, GoldEarned: (long)kill.GoldGranted, TradesCompleted: 0);
        }

        if (envelope.EventType == InventoryItemAcquired.Descriptor.Name)
        {
            _ = InventoryItemAcquired.Parser.ParseFrom(envelope.Payload);  // validate; the count is all this model keeps
            return new ProjectionDelta(Kills: 0, ItemsAcquired: 1, GoldEarned: 0, TradesCompleted: 0);
        }

        // One per participant (ADR-0013). Gold that changed hands is not earned, so it is not added.
        if (envelope.EventType == TradeCompleted.Descriptor.Name)
        {
            _ = TradeCompleted.Parser.ParseFrom(envelope.Payload);  // validate; the count is all this model keeps
            return new ProjectionDelta(Kills: 0, ItemsAcquired: 0, GoldEarned: 0, TradesCompleted: 1);
        }

        return null;
    }
}

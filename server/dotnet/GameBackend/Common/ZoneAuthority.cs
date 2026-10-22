namespace Sa.GameBackend.Common;

/// <summary>
/// Which zone process claims to act for a character, and under which ownership generation (see
/// README "소유권과 lease"). Kills, pickups, checkpoints and transfers all carry it, and the Backend
/// compares it with the ownership row inside the same transaction.
/// </summary>
internal sealed record ZoneAuthority(InstanceId ZoneInstanceId, OwnerEpoch OwnerEpoch)
{
    /// <summary>False when the claim is missing or its instance id is not 16 bytes.</summary>
    public static bool TryFrom(Sa.Backend.V1.ZoneAuthority? proto, out ZoneAuthority authority)
    {
        authority = null!;
        if (proto is null || !InstanceId.TryFrom(proto.ZoneInstanceId.Span, out InstanceId zone))
        {
            return false;
        }

        authority = new ZoneAuthority(zone, new OwnerEpoch((long)proto.OwnerEpoch));
        return true;
    }
}

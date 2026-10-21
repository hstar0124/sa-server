namespace Sa.GameBackend.Common;

/// <summary>
/// 16-byte identifiers minted by processes rather than by the database (ADR-0005, design document
/// 14.1): instance ids, tickets, item instances, kill and request ids. Each wrapper is its own type
/// so that a kill id can never be passed where an item id is expected; this helper holds what they share.
/// </summary>
internal static class OpaqueId
{
    public const int Length = 16;

    public static byte[] New() => Guid.NewGuid().ToByteArray();

    /// <summary>Copies the bytes when they have the right length; false otherwise.</summary>
    public static bool TryCopy(ReadOnlySpan<byte> bytes, out byte[] copy)
    {
        if (bytes.Length != Length)
        {
            copy = [];
            return false;
        }

        copy = bytes.ToArray();
        return true;
    }

    public static string ToHex(ReadOnlySpan<byte> bytes) => Convert.ToHexStringLower(bytes);
}

/// <summary>Identity of one running zone or gateway process; a restart is a new id, never a reused one.</summary>
internal readonly record struct InstanceId(byte[] Bytes)
{
    public static InstanceId New() => new(OpaqueId.New());

    public static bool TryFrom(ReadOnlySpan<byte> bytes, out InstanceId id)
    {
        bool ok = OpaqueId.TryCopy(bytes, out byte[] copy);
        id = new InstanceId(copy);
        return ok;
    }

    public bool SameAs(ReadOnlySpan<byte> other) => other.SequenceEqual(Bytes);
}

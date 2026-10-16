namespace Sa.GameBackend.Modules.WorldDirectory;

/// <summary>16-byte opaque identifiers minted by processes (ADR-0005, see README "소유권과 lease").</summary>
internal readonly record struct InstanceId(byte[] Bytes)
{
    public const int Length = 16;

    public static InstanceId New() => new(Guid.NewGuid().ToByteArray());

    public static bool TryFrom(ReadOnlySpan<byte> bytes, out InstanceId id)
    {
        id = default;
        if (bytes.Length != Length)
        {
            return false;
        }

        id = new InstanceId(bytes.ToArray());
        return true;
    }
}

internal readonly record struct TicketId(byte[] Bytes)
{
    public const int Length = 16;

    public static TicketId New() => new(Guid.NewGuid().ToByteArray());

    public static bool TryFrom(ReadOnlySpan<byte> bytes, out TicketId id)
    {
        id = default;
        if (bytes.Length != Length)
        {
            return false;
        }

        id = new TicketId(bytes.ToArray());
        return true;
    }
}

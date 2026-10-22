using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.WorldDirectory;

/// <summary>Identity of one issued entry ticket; redeemable exactly once.</summary>
internal readonly record struct TicketId(byte[] Bytes)
{
    public static TicketId New() => new(OpaqueId.New());

    public static bool TryFrom(ReadOnlySpan<byte> bytes, out TicketId id)
    {
        bool ok = OpaqueId.TryCopy(bytes, out byte[] copy);
        id = new TicketId(copy);
        return ok;
    }
}

/// <summary>Identity of one zone transfer attempt; the origin zone mints it and its retries reuse it.</summary>
internal readonly record struct TransferId(byte[] Bytes)
{
    public static bool TryFrom(ReadOnlySpan<byte> bytes, out TransferId id)
    {
        bool ok = OpaqueId.TryCopy(bytes, out byte[] copy);
        id = new TransferId(copy);
        return ok;
    }
}

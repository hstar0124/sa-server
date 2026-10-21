using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Economy;

/// <summary>Identity of one physical item; stays with the item from drop to inventory to trade.</summary>
internal readonly record struct ItemInstanceId(byte[] Bytes)
{
    public static ItemInstanceId New() => new(OpaqueId.New());

    public static bool TryFrom(ReadOnlySpan<byte> bytes, out ItemInstanceId id)
    {
        bool ok = OpaqueId.TryCopy(bytes, out byte[] copy);
        id = new ItemInstanceId(copy);
        return ok;
    }
}

/// <summary>Minted by the zone once per monster lifecycle; the grant basis of a kill (see README "자산과 멱등성").</summary>
internal readonly record struct KillId(byte[] Bytes)
{
    public static bool TryFrom(ReadOnlySpan<byte> bytes, out KillId id)
    {
        bool ok = OpaqueId.TryCopy(bytes, out byte[] copy);
        id = new KillId(copy);
        return ok;
    }
}

/// <summary>Idempotency key of one client-visible command; a retry carries the same id (see README "자산과 멱등성").</summary>
internal readonly record struct RequestId(byte[] Bytes)
{
    public static bool TryFrom(ReadOnlySpan<byte> bytes, out RequestId id)
    {
        bool ok = OpaqueId.TryCopy(bytes, out byte[] copy);
        id = new RequestId(copy);
        return ok;
    }
}

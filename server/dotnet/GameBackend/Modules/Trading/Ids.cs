using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Trading;

/// <summary>Identity of one negotiation between two characters (ADR-0013); minted by the Backend when the trade opens.</summary>
internal readonly record struct TradeId(byte[] Bytes)
{
    public static TradeId New() => new(OpaqueId.New());

    public static bool TryFrom(ReadOnlySpan<byte> bytes, out TradeId id)
    {
        bool ok = OpaqueId.TryCopy(bytes, out byte[] copy);
        id = new TradeId(copy);
        return ok;
    }
}

using System.Security.Cryptography;
using Google.Protobuf;
using Microsoft.Extensions.Options;
using Sa.Backend.V1;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.WorldDirectory;

internal sealed record IssuedTicket(byte[] SignedTicket, TicketId TicketId, DateTimeOffset ExpiresAt);

/// <summary>
/// Issues HMAC-signed entry tickets (ADR-0005). The ticket row is written first so that the
/// database clock defines issue and expiry; the signed bytes carry the same instants.
/// </summary>
internal sealed class EntryTicketIssuer(WorldDirectoryRepository repository, IOptions<WorldDirectoryOptions> options)
{
    private readonly byte[] _key = options.Value.DecodedTicketSigningKey();
    private readonly uint _keyId = (uint)options.Value.TicketKeyId;

    public async Task<IssuedTicket> IssueAsync(AccountId accountId, CharacterId characterId, WorldId worldId, CancellationToken cancellationToken)
    {
        TicketId ticketId = TicketId.New();
        (DateTimeOffset issuedAt, DateTimeOffset expiresAt) = await repository.InsertTicketAsync(
            ticketId, accountId, characterId, worldId, options.Value.TicketLifetime, cancellationToken);

        var ticket = new EntryTicket
        {
            TicketId = ByteString.CopyFrom(ticketId.Bytes),
            AccountId = (ulong)accountId.Value,
            CharacterId = (ulong)characterId.Value,
            WorldId = (ulong)worldId.Value,
            IssuedAtMs = (ulong)issuedAt.ToUnixTimeMilliseconds(),
            ExpiresAtMs = (ulong)expiresAt.ToUnixTimeMilliseconds(),
            KeyId = _keyId,
        };
        byte[] ticketBytes = ticket.ToByteArray();
        var signed = new SignedEntryTicket
        {
            Ticket = ByteString.CopyFrom(ticketBytes),
            Signature = ByteString.CopyFrom(HMACSHA256.HashData(_key, ticketBytes)),
        };
        return new IssuedTicket(signed.ToByteArray(), ticketId, expiresAt);
    }
}

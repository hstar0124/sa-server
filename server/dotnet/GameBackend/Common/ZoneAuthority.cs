using Dapper;
using Npgsql;

namespace Sa.GameBackend.Common;

/// <summary>
/// Which zone process claims to act for a character, and under which ownership generation (see
/// README "소유권과 lease"). Kills, pickups, trades, checkpoints and transfers all carry it, and the
/// Backend compares it with the ownership row inside the same transaction.
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

/// <summary>A character's ownership row as a zone's claim is judged against, with the liveness of the owning zone's lease.</summary>
internal sealed record CharacterOwner(ZoneId ZoneId, byte[] ZoneInstanceId, OwnerEpoch OwnerEpoch, string State, bool ZoneAlive)
{
    private const string ActiveState = "active";

    /// <summary>The claimant is the current owner: the same live zone instance under the same generation.</summary>
    public bool IsHeldBy(ZoneAuthority authority) => IsSimulatedBy(authority.ZoneInstanceId) && OwnerEpoch == authority.OwnerEpoch;

    /// <summary>The character stands in this live zone instance, under whatever generation it entered with.</summary>
    public bool IsSimulatedBy(InstanceId zoneInstance)
        => string.Equals(State, ActiveState, StringComparison.Ordinal) && ZoneAlive && zoneInstance.SameAs(ZoneInstanceId);
}

/// <summary>Reads ownership rows inside the caller's transaction, for the modules that act on a zone's behalf.</summary>
internal static class CharacterOwners
{
    // FOR SHARE: a concurrent redeem (which takes FOR UPDATE on the same row) waits until the caller
    // has committed, so a hand-over to another zone can never interleave with a change made under the
    // old one. The zone's lease must be live too: an expired lease ends the zone's authority even
    // while the ownership row still names it.
    private const string OwnershipSql = """
        SELECT o.zone_id AS "ZoneId", o.zone_instance_id AS "ZoneInstanceId", o.owner_epoch AS "OwnerEpoch", o.state AS "State",
               (l.lease_expires_at > now()) AS "ZoneAlive"
        FROM game.character_ownership o
        JOIN game.zone_leases l ON l.zone_instance_id = o.zone_instance_id
        WHERE o.world_id = @WorldId AND o.character_id = @CharacterId
        FOR SHARE OF o
        """;

    /// <summary>The character's ownership row, held shared until the transaction ends; null when it has never entered.</summary>
    public static async Task<CharacterOwner?> ReadForShareAsync(NpgsqlConnection connection, NpgsqlTransaction tx, WorldId worldId, CharacterId characterId, CancellationToken ct)
        => await connection.QuerySingleOrDefaultAsync<CharacterOwner>(
            new CommandDefinition(OwnershipSql, new { WorldId = worldId, CharacterId = characterId }, tx, cancellationToken: ct));
}

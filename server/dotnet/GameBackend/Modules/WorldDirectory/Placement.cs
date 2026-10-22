using System.Data;
using Dapper;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.WorldDirectory;

/// <summary>Where a character last stood, as its owning zone saved it.</summary>
internal sealed record Checkpoint(ZoneId ZoneId, float X, float Y, OwnerEpoch OwnerEpoch, long Version);

internal enum CheckpointOutcome
{
    Saved,
    StaleAuthority,  // the claimant does not own the character (any more), or not in that zone
    StaleVersion,    // the same owner already stored a newer checkpoint
}

internal sealed record TransferRequest(
    TransferId TransferId,
    WorldId WorldId,
    CharacterId CharacterId,
    ZoneAuthority From,
    ZoneId ToZoneId,
    float ArrivalX,
    float ArrivalY);

internal enum TransferOutcome
{
    Transferred,
    AlreadyTransferred,
    StaleAuthority,
    NoZoneAvailable,
    SameZone,
}

internal sealed record TransferResult(TransferOutcome Outcome, ZoneAssignment? To)
{
    public static TransferResult Refused(TransferOutcome outcome) => new(outcome, null);
}

/// <summary>
/// Checkpoints and zone transfers (ADR-0010). Both check the claimant against the ownership row in
/// the same transaction as their write, so a zone that lost the character in between cannot slip
/// through. The claimant's lease must be live as well: an expired lease ends a zone's authority
/// even while the ownership row still names it, as it does for kills and pickups.
/// </summary>
internal sealed class PlacementRepository(NpgsqlDataSource dataSource)
{
    private const string TransferPrimaryKey = "zone_transfers_pkey";

    // FOR SHARE for a checkpoint: a concurrent entry or transfer (FOR UPDATE) waits for it.
    private const string OwnershipForShareSql = """
        SELECT o.zone_id AS "ZoneId", o.zone_instance_id AS "ZoneInstanceId", o.owner_epoch AS "OwnerEpoch",
               (l.lease_expires_at > now()) AS "ZoneAlive"
        FROM game.character_ownership o
        JOIN game.zone_leases l ON l.zone_instance_id = o.zone_instance_id
        WHERE o.world_id = @WorldId AND o.character_id = @CharacterId
        FOR SHARE OF o
        """;

    private const string OwnershipForUpdateSql = """
        SELECT o.zone_id AS "ZoneId", o.zone_instance_id AS "ZoneInstanceId", o.owner_epoch AS "OwnerEpoch",
               (l.lease_expires_at > now()) AS "ZoneAlive"
        FROM game.character_ownership o
        JOIN game.zone_leases l ON l.zone_instance_id = o.zone_instance_id
        WHERE o.world_id = @WorldId AND o.character_id = @CharacterId
        FOR UPDATE OF o
        """;

    // A newer owner always wins; the same owner must advance its version.
    private const string UpsertCheckpointSql = """
        INSERT INTO game.character_checkpoints (world_id, character_id, zone_id, pos_x, pos_y, owner_epoch, checkpoint_version)
        VALUES (@WorldId, @CharacterId, @ZoneId, @X, @Y, @OwnerEpoch, @Version)
        ON CONFLICT (world_id, character_id) DO UPDATE
            SET zone_id = EXCLUDED.zone_id, pos_x = EXCLUDED.pos_x, pos_y = EXCLUDED.pos_y,
                owner_epoch = EXCLUDED.owner_epoch, checkpoint_version = EXCLUDED.checkpoint_version, saved_at = now()
            WHERE EXCLUDED.owner_epoch > game.character_checkpoints.owner_epoch
               OR (EXCLUDED.owner_epoch = game.character_checkpoints.owner_epoch
                   AND EXCLUDED.checkpoint_version > game.character_checkpoints.checkpoint_version)
        """;

    // Renewal keeps one live instance per zone id; newest first all the same.
    private const string LiveZoneSql = """
        SELECT zone_instance_id FROM game.zone_leases
        WHERE world_id = @WorldId AND zone_id = @ZoneId AND lease_expires_at > now()
        ORDER BY first_seen_at DESC LIMIT 1
        """;

    private const string MoveOwnershipSql = """
        UPDATE game.character_ownership
        SET zone_id = @ZoneId, zone_instance_id = @ZoneInstanceId, owner_epoch = owner_epoch + 1, state = 'active', updated_at = now()
        WHERE world_id = @WorldId AND character_id = @CharacterId
        RETURNING owner_epoch
        """;

    // The live session follows the ownership generation, as an entry would have set it.
    private const string RetagSessionSql = """
        UPDATE game.character_sessions SET owner_epoch = @OwnerEpoch
        WHERE world_id = @WorldId AND character_id = @CharacterId AND ended_at IS NULL
        """;

    private const string StoredTransferSql = """
        SELECT outcome AS "Outcome", to_zone_id AS "ToZoneId", to_zone_instance_id AS "ToZoneInstanceId", to_owner_epoch AS "ToOwnerEpoch"
        FROM game.zone_transfers WHERE transfer_id = @TransferId
        """;

    private const string InsertTransferSql = """
        INSERT INTO game.zone_transfers
            (transfer_id, world_id, character_id, from_zone_instance_id, from_owner_epoch, to_zone_id, to_zone_instance_id, to_owner_epoch, outcome)
        VALUES (@TransferId, @WorldId, @CharacterId, @FromInstance, @FromEpoch, @ToZoneId, @ToInstance, @ToEpoch, @Outcome)
        """;

    // An arrival point is the new owner's first checkpoint, version 0, so the owner's own first save (1) is newer.
    private const long ArrivalCheckpointVersion = 0;

    public async Task<CheckpointOutcome> SaveCheckpointAsync(
        WorldId worldId, CharacterId characterId, ZoneAuthority authority, ZoneId zoneId, float x, float y, long version, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        await using NpgsqlTransaction tx = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, ct);
        OwnershipRow? owner = await connection.QuerySingleOrDefaultAsync<OwnershipRow>(Cmd(OwnershipForShareSql, new { WorldId = worldId, CharacterId = characterId }, tx, ct));
        if (!Holds(owner, authority) || owner!.ZoneId != zoneId)
        {
            return CheckpointOutcome.StaleAuthority;
        }

        int rows = await connection.ExecuteAsync(Cmd(UpsertCheckpointSql, new
        {
            WorldId = worldId,
            CharacterId = characterId,
            ZoneId = zoneId,
            X = x,
            Y = y,
            authority.OwnerEpoch,
            Version = version,
        }, tx, ct));
        await tx.CommitAsync(ct);
        return rows == 1 ? CheckpointOutcome.Saved : CheckpointOutcome.StaleVersion;
    }

    public async Task<TransferResult> TransferAsync(TransferRequest request, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        await using NpgsqlTransaction tx = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, ct);
        if (await connection.QuerySingleOrDefaultAsync<StoredTransfer>(Cmd(StoredTransferSql, new { TransferId = request.TransferId.Bytes }, tx, ct)) is { } stored)
        {
            return stored.ToResult(request.WorldId);
        }

        OwnershipRow? owner = await connection.QuerySingleOrDefaultAsync<OwnershipRow>(Cmd(OwnershipForUpdateSql, new { request.WorldId, request.CharacterId }, tx, ct));
        if (!Holds(owner, request.From))
        {
            return await RecordRefusalAsync(connection, tx, request, TransferOutcome.StaleAuthority, ct);
        }

        if (owner!.ZoneId == request.ToZoneId)
        {
            return await RecordRefusalAsync(connection, tx, request, TransferOutcome.SameZone, ct);
        }

        byte[]? destination = await connection.ExecuteScalarAsync<byte[]?>(Cmd(LiveZoneSql, new { request.WorldId, ZoneId = request.ToZoneId }, tx, ct));
        if (destination is null)
        {
            return await RecordRefusalAsync(connection, tx, request, TransferOutcome.NoZoneAvailable, ct);
        }

        var epoch = new OwnerEpoch(await connection.ExecuteScalarAsync<long>(Cmd(MoveOwnershipSql,
            new { request.WorldId, request.CharacterId, ZoneId = request.ToZoneId, ZoneInstanceId = destination }, tx, ct)));
        await connection.ExecuteAsync(Cmd(UpsertCheckpointSql, new
        {
            request.WorldId,
            request.CharacterId,
            ZoneId = request.ToZoneId,
            X = request.ArrivalX,
            Y = request.ArrivalY,
            OwnerEpoch = epoch,
            Version = ArrivalCheckpointVersion,
        }, tx, ct));
        await connection.ExecuteAsync(Cmd(RetagSessionSql, new { OwnerEpoch = epoch, request.WorldId, request.CharacterId }, tx, ct));
        try
        {
            await connection.ExecuteAsync(Cmd(InsertTransferSql, TransferRow(request, destination, epoch, TransferOutcome.Transferred), tx, ct));
        }
        catch (PostgresException ex) when (ex.IsUniqueViolation(TransferPrimaryKey))
        {
            // A concurrent retry of the same transfer committed first; its decision stands.
            await tx.RollbackAsync(ct);
            return await StoredAsync(request, ct);
        }

        await tx.CommitAsync(ct);
        return new TransferResult(TransferOutcome.Transferred, new ZoneAssignment(request.WorldId, request.ToZoneId, new InstanceId(destination), epoch));
    }

    private static bool Holds(OwnershipRow? owner, ZoneAuthority claim)
        => owner is { ZoneAlive: true } && owner.OwnerEpoch == claim.OwnerEpoch && claim.ZoneInstanceId.SameAs(owner.ZoneInstanceId);

    private async Task<TransferResult> RecordRefusalAsync(NpgsqlConnection connection, NpgsqlTransaction tx, TransferRequest request, TransferOutcome outcome, CancellationToken ct)
    {
        try
        {
            await connection.ExecuteAsync(Cmd(InsertTransferSql, TransferRow(request, null, null, outcome), tx, ct));
            await tx.CommitAsync(ct);
            return TransferResult.Refused(outcome);
        }
        catch (PostgresException ex) when (ex.IsUniqueViolation(TransferPrimaryKey))
        {
            await tx.RollbackAsync(ct);
            return await StoredAsync(request, ct);
        }
    }

    private async Task<TransferResult> StoredAsync(TransferRequest request, CancellationToken ct)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(ct);
        StoredTransfer stored = await connection.QuerySingleAsync<StoredTransfer>(new CommandDefinition(StoredTransferSql, new { TransferId = request.TransferId.Bytes }, cancellationToken: ct));
        return stored.ToResult(request.WorldId);
    }

    private static object TransferRow(TransferRequest request, byte[]? toInstance, OwnerEpoch? toEpoch, TransferOutcome outcome) => new
    {
        TransferId = request.TransferId.Bytes,
        request.WorldId,
        request.CharacterId,
        FromInstance = request.From.ZoneInstanceId.Bytes,
        FromEpoch = request.From.OwnerEpoch,
        request.ToZoneId,
        ToInstance = toInstance,
        ToEpoch = toEpoch?.Value,
        Outcome = OutcomeName(outcome),
    };

    private static string OutcomeName(TransferOutcome outcome) => outcome switch
    {
        TransferOutcome.Transferred => "transferred",
        TransferOutcome.StaleAuthority => "stale_authority",
        TransferOutcome.NoZoneAvailable => "no_zone_available",
        TransferOutcome.SameZone => "same_zone",
        _ => throw new InvalidOperationException($"not a stored outcome: {outcome}"),
    };

    private static CommandDefinition Cmd(string sql, object parameters, NpgsqlTransaction tx, CancellationToken ct)
        => new(sql, parameters, tx, cancellationToken: ct);

    private sealed record OwnershipRow(ZoneId ZoneId, byte[] ZoneInstanceId, OwnerEpoch OwnerEpoch, bool ZoneAlive);

    private sealed record StoredTransfer(string Outcome, ZoneId ToZoneId, byte[]? ToZoneInstanceId, long? ToOwnerEpoch)
    {
        /// <summary>A repeated transfer id gets the first decision: a move is reported as already done, a refusal as itself.</summary>
        public TransferResult ToResult(WorldId worldId) => Outcome switch
        {
            "transferred" => new TransferResult(TransferOutcome.AlreadyTransferred,
                new ZoneAssignment(worldId, ToZoneId, new InstanceId(ToZoneInstanceId!), new OwnerEpoch(ToOwnerEpoch!.Value))),
            "stale_authority" => TransferResult.Refused(TransferOutcome.StaleAuthority),
            "no_zone_available" => TransferResult.Refused(TransferOutcome.NoZoneAvailable),
            "same_zone" => TransferResult.Refused(TransferOutcome.SameZone),
            _ => throw new InvalidOperationException($"unknown stored transfer outcome {Outcome}"),
        };
    }
}

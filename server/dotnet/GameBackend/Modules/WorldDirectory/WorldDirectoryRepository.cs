using System.Data;
using Dapper;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.WorldDirectory;

internal enum RedeemOutcome
{
    Granted,
    AlreadyRedeemed,
    UnknownTicket,
    Expired,
    ActiveSessionExists,
    CharacterNotOwned,
    NoZoneAvailable,
    UnknownGateway,
}

internal sealed record ZoneAssignment(WorldId WorldId, int ZoneId, InstanceId ZoneInstanceId, long OwnerEpoch);

internal sealed record CharacterProfile(CharacterId Id, string Name, int ClassId);

internal sealed record RedeemResult(RedeemOutcome Outcome, ZoneAssignment? Zone, CharacterProfile? Character);

internal sealed record RedeemRequest(
    TicketId TicketId,
    AccountId AccountId,
    CharacterId CharacterId,
    WorldId WorldId,
    InstanceId GatewayInstanceId,
    InstanceId GatewaySessionId);

/// <summary>
/// SQL of the world directory. Every decision about tickets, sessions and ownership is made inside
/// one transaction here, against the database clock, so two gateways cannot both win.
/// </summary>
internal sealed class WorldDirectoryRepository(NpgsqlDataSource dataSource)
{
    private const string LiveSessionIndex = "character_sessions_one_live_per_character";

    // ---- tickets --------------------------------------------------------------------------

    private const string InsertTicketSql = """
        INSERT INTO game.entry_tickets (ticket_id, account_id, character_id, world_id, expires_at)
        VALUES (@TicketId, @AccountId, @CharacterId, @WorldId, now() + @Lifetime)
        RETURNING issued_at, expires_at
        """;

    public async Task<(DateTimeOffset IssuedAt, DateTimeOffset ExpiresAt)> InsertTicketAsync(
        TicketId ticketId, AccountId accountId, CharacterId characterId, WorldId worldId, TimeSpan lifetime, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        var row = await connection.QuerySingleAsync<(DateTime IssuedAt, DateTime ExpiresAt)>(new CommandDefinition(
            InsertTicketSql,
            new { TicketId = ticketId.Bytes, AccountId = accountId, CharacterId = characterId, WorldId = worldId, Lifetime = lifetime },
            cancellationToken: cancellationToken));
        return (new DateTimeOffset(row.IssuedAt, TimeSpan.Zero), new DateTimeOffset(row.ExpiresAt, TimeSpan.Zero));
    }

    // ---- leases ---------------------------------------------------------------------------

    private const string RenewZoneLeaseSql = """
        INSERT INTO game.zone_leases (zone_instance_id, world_id, zone_id, lease_expires_at)
        VALUES (@InstanceId, @WorldId, @ZoneId, now() + @Duration)
        ON CONFLICT (zone_instance_id) DO UPDATE
            SET lease_expires_at = EXCLUDED.lease_expires_at
            WHERE game.zone_leases.world_id = EXCLUDED.world_id AND game.zone_leases.zone_id = EXCLUDED.zone_id
        RETURNING lease_expires_at
        """;

    /// <summary>Null when the instance id is already bound to a different world/zone: an instance never changes identity.</summary>
    public async Task<DateTimeOffset?> RenewZoneLeaseAsync(InstanceId instanceId, WorldId worldId, int zoneId, TimeSpan duration, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        DateTime? expires = await connection.ExecuteScalarAsync<DateTime?>(new CommandDefinition(
            RenewZoneLeaseSql, new { InstanceId = instanceId.Bytes, WorldId = worldId, ZoneId = zoneId, Duration = duration }, cancellationToken: cancellationToken));
        return expires is null ? null : new DateTimeOffset(expires.Value, TimeSpan.Zero);
    }

    private const string RenewGatewayLeaseSql = """
        INSERT INTO game.gateway_leases (gateway_instance_id, lease_expires_at)
        VALUES (@InstanceId, now() + @Duration)
        ON CONFLICT (gateway_instance_id) DO UPDATE SET lease_expires_at = EXCLUDED.lease_expires_at
        RETURNING lease_expires_at
        """;

    public async Task<DateTimeOffset> RenewGatewayLeaseAsync(InstanceId instanceId, TimeSpan duration, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        DateTime expires = await connection.ExecuteScalarAsync<DateTime>(new CommandDefinition(
            RenewGatewayLeaseSql, new { InstanceId = instanceId.Bytes, Duration = duration }, cancellationToken: cancellationToken));
        return new DateTimeOffset(expires, TimeSpan.Zero);
    }

    // ---- sessions -------------------------------------------------------------------------

    private const string EndSessionSql = """
        UPDATE game.character_sessions
        SET ended_at = now(), end_reason = @Reason
        WHERE gateway_session_id = @SessionId AND ended_at IS NULL
        """;

    public async Task<bool> EndSessionAsync(InstanceId sessionId, string reason, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        int rows = await connection.ExecuteAsync(new CommandDefinition(
            EndSessionSql, new { SessionId = sessionId.Bytes, Reason = reason }, cancellationToken: cancellationToken));
        return rows == 1;
    }

    // ---- redemption -----------------------------------------------------------------------

    private const string GatewayAliveSql = """
        SELECT 1 FROM game.gateway_leases WHERE gateway_instance_id = @GatewayInstanceId AND lease_expires_at > now()
        """;

    private const string ConsumeTicketSql = """
        UPDATE game.entry_tickets
        SET redeemed_at = now()
        WHERE ticket_id = @TicketId AND redeemed_at IS NULL AND expires_at > now()
        RETURNING account_id AS "AccountId", character_id AS "CharacterId", world_id AS "WorldId"
        """;

    private const string ExplainTicketSql = """
        SELECT redeemed_at IS NOT NULL AS "Redeemed", expires_at <= now() AS "Expired"
        FROM game.entry_tickets WHERE ticket_id = @TicketId
        """;

    private const string CharacterProfileSql = """
        SELECT c.character_id AS "Id", c.name AS "Name", c.class_id AS "ClassId"
        FROM game.characters c
        WHERE c.character_id = @CharacterId AND c.account_id = @AccountId AND c.world_id = @WorldId
        """;

    // Sessions whose gateway lease lapsed are dead even though nobody called EndSession.
    private const string ReapDeadSessionsSql = """
        UPDATE game.character_sessions s
        SET ended_at = now(), end_reason = 'gateway_lease_expired'
        FROM game.gateway_leases g
        WHERE s.gateway_instance_id = g.gateway_instance_id
          AND s.character_id = @CharacterId AND s.ended_at IS NULL
          AND g.lease_expires_at <= now()
        """;

    private const string LiveSessionExistsSql = """
        SELECT 1 FROM game.character_sessions WHERE character_id = @CharacterId AND ended_at IS NULL
        """;

    private const string CurrentOwnershipSql = """
        SELECT o.zone_id AS "ZoneId", o.zone_instance_id AS "ZoneInstanceId", o.owner_epoch AS "OwnerEpoch",
               (l.lease_expires_at > now()) AS "ZoneAlive"
        FROM game.character_ownership o
        JOIN game.zone_leases l ON l.zone_instance_id = o.zone_instance_id
        WHERE o.world_id = @WorldId AND o.character_id = @CharacterId
        FOR UPDATE OF o
        """;

    // Stage 3 placement: the lowest zone id with a live lease serves the whole world. Stage 8
    // replaces this with the character's checkpointed zone and transfer state.
    private const string PickZoneSql = """
        SELECT zone_id AS "ZoneId", zone_instance_id AS "ZoneInstanceId"
        FROM game.zone_leases
        WHERE world_id = @WorldId AND lease_expires_at > now()
        ORDER BY zone_id, first_seen_at
        LIMIT 1
        """;

    private const string UpsertOwnershipSql = """
        INSERT INTO game.character_ownership (world_id, character_id, zone_id, zone_instance_id, owner_epoch, state)
        VALUES (@WorldId, @CharacterId, @ZoneId, @ZoneInstanceId, 1, 'active')
        ON CONFLICT (world_id, character_id) DO UPDATE
            SET zone_id = EXCLUDED.zone_id,
                zone_instance_id = EXCLUDED.zone_instance_id,
                owner_epoch = game.character_ownership.owner_epoch + 1,
                state = 'active',
                updated_at = now()
        RETURNING owner_epoch
        """;

    private const string InsertSessionSql = """
        INSERT INTO game.character_sessions
            (gateway_session_id, gateway_instance_id, world_id, character_id, account_id, owner_epoch)
        VALUES (@SessionId, @GatewayInstanceId, @WorldId, @CharacterId, @AccountId, @OwnerEpoch)
        """;

    public async Task<RedeemResult> RedeemEntryTicketAsync(RedeemRequest request, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction tx = await connection.BeginTransactionAsync(IsolationLevel.ReadCommitted, cancellationToken);

        if (await connection.ExecuteScalarAsync<int?>(Cmd(GatewayAliveSql, new { GatewayInstanceId = request.GatewayInstanceId.Bytes }, tx, cancellationToken)) is null)
        {
            return Fail(RedeemOutcome.UnknownGateway);
        }

        TicketRow? ticket = await connection.QuerySingleOrDefaultAsync<TicketRow>(Cmd(ConsumeTicketSql, new { TicketId = request.TicketId.Bytes }, tx, cancellationToken));
        if (ticket is null)
        {
            TicketState? state = await connection.QuerySingleOrDefaultAsync<TicketState>(Cmd(ExplainTicketSql, new { TicketId = request.TicketId.Bytes }, tx, cancellationToken));
            return Fail(state switch
            {
                null => RedeemOutcome.UnknownTicket,
                { Redeemed: true } => RedeemOutcome.AlreadyRedeemed,
                _ => RedeemOutcome.Expired,
            });
        }

        // The ticket is consumed even when the request lies about who it is for: a mismatch is tampering.
        bool matches = ticket.AccountId == request.AccountId && ticket.CharacterId == request.CharacterId && ticket.WorldId == request.WorldId;
        CharacterProfile? character = matches
            ? await connection.QuerySingleOrDefaultAsync<CharacterProfile>(Cmd(CharacterProfileSql, new { request.CharacterId, request.AccountId, request.WorldId }, tx, cancellationToken))
            : null;
        if (character is null)
        {
            await tx.CommitAsync(cancellationToken);
            return Fail(RedeemOutcome.CharacterNotOwned);
        }

        await connection.ExecuteAsync(Cmd(ReapDeadSessionsSql, new { request.CharacterId }, tx, cancellationToken));
        if (await connection.ExecuteScalarAsync<int?>(Cmd(LiveSessionExistsSql, new { request.CharacterId }, tx, cancellationToken)) is not null)
        {
            await tx.CommitAsync(cancellationToken);
            return Fail(RedeemOutcome.ActiveSessionExists);
        }

        OwnershipRow? current = await connection.QuerySingleOrDefaultAsync<OwnershipRow>(Cmd(CurrentOwnershipSql, new { request.WorldId, request.CharacterId }, tx, cancellationToken));
        ZoneRow? zone = current is { ZoneAlive: true }
            ? new ZoneRow(current.ZoneId, current.ZoneInstanceId)
            : await connection.QuerySingleOrDefaultAsync<ZoneRow>(Cmd(PickZoneSql, new { request.WorldId }, tx, cancellationToken));
        if (zone is null)
        {
            await tx.CommitAsync(cancellationToken);
            return Fail(RedeemOutcome.NoZoneAvailable);
        }

        long epoch = await connection.ExecuteScalarAsync<long>(Cmd(UpsertOwnershipSql,
            new { request.WorldId, request.CharacterId, zone.ZoneId, zone.ZoneInstanceId }, tx, cancellationToken));

        try
        {
            await connection.ExecuteAsync(Cmd(InsertSessionSql, new
            {
                SessionId = request.GatewaySessionId.Bytes,
                GatewayInstanceId = request.GatewayInstanceId.Bytes,
                request.WorldId,
                request.CharacterId,
                request.AccountId,
                OwnerEpoch = epoch,
            }, tx, cancellationToken));
        }
        catch (PostgresException ex) when (ex.IsUniqueViolation(LiveSessionIndex))
        {
            // Lost a race with another gateway between the existence check and the insert.
            return Fail(RedeemOutcome.ActiveSessionExists);
        }

        await tx.CommitAsync(cancellationToken);
        return new RedeemResult(RedeemOutcome.Granted,
            new ZoneAssignment(request.WorldId, zone.ZoneId, new InstanceId(zone.ZoneInstanceId), epoch), character);
    }

    private static RedeemResult Fail(RedeemOutcome outcome) => new(outcome, null, null);

    private static CommandDefinition Cmd(string sql, object parameters, NpgsqlTransaction tx, CancellationToken ct)
        => new(sql, parameters, tx, cancellationToken: ct);

    private sealed record TicketRow(AccountId AccountId, CharacterId CharacterId, WorldId WorldId);

    private sealed record TicketState(bool Redeemed, bool Expired);

    private sealed record OwnershipRow(int ZoneId, byte[] ZoneInstanceId, long OwnerEpoch, bool ZoneAlive);

    private sealed record ZoneRow(int ZoneId, byte[] ZoneInstanceId);
}

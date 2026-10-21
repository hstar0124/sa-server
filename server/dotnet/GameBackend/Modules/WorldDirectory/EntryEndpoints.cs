using System.Security.Claims;
using Microsoft.AspNetCore.Http.HttpResults;
using Microsoft.Extensions.Options;
using Sa.GameBackend.Common;
using Sa.GameBackend.Modules.Auth;
using Sa.GameBackend.Modules.Characters;

namespace Sa.GameBackend.Modules.WorldDirectory;

internal sealed record GatewayEndpoint(string Host, int Port, int WebSocketPort);

internal sealed record EnterWorldResponse(string EntryTicket, GatewayEndpoint Gateway, DateTimeOffset ExpiresAt);

/// <summary>see README "네트워크와 입장": the client selects a character and receives a short-lived ticket.</summary>
internal static class EntryEndpoints
{
    public static IEndpointRouteBuilder MapEntryEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapPost("/api/v1/worlds/{worldId:long}/characters/{characterId:long}/enter", EnterAsync).RequireAuthorization();
        return app;
    }

    private static async Task<Results<Ok<EnterWorldResponse>, ProblemHttpResult>> EnterAsync(
        WorldId worldId,
        CharacterId characterId,
        ClaimsPrincipal principal,
        CharacterRepository characters,
        EntryTicketIssuer tickets,
        IOptions<WorldDirectoryOptions> options,
        CancellationToken cancellationToken)
    {
        AccountId accountId = CurrentAccount.Require(principal);
        if (!await characters.IsOwnedByAsync(characterId, accountId, worldId, cancellationToken))
        {
            return Problems.NotFound("unknown-character", "no such character on this account");
        }

        IssuedTicket issued = await tickets.IssueAsync(accountId, characterId, worldId, cancellationToken);
        WorldDirectoryOptions o = options.Value;
        return TypedResults.Ok(new EnterWorldResponse(
            Convert.ToBase64String(issued.SignedTicket), new GatewayEndpoint(o.GatewayHost, o.GatewayPort, o.GatewayWebSocketPort), issued.ExpiresAt));
    }
}

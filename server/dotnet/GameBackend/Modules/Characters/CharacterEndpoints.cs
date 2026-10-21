using System.ComponentModel.DataAnnotations;
using System.Security.Claims;
using Microsoft.AspNetCore.Http.HttpResults;
using Microsoft.Extensions.Options;
using Sa.GameBackend.Common;
using Sa.GameBackend.Infrastructure.GameData;
using Sa.GameBackend.Modules.Auth;

namespace Sa.GameBackend.Modules.Characters;

internal sealed record CreateCharacterRequest(
    [property: Required, RegularExpression(CharacterRules.NamePattern, ErrorMessage = CharacterRules.NameRuleText)] string Name,
    [property: Range(1, int.MaxValue)] int ClassId);

internal sealed record CharacterResponse(long CharacterId, long WorldId, string Name, int ClassId)
{
    public static CharacterResponse From(Character character)
        => new(character.Id.Value, character.WorldId.Value, character.Name, character.ClassId);
}

internal static class CharacterEndpoints
{
    public static IEndpointRouteBuilder MapCharacterEndpoints(this IEndpointRouteBuilder app)
    {
        RouteGroupBuilder group = app.MapGroup("/api/v1/worlds/{worldId:long}/characters").RequireAuthorization();
        group.MapGet("/", ListAsync);
        group.MapPost("/", CreateAsync);
        return app;
    }

    private static async Task<Ok<IReadOnlyList<CharacterResponse>>> ListAsync(
        long worldId, ClaimsPrincipal principal, CharacterRepository characters, CancellationToken cancellationToken)
    {
        IReadOnlyList<Character> rows = await characters.ListAsync(CurrentAccount.Require(principal), new WorldId(worldId), cancellationToken);
        return TypedResults.Ok<IReadOnlyList<CharacterResponse>>(rows.Select(CharacterResponse.From).ToList());
    }

    private static async Task<Results<Created<CharacterResponse>, ValidationProblem, ProblemHttpResult>> CreateAsync(
        long worldId,
        CreateCharacterRequest request,
        ClaimsPrincipal principal,
        CharacterRepository characters,
        IOptions<CharactersOptions> options,
        ContentCatalog content,
        CancellationToken cancellationToken)
    {
        if (RequestValidation.Validate(request) is { } invalid)
        {
            return invalid;
        }

        // A class the zones do not know would make a character no zone can spawn.
        if (!content.KnowsClass(request.ClassId))
        {
            return Problems.UnprocessableEntity("unknown-class", $"class {request.ClassId} does not exist");
        }

        CreateCharacterResult result = await characters.CreateAsync(
            CurrentAccount.Require(principal), new WorldId(worldId), request.Name, request.ClassId,
            options.Value.MaxCharactersPerWorld, cancellationToken);

        return result.Outcome switch
        {
            CreateCharacterOutcome.Created => TypedResults.Created((string?)null, CharacterResponse.From(result.Character!)),
            CreateCharacterOutcome.NameTaken => Problems.Conflict("character-name-taken", "a character with that name exists in this world"),
            CreateCharacterOutcome.LimitReached => Problems.UnprocessableEntity("character-limit", $"at most {options.Value.MaxCharactersPerWorld} characters per world"),
            CreateCharacterOutcome.UnknownWorld => Problems.NotFound("unknown-world", $"world {worldId} does not exist"),
            _ => throw new InvalidOperationException($"unhandled outcome {result.Outcome}"),
        };
    }
}

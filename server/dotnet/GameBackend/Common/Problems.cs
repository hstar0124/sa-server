using Microsoft.AspNetCore.Http.HttpResults;

namespace Sa.GameBackend.Common;

/// <summary>Problem-details responses with a stable machine-readable <c>type</c> per domain outcome.</summary>
internal static class Problems
{
    private const string TypePrefix = "urn:sa:problem:";

    public static ProblemHttpResult Conflict(string code, string detail)
        => TypedResults.Problem(statusCode: StatusCodes.Status409Conflict, type: TypePrefix + code, title: code, detail: detail);

    public static ProblemHttpResult Unauthorized(string code, string detail)
        => TypedResults.Problem(statusCode: StatusCodes.Status401Unauthorized, type: TypePrefix + code, title: code, detail: detail);

    public static ProblemHttpResult NotFound(string code, string detail)
        => TypedResults.Problem(statusCode: StatusCodes.Status404NotFound, type: TypePrefix + code, title: code, detail: detail);

    public static ProblemHttpResult UnprocessableEntity(string code, string detail)
        => TypedResults.Problem(statusCode: StatusCodes.Status422UnprocessableEntity, type: TypePrefix + code, title: code, detail: detail);
}

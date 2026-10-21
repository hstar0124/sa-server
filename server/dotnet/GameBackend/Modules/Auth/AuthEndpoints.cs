using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Http.HttpResults;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Auth;

internal sealed record RegisterRequest(
    [property: Required, RegularExpression(AccountRules.UsernamePattern, ErrorMessage = AccountRules.UsernameRuleText)] string Username,
    [property: Required, MaxLength(AccountRules.PasswordMaxLength)] string Password);

internal sealed record RegisterResponse(long AccountId);

internal sealed record LoginRequest(
    [property: Required, MaxLength(AccountRules.UsernameMaxLength)] string Username,
    [property: Required, MaxLength(AccountRules.PasswordMaxLength)] string Password);

internal sealed record LoginResponse(string AccessToken, DateTimeOffset ExpiresAt);

internal static class AuthEndpoints
{
    public const string RateLimitPolicy = "auth";

    public static IEndpointRouteBuilder MapAuthEndpoints(this IEndpointRouteBuilder app)
    {
        RouteGroupBuilder group = app.MapGroup("/api/v1/auth").RequireRateLimiting(RateLimitPolicy).AllowAnonymous();
        group.MapPost("/register", RegisterAsync);
        group.MapPost("/login", LoginAsync);
        return app;
    }

    private static async Task<Results<Created<RegisterResponse>, ValidationProblem, ProblemHttpResult>> RegisterAsync(
        RegisterRequest request, AuthService auth, CancellationToken cancellationToken)
    {
        if (RequestValidation.Validate(request) is { } invalid)
        {
            return invalid;
        }

        if (request.Password.Length < auth.MinPasswordLength)
        {
            return TypedResults.ValidationProblem(new Dictionary<string, string[]>(StringComparer.Ordinal)
            {
                [nameof(RegisterRequest.Password)] = [$"at least {auth.MinPasswordLength} characters"],
            });
        }

        RegisterResult result = await auth.RegisterAsync(request.Username, request.Password, cancellationToken);
        return result.Outcome switch
        {
            RegisterOutcome.Created => TypedResults.Created((string?)null, new RegisterResponse(result.AccountId!.Value.Value)),
            RegisterOutcome.UsernameTaken => Problems.Conflict("username-taken", "that username is already registered"),
            _ => throw new InvalidOperationException($"unhandled outcome {result.Outcome}"),
        };
    }

    private static async Task<Results<Ok<LoginResponse>, ValidationProblem, ProblemHttpResult>> LoginAsync(
        LoginRequest request, AuthService auth, CancellationToken cancellationToken)
    {
        if (RequestValidation.Validate(request) is { } invalid)
        {
            return invalid;
        }

        LoginResult result = await auth.LoginAsync(request.Username, request.Password, cancellationToken);
        return result.Outcome switch
        {
            LoginOutcome.Success => TypedResults.Ok(new LoginResponse(result.Token!.Value, result.Token.ExpiresAt)),
            LoginOutcome.InvalidCredentials => Problems.Unauthorized("invalid-credentials", "username or password is incorrect"),
            _ => throw new InvalidOperationException($"unhandled outcome {result.Outcome}"),
        };
    }
}

using Microsoft.AspNetCore.Identity;
using Microsoft.Extensions.Options;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Auth;

internal enum RegisterOutcome
{
    Created,
    UsernameTaken,
}

internal sealed record RegisterResult(RegisterOutcome Outcome, AccountId? AccountId);

internal enum LoginOutcome
{
    Success,
    InvalidCredentials,
}

internal sealed record LoginResult(LoginOutcome Outcome, AccessToken? Token);

/// <summary>Account use-cases. Password handling is delegated to ASP.NET Core Identity's hasher.</summary>
internal sealed partial class AuthService(
    AccountRepository accounts,
    IPasswordHasher<Account> hasher,
    TokenIssuer tokens,
    IOptions<AuthOptions> options,
    ILogger<AuthService> logger)
{
    // Hashing needs an instance for the generic hasher API; the value is never read.
    private static readonly Account HashSubject = new(default, string.Empty, string.Empty);

    // Verified against when the username is unknown, so that path costs one verification like a real
    // account does. Made on first use with the configured hasher; a race only computes it twice.
    private static string? _dummyHash;

    public int MinPasswordLength => options.Value.MinPasswordLength;

    public async Task<RegisterResult> RegisterAsync(string username, string password, CancellationToken cancellationToken)
    {
        string hash = hasher.HashPassword(HashSubject, password);
        AccountId? id = await accounts.TryInsertAsync(username, hash, cancellationToken);
        if (id is null)
        {
            return new RegisterResult(RegisterOutcome.UsernameTaken, null);
        }

        LogAccountCreated(id.Value);
        return new RegisterResult(RegisterOutcome.Created, id);
    }

    public async Task<LoginResult> LoginAsync(string username, string password, CancellationToken cancellationToken)
    {
        Account? account = await accounts.FindByUsernameAsync(username, cancellationToken);
        if (account is null)
        {
            // Same outcome and the same work for unknown users: one verification, as for a real account.
            string dummy = _dummyHash ??= hasher.HashPassword(HashSubject, Guid.NewGuid().ToString());
            hasher.VerifyHashedPassword(HashSubject, dummy, password);
            return new LoginResult(LoginOutcome.InvalidCredentials, null);
        }

        PasswordVerificationResult verification = hasher.VerifyHashedPassword(account, account.PasswordHash, password);
        if (verification == PasswordVerificationResult.Failed)
        {
            LogLoginRejected(account.Id);
            return new LoginResult(LoginOutcome.InvalidCredentials, null);
        }

        if (verification == PasswordVerificationResult.SuccessRehashNeeded)
        {
            await accounts.UpdatePasswordHashAsync(account.Id, hasher.HashPassword(account, password), cancellationToken);
        }

        return new LoginResult(LoginOutcome.Success, tokens.Issue(account.Id));
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "account {AccountId} created")]
    private partial void LogAccountCreated(AccountId accountId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "login rejected for account {AccountId}")]
    private partial void LogLoginRejected(AccountId accountId);
}

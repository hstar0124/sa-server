using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Auth;

internal sealed record Account(AccountId Id, string Username, string PasswordHash);

/// <summary>Username rules. The regular expression is mirrored by the accounts_username_format CHECK constraint.</summary>
internal static class AccountRules
{
    public const string UsernamePattern = "^[a-z0-9_]{3,24}$";
    public const string UsernameRuleText = "3-24 characters: lowercase letters, digits, underscore";
}

using System.Globalization;
using System.Security.Claims;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Auth;

/// <summary>Reads the authenticated account from the validated bearer token. Other modules depend on this, not on claims.</summary>
internal static class CurrentAccount
{
    public static AccountId Require(ClaimsPrincipal principal)
    {
        string? sub = principal.FindFirstValue(ClaimTypes.NameIdentifier) ?? principal.FindFirstValue("sub");
        if (sub is null || !long.TryParse(sub, NumberStyles.None, CultureInfo.InvariantCulture, out long id))
        {
            throw new InvalidOperationException("authenticated principal has no account subject");
        }

        return new AccountId(id);
    }
}

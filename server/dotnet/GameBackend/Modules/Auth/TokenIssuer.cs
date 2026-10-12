using System.IdentityModel.Tokens.Jwt;
using System.Security.Claims;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Tokens;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Modules.Auth;

internal sealed record AccessToken(string Value, DateTimeOffset ExpiresAt);

/// <summary>Issues HMAC-signed JWTs carrying the account id as <c>sub</c>.</summary>
internal sealed class TokenIssuer(IOptions<AuthOptions> options, TimeProvider clock)
{
    private readonly AuthOptions _options = options.Value;
    private readonly SigningCredentials _credentials = new(
        new SymmetricSecurityKey(options.Value.DecodedSigningKey()),
        SecurityAlgorithms.HmacSha256);

    public AccessToken Issue(AccountId accountId)
    {
        DateTimeOffset now = clock.GetUtcNow();
        DateTimeOffset expires = now + _options.AccessTokenLifetime;
        var token = new JwtSecurityToken(
            issuer: _options.Issuer,
            audience: _options.Audience,
            claims: [new Claim(JwtRegisteredClaimNames.Sub, accountId.ToString())],
            notBefore: now.UtcDateTime,
            expires: expires.UtcDateTime,
            signingCredentials: _credentials);
        return new AccessToken(new JwtSecurityTokenHandler().WriteToken(token), expires);
    }
}

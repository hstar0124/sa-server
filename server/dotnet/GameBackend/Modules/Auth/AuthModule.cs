using System.Threading.RateLimiting;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.AspNetCore.Identity;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Tokens;

namespace Sa.GameBackend.Modules.Auth;

internal static class AuthModule
{
    // The option counts attempts per minute, so the window is a minute by definition.
    private static readonly TimeSpan CredentialAttemptWindow = TimeSpan.FromMinutes(1);

    // Over the limit is refused at once, not held until the window turns.
    private const int NoQueue = 0;

    public static IServiceCollection AddAuthModule(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<AuthOptions>()
            .Bind(configuration.GetSection(AuthOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddSingleton<IValidateOptions<AuthOptions>, AuthOptionsValidator>();

        services.AddSingleton<IPasswordHasher<Account>, PasswordHasher<Account>>();
        services.AddSingleton<TokenIssuer>();
        services.AddSingleton<AccountRepository>();
        services.AddSingleton<AuthService>();

        services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme)
            .AddJwtBearer();
        services.AddOptions<JwtBearerOptions>(JwtBearerDefaults.AuthenticationScheme)
            .Configure<IOptions<AuthOptions>>((jwt, auth) =>
            {
                jwt.TokenValidationParameters = new TokenValidationParameters
                {
                    ValidIssuer = auth.Value.Issuer,
                    ValidAudience = auth.Value.Audience,
                    IssuerSigningKey = new SymmetricSecurityKey(auth.Value.DecodedSigningKey()),
                    ValidateIssuerSigningKey = true,
                    ClockSkew = auth.Value.TokenClockSkew,
                };
            });
        services.AddAuthorization();

        // Credential endpoints are brute-force targets.
        services.AddRateLimiter(limiter =>
        {
            limiter.RejectionStatusCode = StatusCodes.Status429TooManyRequests;
            limiter.AddPolicy(AuthEndpoints.RateLimitPolicy, context =>
            {
                int permits = context.RequestServices.GetRequiredService<IOptions<AuthOptions>>().Value.CredentialAttemptsPerMinutePerAddress;
                return RateLimitPartition.GetFixedWindowLimiter(
                    context.Connection.RemoteIpAddress?.ToString() ?? "unknown",
                    _ => new FixedWindowRateLimiterOptions
                    {
                        PermitLimit = permits,
                        Window = CredentialAttemptWindow,
                        QueueLimit = NoQueue,
                    });
            });
        });

        return services;
    }
}

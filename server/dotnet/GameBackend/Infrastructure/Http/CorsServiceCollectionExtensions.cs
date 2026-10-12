namespace Sa.GameBackend.Infrastructure.Http;

internal static class CorsServiceCollectionExtensions
{
    public static IServiceCollection AddViewerCors(this IServiceCollection services, IConfiguration configuration)
    {
        string[] origins = configuration.GetSection(CorsOptions.SectionName).Get<CorsOptions>()?.AllowedOrigins.ToArray() ?? [];
        services.AddCors(cors => cors.AddPolicy(CorsOptions.PolicyName, policy =>
        {
            if (origins.Length == 0)
            {
                return;  // an empty policy allows nothing
            }

            policy.WithOrigins(origins).AllowAnyHeader().WithMethods("GET", "POST");
        }));
        return services;
    }
}

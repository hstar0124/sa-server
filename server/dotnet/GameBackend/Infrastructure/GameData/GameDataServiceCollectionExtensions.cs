using Microsoft.Extensions.Options;

namespace Sa.GameBackend.Infrastructure.GameData;

internal static class GameDataServiceCollectionExtensions
{
    public static IServiceCollection AddGameData(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<GameDataOptions>()
            .Bind(configuration.GetSection(GameDataOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddSingleton(provider => ContentCatalog.Load(provider.GetRequiredService<IOptions<GameDataOptions>>().Value.ContentDir));
        return services;
    }

    /// <summary>Loads the catalog now, so a bad content directory stops start-up rather than the first request.</summary>
    public static WebApplication UseGameData(this WebApplication app)
    {
        _ = app.Services.GetRequiredService<ContentCatalog>();
        return app;
    }
}

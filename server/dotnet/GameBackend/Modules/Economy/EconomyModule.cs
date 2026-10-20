using Microsoft.Extensions.Options;

namespace Sa.GameBackend.Modules.Economy;

/// <summary>Composition root of the module: content catalog, repository and the gRPC surface for zones.</summary>
internal static class EconomyModule
{
    public static IServiceCollection AddEconomyModule(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<EconomyOptions>()
            .Bind(configuration.GetSection(EconomyOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();

        services.AddSingleton(provider => ContentCatalog.Load(provider.GetRequiredService<IOptions<EconomyOptions>>().Value.ContentDir));
        services.AddSingleton<EconomyRepository>();
        return services;
    }

    /// <summary>Maps the service and loads the catalog now, so a bad content directory stops start-up rather than the first kill.</summary>
    public static WebApplication UseEconomyModule(this WebApplication app)
    {
        _ = app.Services.GetRequiredService<ContentCatalog>();
        app.MapGrpcService<EconomyGrpcService>();
        return app;
    }
}

namespace Sa.GameBackend.Modules.Economy;

/// <summary>Composition root of the module: the repository and the gRPC surface for zones.</summary>
internal static class EconomyModule
{
    public static IServiceCollection AddEconomyModule(this IServiceCollection services)
    {
        services.AddSingleton<EconomyRepository>();
        return services;
    }

    public static WebApplication UseEconomyModule(this WebApplication app)
    {
        app.MapGrpcService<EconomyGrpcService>();
        return app;
    }
}

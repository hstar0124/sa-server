namespace Sa.GameBackend.Modules.Trading;

/// <summary>Composition root of the module: the repository and the gRPC surface for zones.</summary>
internal static class TradingModule
{
    public static IServiceCollection AddTradingModule(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<TradingOptions>()
            .Bind(configuration.GetSection(TradingOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddSingleton<TradingRepository>();
        return services;
    }

    public static WebApplication UseTradingModule(this WebApplication app)
    {
        app.MapGrpcService<TradingGrpcService>();
        return app;
    }
}

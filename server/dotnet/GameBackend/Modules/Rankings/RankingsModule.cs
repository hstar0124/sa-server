namespace Sa.GameBackend.Modules.Rankings;

internal static class RankingsModule
{
    public static IServiceCollection AddRankingsModule(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<RankingsOptions>()
            .Bind(configuration.GetSection(RankingsOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddSingleton<RankingRepository>();
        return services;
    }
}

using Microsoft.Extensions.Options;
using StackExchange.Redis;

namespace Sa.GameBackend.Modules.Rankings;

internal static class RankingsModule
{
    public static IServiceCollection AddRankingsModule(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<RankingsOptions>()
            .Bind(configuration.GetSection(RankingsOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddOptions<RedisOptions>()
            .Bind(configuration.GetSection(RedisOptions.SectionName))
            .Validate<IOptions<RankingsOptions>>(
                (redis, rankings) => rankings.Value.Backend != RankingBackend.Redis || !string.IsNullOrWhiteSpace(redis.ConnectionString),
                "Redis:ConnectionString is required when Rankings:Backend is Redis")
            .ValidateOnStart();

        services.AddSingleton<RankingRepository>();
        services.AddSingleton<IRankingSource>(provider =>
        {
            RankingsOptions options = provider.GetRequiredService<IOptions<RankingsOptions>>().Value;
            return options.Backend switch
            {
                RankingBackend.Postgres => provider.GetRequiredService<RankingRepository>(),
                RankingBackend.Redis => new RedisRankingSource(
                    ConnectionMultiplexer.Connect(provider.GetRequiredService<IOptions<RedisOptions>>().Value.ConnectionString),
                    provider.GetRequiredService<RankingRepository>(),
                    provider.GetRequiredService<ILogger<RedisRankingSource>>()),
                _ => throw new InvalidOperationException($"unknown ranking backend {options.Backend}"),
            };
        });
        return services;
    }
}

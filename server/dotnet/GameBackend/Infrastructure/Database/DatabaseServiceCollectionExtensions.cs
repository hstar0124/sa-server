using Microsoft.Extensions.Options;
using Npgsql;
using Sa.GameBackend.Common;

namespace Sa.GameBackend.Infrastructure.Database;

internal static class DatabaseServiceCollectionExtensions
{
    /// <summary>Registers a single <see cref="NpgsqlDataSource"/>; every module borrows connections from it.</summary>
    public static IServiceCollection AddDatabase(this IServiceCollection services, IConfiguration configuration)
    {
        IdTypeHandlers.Register();
        services.AddOptions<DatabaseOptions>()
            .Bind(configuration.GetSection(DatabaseOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();

        services.AddSingleton(provider =>
        {
            DatabaseOptions options = provider.GetRequiredService<IOptions<DatabaseOptions>>().Value;
            var builder = new NpgsqlDataSourceBuilder(options.ConnectionString)
            {
                ConnectionStringBuilder = { MaxPoolSize = options.MaxPoolSize },
            };
            builder.UseLoggerFactory(provider.GetRequiredService<ILoggerFactory>());
            return builder.Build();
        });

        return services;
    }
}

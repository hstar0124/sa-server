using Microsoft.Extensions.Options;

namespace Sa.GameBackend.Modules.WorldDirectory;

/// <summary>Composition root of the module. Other modules depend on this registration, never on internals.</summary>
internal static class WorldDirectoryModule
{
    public static IServiceCollection AddWorldDirectoryModule(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<WorldDirectoryOptions>()
            .Bind(configuration.GetSection(WorldDirectoryOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddSingleton<IValidateOptions<WorldDirectoryOptions>, WorldDirectoryOptionsValidator>();

        services.AddSingleton<WorldDirectoryRepository>();
        services.AddSingleton<EntryTicketIssuer>();
        return services;
    }
}

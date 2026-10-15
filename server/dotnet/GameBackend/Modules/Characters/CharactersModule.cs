namespace Sa.GameBackend.Modules.Characters;

internal static class CharactersModule
{
    public static IServiceCollection AddCharactersModule(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<CharactersOptions>()
            .Bind(configuration.GetSection(CharactersOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddSingleton<CharacterRepository>();
        return services;
    }
}

using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;

namespace Sa.Migrator;

internal sealed record MigratorOptions(string ConnectionString, string ScriptsDirectory)
{
    private const string ConnectionKey = "connection";
    private const string ScriptsKey = "scripts";

    public static MigratorOptions? From(IConfiguration configuration, ILogger logger)
    {
        string? connection = configuration[ConnectionKey];
        string? scripts = configuration[ScriptsKey];

        if (string.IsNullOrWhiteSpace(connection) || string.IsNullOrWhiteSpace(scripts))
        {
            logger.LogUsage();
            return null;
        }

        if (!Directory.Exists(scripts))
        {
            logger.LogScriptsDirectoryMissing(scripts);
            return null;
        }

        return new MigratorOptions(connection, Path.GetFullPath(scripts));
    }
}

internal static partial class MigratorLog
{
    [LoggerMessage(Level = LogLevel.Error,
        Message = "usage: Migrator --connection <npgsql connection string> --scripts <migrations directory>")]
    public static partial void LogUsage(this ILogger logger);

    [LoggerMessage(Level = LogLevel.Error, Message = "scripts directory does not exist: {Directory}")]
    public static partial void LogScriptsDirectoryMissing(this ILogger logger, string directory);

    [LoggerMessage(Level = LogLevel.Error, Message = "migration failed in script {Script}")]
    public static partial void LogMigrationFailed(this ILogger logger, string? script, Exception? exception);

    [LoggerMessage(Level = LogLevel.Information, Message = "database up to date, {Applied} script(s) applied")]
    public static partial void LogMigrationSucceeded(this ILogger logger, int applied);
}

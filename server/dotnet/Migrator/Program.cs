using DbUp;
using DbUp.Engine;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using Sa.Migrator;

// Usage: Migrator --connection "<npgsql connection string>" --scripts <dir>
// or     MIGRATOR_Connection=... MIGRATOR_Scripts=... Migrator
IConfigurationRoot configuration = new ConfigurationBuilder()
    .AddEnvironmentVariables(prefix: "MIGRATOR_")
    .AddCommandLine(args)
    .Build();

using ILoggerFactory loggerFactory = LoggerFactory.Create(logging => logging.AddSimpleConsole(o => o.SingleLine = true));
ILogger logger = loggerFactory.CreateLogger("Migrator");

MigratorOptions? options = MigratorOptions.From(configuration, logger);
if (options is null)
{
    return ExitCodes.BadArguments;
}

UpgradeEngine upgrader = DeployChanges.To
    .PostgresqlDatabase(options.ConnectionString)
    .WithScriptsFromFileSystem(options.ScriptsDirectory)
    .WithTransactionPerScript()
    .JournalToPostgresqlTable("public", "schema_migrations")
    .LogToConsole()
    .Build();

DatabaseUpgradeResult result = upgrader.PerformUpgrade();
if (!result.Successful)
{
    logger.LogMigrationFailed(result.ErrorScript?.Name, result.Error);
    return ExitCodes.MigrationFailed;
}

int applied = result.Scripts.Count();
logger.LogMigrationSucceeded(applied);
return ExitCodes.Success;

internal static class ExitCodes
{
    public const int Success = 0;
    public const int BadArguments = 2;
    public const int MigrationFailed = 1;
}

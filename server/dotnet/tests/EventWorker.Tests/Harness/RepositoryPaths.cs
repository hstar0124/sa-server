namespace Sa.EventWorker.Tests.Harness;

/// <summary>Locates repository folders from the test binary, so tests apply the same SQL the Migrator ships.</summary>
internal static class RepositoryPaths
{
    public static string Migrations { get; } = Path.Combine(FindRoot(), "database", "migrations");

    private static string FindRoot()
    {
        DirectoryInfo? dir = new(AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (Directory.Exists(Path.Combine(dir.FullName, "database", "migrations")) && File.Exists(Path.Combine(dir.FullName, "CLAUDE.md")))
            {
                return dir.FullName;
            }

            dir = dir.Parent;
        }

        throw new InvalidOperationException("repository root not found above " + AppContext.BaseDirectory);
    }
}

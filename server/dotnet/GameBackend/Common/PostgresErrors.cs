using Npgsql;

namespace Sa.GameBackend.Common;

/// <summary>
/// Maps PostgreSQL constraint violations back to domain outcomes. Constraints are the last line of
/// defence (CLAUDE.md rule 7), so modules must recognise them rather than treat them as crashes.
/// </summary>
internal static class PostgresErrors
{
    public static bool IsUniqueViolation(this PostgresException exception, string constraintName)
        => exception.SqlState == PostgresErrorCodes.UniqueViolation
           && string.Equals(exception.ConstraintName, constraintName, StringComparison.Ordinal);

    public static bool IsCheckViolation(this PostgresException exception, string constraintName)
        => exception.SqlState == PostgresErrorCodes.CheckViolation
           && string.Equals(exception.ConstraintName, constraintName, StringComparison.Ordinal);

    public static bool IsForeignKeyViolation(this PostgresException exception, string constraintName)
        => exception.SqlState == PostgresErrorCodes.ForeignKeyViolation
           && string.Equals(exception.ConstraintName, constraintName, StringComparison.Ordinal);
}

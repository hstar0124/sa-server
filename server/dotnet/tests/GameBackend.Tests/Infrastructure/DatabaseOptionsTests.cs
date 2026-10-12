using System.ComponentModel.DataAnnotations;
using Sa.GameBackend.Infrastructure.Database;

namespace Sa.GameBackend.Tests.Infrastructure;

public sealed class DatabaseOptionsTests
{
    [Fact]
    public void Rejects_empty_connection_string()
    {
        var options = new DatabaseOptions { ConnectionString = string.Empty };

        Assert.False(TryValidate(options, out ICollection<ValidationResult> results));
        Assert.Contains(results, r => r.MemberNames.Contains(nameof(DatabaseOptions.ConnectionString)));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(201)]
    public void Rejects_pool_size_outside_bounds(int poolSize)
    {
        var options = new DatabaseOptions { ConnectionString = "Host=x", MaxPoolSize = poolSize };

        Assert.False(TryValidate(options, out _));
    }

    [Fact]
    public void Accepts_well_formed_options()
    {
        var options = new DatabaseOptions { ConnectionString = "Host=x", MaxPoolSize = 10 };

        Assert.True(TryValidate(options, out _));
    }

    private static bool TryValidate(object instance, out ICollection<ValidationResult> results)
    {
        results = [];
        return Validator.TryValidateObject(instance, new ValidationContext(instance), results, validateAllProperties: true);
    }
}

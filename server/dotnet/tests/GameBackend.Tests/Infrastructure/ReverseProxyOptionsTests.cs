using Microsoft.Extensions.Options;
using Sa.GameBackend.Infrastructure.Http;

namespace Sa.GameBackend.Tests.Infrastructure;

public sealed class ReverseProxyOptionsTests
{
    private static readonly ReverseProxyOptionsValidator Validator = new();

    [Fact]
    public void No_trusted_network_means_disabled_and_valid()
    {
        var options = new ReverseProxyOptions();

        Assert.False(options.Enabled);
        Assert.True(Validator.Validate(null, options).Succeeded);
    }

    [Theory]
    [InlineData("10.42.0.0/16")]
    [InlineData("fd00::/8")]
    public void Accepts_networks_in_cidr_notation(string network)
    {
        var options = new ReverseProxyOptions { TrustedNetworks = [network] };

        Assert.True(options.Enabled);
        Assert.True(Validator.Validate(null, options).Succeeded);
    }

    [Theory]
    [InlineData("10.42.0.1")]      // an address, not a network
    [InlineData("cloudflared")]
    [InlineData("10.42.0.0/33")]
    public void Rejects_anything_that_is_not_a_network(string network)
    {
        var options = new ReverseProxyOptions { TrustedNetworks = ["10.0.0.0/8", network] };

        ValidateOptionsResult result = Validator.Validate(null, options);

        Assert.True(result.Failed);
        Assert.Contains(network, result.FailureMessage, StringComparison.Ordinal);
    }
}

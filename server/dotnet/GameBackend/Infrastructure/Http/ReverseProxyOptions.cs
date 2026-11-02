using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.Extensions.Options;
using IPNetwork = System.Net.IPNetwork;

namespace Sa.GameBackend.Infrastructure.Http;

/// <summary>
/// Configuration section "ReverseProxy". Behind a reverse proxy every connection arrives from the
/// proxy's address, so anything counted per client address (the credential attempt limit) would
/// count all clients as one. Connections from a trusted network carry the real client address in
/// a header the proxy sets; that header replaces the connection's address. Empty means no proxy:
/// the connection's address is the client's, which is what local runs want.
/// </summary>
internal sealed class ReverseProxyOptions
{
    public const string SectionName = "ReverseProxy";

    /// <summary>Networks in CIDR notation whose connections carry a trustworthy header, e.g. the cluster's pod range.</summary>
    public IReadOnlyList<string> TrustedNetworks { get; init; } = [];

    /// <summary>Header the proxy sets to the client's address. Only the last address in it is used.</summary>
    [Required(AllowEmptyStrings = false)]
    public string ClientAddressHeader { get; init; } = "X-Forwarded-For";

    public bool Enabled => TrustedNetworks.Count > 0;
}

internal sealed class ReverseProxyOptionsValidator : IValidateOptions<ReverseProxyOptions>
{
    public ValidateOptionsResult Validate(string? name, ReverseProxyOptions options)
    {
        foreach (string network in options.TrustedNetworks)
        {
            if (!IPNetwork.TryParse(network, out _))
            {
                return ValidateOptionsResult.Fail($"ReverseProxy:TrustedNetworks entry '{network}' is not a network in CIDR notation");
            }
        }

        return ValidateOptionsResult.Success;
    }
}

internal static class ReverseProxyServiceCollectionExtensions
{
    // Only the proxy's own entry counts; anything a client wrote into the header before it is not trusted.
    private const int OneProxyHop = 1;

    public static IServiceCollection AddReverseProxyClientAddress(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<ReverseProxyOptions>()
            .Bind(configuration.GetSection(ReverseProxyOptions.SectionName))
            .ValidateDataAnnotations()
            .ValidateOnStart();
        services.AddSingleton<IValidateOptions<ReverseProxyOptions>, ReverseProxyOptionsValidator>();

        services.AddOptions<ForwardedHeadersOptions>()
            .Configure<IOptions<ReverseProxyOptions>>((forwarded, proxy) =>
            {
                forwarded.ForwardedHeaders = ForwardedHeaders.XForwardedFor;
                forwarded.ForwardedForHeaderName = proxy.Value.ClientAddressHeader;
                forwarded.ForwardLimit = OneProxyHop;
                // The defaults trust loopback; only the configured networks are trusted here.
                forwarded.KnownProxies.Clear();
                forwarded.KnownIPNetworks.Clear();
                foreach (string network in proxy.Value.TrustedNetworks)
                {
                    forwarded.KnownIPNetworks.Add(IPNetwork.Parse(network));
                }
            });
        return services;
    }

    /// <summary>Runs before anything that reads the connection's address (the rate limiter).</summary>
    public static WebApplication UseReverseProxyClientAddress(this WebApplication app)
    {
        if (app.Services.GetRequiredService<IOptions<ReverseProxyOptions>>().Value.Enabled)
        {
            app.UseForwardedHeaders();
        }

        return app;
    }
}

using System.Diagnostics;
using System.Globalization;
using System.Net.Http.Headers;
using Sa.HttpBench;

// Usage: httpbench --url <url> [--clients 16] [--duration-sec 10] [--bearer <token>] [--label <text>]
// Each client sends the next request as soon as the previous one completed. Output is one summary
// line per run so several runs paste into a table.
BenchOptions? options = BenchOptions.Parse(args);
if (options is null)
{
    await Console.Error.WriteLineAsync("usage: httpbench --url <url> [--clients N] [--duration-sec S] [--bearer <token>] [--label <text>]");
    return 2;
}

BenchResult result = await Bench.RunAsync(options);
await Console.Out.WriteLineAsync(result.ToLine(options));
return result.Errors == 0 ? 0 : 1;

namespace Sa.HttpBench
{
    internal sealed record BenchOptions(Uri Url, int Clients, TimeSpan Duration, string? Bearer, string Label)
    {
        private const int DefaultClients = 16;
        private const int DefaultDurationSeconds = 10;

        public static BenchOptions? Parse(string[] args)
        {
            string? url = null;
            string? bearer = null;
            string label = "";
            int clients = DefaultClients;
            int duration = DefaultDurationSeconds;
            for (int i = 0; i + 1 < args.Length; i += 2)
            {
                switch (args[i])
                {
                    case "--url": url = args[i + 1]; break;
                    case "--bearer": bearer = args[i + 1]; break;
                    case "--label": label = args[i + 1]; break;
                    case "--clients" when int.TryParse(args[i + 1], NumberStyles.None, CultureInfo.InvariantCulture, out int c): clients = c; break;
                    case "--duration-sec" when int.TryParse(args[i + 1], NumberStyles.None, CultureInfo.InvariantCulture, out int d): duration = d; break;
                    default: return null;  // an unknown flag or a value that is not a number: print the usage
                }
            }

            return args.Length % 2 != 0 || url is null || !Uri.TryCreate(url, UriKind.Absolute, out Uri? uri) || clients < 1 || duration < 1
                ? null
                : new BenchOptions(uri, clients, TimeSpan.FromSeconds(duration), bearer, label);
        }
    }

    internal sealed record BenchResult(long Requests, long Errors, TimeSpan Elapsed, double P50Ms, double P95Ms, double P99Ms, double MaxMs)
    {
        public string ToLine(BenchOptions options)
            => string.Create(CultureInfo.InvariantCulture,
                $"{options.Label,-28} clients={options.Clients,3} req/s={Requests / Elapsed.TotalSeconds,9:F0} p50={P50Ms,7:F2}ms p95={P95Ms,7:F2}ms p99={P99Ms,7:F2}ms max={MaxMs,8:F1}ms errors={Errors}");
    }

    internal static class Bench
    {
        public static async Task<BenchResult> RunAsync(BenchOptions options)
        {
            using var handler = new SocketsHttpHandler { MaxConnectionsPerServer = options.Clients, PooledConnectionLifetime = TimeSpan.FromMinutes(5) };
            using var http = new HttpClient(handler);
            if (options.Bearer is not null)
            {
                http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", options.Bearer);
            }

            // Warm connections and JIT before the clock starts.
            using (await http.GetAsync(options.Url))
            {
            }

            using var stop = new CancellationTokenSource(options.Duration);
            var latencies = new List<double>[options.Clients];
            long errors = 0;
            Stopwatch clock = Stopwatch.StartNew();
            Task[] workers = Enumerable.Range(0, options.Clients).Select(async i =>
            {
                var mine = new List<double>(capacity: 4096);
                latencies[i] = mine;
                while (!stop.IsCancellationRequested)
                {
                    long started = Stopwatch.GetTimestamp();
                    try
                    {
                        using HttpResponseMessage response = await http.GetAsync(options.Url, HttpCompletionOption.ResponseContentRead, CancellationToken.None);
                        if (!response.IsSuccessStatusCode)
                        {
                            Interlocked.Increment(ref errors);
                        }
                    }
                    catch (HttpRequestException)
                    {
                        Interlocked.Increment(ref errors);
                    }

                    mine.Add(Stopwatch.GetElapsedTime(started).TotalMilliseconds);
                }
            }).ToArray();
            await Task.WhenAll(workers);
            clock.Stop();

            double[] all = latencies.SelectMany(l => l).Order().ToArray();
            return new BenchResult(all.Length, errors, clock.Elapsed, Percentile(all, 0.50), Percentile(all, 0.95), Percentile(all, 0.99), all.Length == 0 ? 0 : all[^1]);
        }

        private static double Percentile(double[] sorted, double q)
            => sorted.Length == 0 ? 0 : sorted[Math.Min(sorted.Length - 1, (int)Math.Ceiling(q * sorted.Length) - 1)];
    }
}

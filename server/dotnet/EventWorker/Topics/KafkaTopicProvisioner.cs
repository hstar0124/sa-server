using Confluent.Kafka;
using Confluent.Kafka.Admin;
using Microsoft.Extensions.Options;

namespace Sa.EventWorker.Topics;

/// <summary>
/// Creates the configured topics before the publisher starts. Auto-creation is off on the broker, so
/// a misspelled topic fails here, loudly, instead of silently forking traffic. Registered first:
/// hosted services start in order, and this one finishes before returning.
/// </summary>
internal sealed partial class KafkaTopicProvisioner(IOptions<KafkaOptions> options, ILogger<KafkaTopicProvisioner> logger) : IHostedService
{
    public async Task StartAsync(CancellationToken cancellationToken)
    {
        KafkaOptions kafka = options.Value;
        using IAdminClient admin = new AdminClientBuilder(new AdminClientConfig { BootstrapServers = kafka.BootstrapServers }).Build();
        await EnsureAsync(admin, kafka, cancellationToken);
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <summary>Creates every missing topic; existing ones are left as they are (partition counts are not reconciled).</summary>
    public async Task EnsureAsync(IAdminClient admin, KafkaOptions kafka, CancellationToken cancellationToken)
    {
        var specs = kafka.AllTopics.Select(t => new TopicSpecification
        {
            Name = t.Name,
            NumPartitions = t.Partitions,
            ReplicationFactor = kafka.ReplicationFactor,
        }).ToList();
        string names = string.Join(", ", specs.Select(s => s.Name));
        try
        {
            await admin.CreateTopicsAsync(specs);
            LogCreated(names);
        }
        catch (CreateTopicsException ex) when (ex.Results.All(r => r.Error.Code is ErrorCode.TopicAlreadyExists or ErrorCode.NoError))
        {
            LogPresent(names);
        }

        cancellationToken.ThrowIfCancellationRequested();
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "created topics {Topics}")]
    private partial void LogCreated(string topics);

    [LoggerMessage(Level = LogLevel.Information, Message = "topics present {Topics}")]
    private partial void LogPresent(string topics);
}

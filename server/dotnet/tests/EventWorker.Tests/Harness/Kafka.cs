using Confluent.Kafka;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Sa.Events.V1;
using Sa.EventWorker.Topics;

namespace Sa.EventWorker.Tests.Harness;

/// <summary>Kafka helpers: per-test topic names, provisioning, and a plain consumer that reads envelopes.</summary>
internal static class Kafka
{
    private static readonly TimeSpan ReadTimeout = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan PollTimeout = TimeSpan.FromMilliseconds(500);

    /// <summary>Topic options whose names are unique to this test, so tests never see each other's events.</summary>
    public static IOptions<KafkaOptions> FreshTopics(string bootstrapServers)
    {
        string suffix = Guid.NewGuid().ToString("N")[..8];
        return Options.Create(new KafkaOptions
        {
            BootstrapServers = bootstrapServers,
            CharacterEvents = new TopicOptions { Name = $"test.character-events.{suffix}", Partitions = 3 },
        });
    }

    public static async Task ProvisionAsync(IOptions<KafkaOptions> options)
    {
        using IAdminClient admin = new AdminClientBuilder(new AdminClientConfig { BootstrapServers = options.Value.BootstrapServers }).Build();
        var provisioner = new KafkaTopicProvisioner(options, NullLogger<KafkaTopicProvisioner>.Instance);
        await provisioner.EnsureAsync(admin, options.Value, CancellationToken.None);
    }

    /// <summary>Reads up to <paramref name="count"/> envelopes from the start of the topic, giving up after the timeout.</summary>
    public static List<(string Key, EventEnvelope Envelope)> ReadEnvelopes(string bootstrapServers, string topic, int count)
    {
        var config = new ConsumerConfig
        {
            BootstrapServers = bootstrapServers,
            GroupId = "reader-" + Guid.NewGuid().ToString("N"),
            AutoOffsetReset = AutoOffsetReset.Earliest,
            EnableAutoCommit = false,
        };
        using IConsumer<string, byte[]> consumer = new ConsumerBuilder<string, byte[]>(config).Build();
        consumer.Subscribe(topic);
        var read = new List<(string, EventEnvelope)>();
        DateTime deadline = DateTime.UtcNow + ReadTimeout;
        while (read.Count < count && DateTime.UtcNow < deadline)
        {
            ConsumeResult<string, byte[]>? result = consumer.Consume(PollTimeout);
            if (result is not null)
            {
                read.Add((result.Message.Key, EventEnvelope.Parser.ParseFrom(result.Message.Value)));
            }
        }

        consumer.Close();
        return read;
    }
}

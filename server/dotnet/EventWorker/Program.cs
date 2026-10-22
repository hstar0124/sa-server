using Microsoft.Extensions.Options;
using Npgsql;
using Sa.EventWorker.Consuming;
using Sa.EventWorker.Infrastructure;
using Sa.EventWorker.Publishing;
using Sa.EventWorker.Ranking;
using Sa.EventWorker.Topics;
using StackExchange.Redis;

HostApplicationBuilder builder = Host.CreateApplicationBuilder(args);

// Environment overrides use the .NET convention: Database__ConnectionString=..., Kafka__BootstrapServers=...
builder.Services.AddOptions<DatabaseOptions>().Bind(builder.Configuration.GetSection(DatabaseOptions.SectionName)).ValidateDataAnnotations().ValidateOnStart();
builder.Services.AddOptions<KafkaOptions>().Bind(builder.Configuration.GetSection(KafkaOptions.SectionName)).ValidateDataAnnotations().ValidateOnStart();
builder.Services.AddOptions<RedisOptions>().Bind(builder.Configuration.GetSection(RedisOptions.SectionName)).ValidateDataAnnotations().ValidateOnStart();
builder.Services.AddOptions<RankingOptions>().Bind(builder.Configuration.GetSection(RankingOptions.SectionName)).ValidateDataAnnotations().ValidateOnStart();
builder.Services.AddOptions<ConsumerOptions>().Bind(builder.Configuration.GetSection(ConsumerOptions.SectionName)).ValidateDataAnnotations().ValidateOnStart();
builder.Services.AddOptions<OutboxPublisherOptions>().Bind(builder.Configuration.GetSection(OutboxPublisherOptions.SectionName)).ValidateDataAnnotations().ValidateOnStart();

builder.Services.AddSingleton(provider =>
{
    DatabaseOptions database = provider.GetRequiredService<IOptions<DatabaseOptions>>().Value;
    var dataSource = new NpgsqlDataSourceBuilder(database.ConnectionString) { ConnectionStringBuilder = { MaxPoolSize = database.MaxPoolSize } };
    dataSource.UseLoggerFactory(provider.GetRequiredService<ILoggerFactory>());
    return dataSource.Build();
});
builder.Services.AddSingleton<IEventProducer>(provider => new KafkaEventProducer(
    provider.GetRequiredService<IOptions<KafkaOptions>>().Value.BootstrapServers,
    provider.GetRequiredService<IOptions<OutboxPublisherOptions>>().Value.PublishTimeout));
builder.Services.AddSingleton<TopicRouting>();
builder.Services.AddSingleton<OutboxRepository>();
builder.Services.AddSingleton<OutboxPublisher>();
builder.Services.AddSingleton<ProjectionStore>();
builder.Services.AddSingleton<IConnectionMultiplexer>(provider =>
    ConnectionMultiplexer.Connect(provider.GetRequiredService<IOptions<RedisOptions>>().Value.ConnectionString));
builder.Services.AddSingleton<RankingWriter>();

// Topics first: hosted services start in registration order and the provisioner completes before returning.
builder.Services.AddHostedService<KafkaTopicProvisioner>();
builder.Services.AddHostedService<OutboxPublisherService>();
builder.Services.AddHostedService<CharacterStatsConsumer>();
builder.Services.AddHostedService<RankingReconciler>();

IHost host = builder.Build();
host.Run();

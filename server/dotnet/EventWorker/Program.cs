using Sa.EventWorker.Publishing;

HostApplicationBuilder builder = Host.CreateApplicationBuilder(args);

builder.Services.AddOptions<OutboxPublisherOptions>()
    .Bind(builder.Configuration.GetSection(OutboxPublisherOptions.SectionName))
    .ValidateDataAnnotations()
    .ValidateOnStart();
builder.Services.AddHostedService<OutboxPublisherService>();

IHost host = builder.Build();
host.Run();

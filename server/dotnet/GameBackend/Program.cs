using Sa.GameBackend.Infrastructure.Database;
using Sa.GameBackend.Infrastructure.Http;
using Sa.GameBackend.Modules.Auth;
using Sa.GameBackend.Modules.Characters;
using Sa.GameBackend.Modules.WorldDirectory;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

// Environment overrides use the .NET convention: Database__ConnectionString=..., Auth__SigningKey=...
builder.Services.AddSingleton(TimeProvider.System);
builder.Services.AddDatabase(builder.Configuration);
builder.Services.AddViewerCors(builder.Configuration);
builder.Services.AddAuthModule(builder.Configuration);
builder.Services.AddCharactersModule(builder.Configuration);
builder.Services.AddWorldDirectoryModule(builder.Configuration);

builder.Services.AddGrpc();
builder.Services.AddProblemDetails();
builder.Services.AddHealthChecks()
    .AddCheck<PostgresHealthCheck>(PostgresHealthCheck.Name, tags: [HealthTags.Ready]);

WebApplication app = builder.Build();

app.UseExceptionHandler();
app.UseStatusCodePages();
app.UseCors(CorsOptions.PolicyName);
app.UseRateLimiter();
app.UseAuthentication();
app.UseAuthorization();

app.MapAuthEndpoints();
app.MapCharacterEndpoints();
app.MapEntryEndpoints();
app.MapGrpcService<WorldDirectoryGrpcService>();

// Liveness answers while the process runs; readiness also requires the database.
app.MapHealthChecks("/health/live", new() { Predicate = _ => false });
app.MapHealthChecks("/health/ready", new() { Predicate = check => check.Tags.Contains(HealthTags.Ready) });

app.Run();

internal static class HealthTags
{
    public const string Ready = "ready";
}

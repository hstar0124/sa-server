using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;

namespace Sa.GameBackend.IntegrationTests.Harness;

/// <summary>Thin typed wrapper over the REST surface so tests read like the flows they verify.</summary>
internal sealed class ApiClient(HttpClient http, string? bearerToken = null)
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    public Task<HttpResponseMessage> RegisterAsync(string username, string password)
        => SendAsync(HttpMethod.Post, "/api/v1/auth/register", new { username, password });

    public Task<HttpResponseMessage> LoginAsync(string username, string password)
        => SendAsync(HttpMethod.Post, "/api/v1/auth/login", new { username, password });

    /// <summary>Registers a fresh account and returns its access token.</summary>
    public async Task<string> RegisterAndLoginAsync(string username, string password = "correct horse battery")
    {
        (await RegisterAsync(username, password)).EnsureSuccessStatusCode();
        HttpResponseMessage login = await LoginAsync(username, password);
        login.EnsureSuccessStatusCode();
        return (await login.Content.ReadFromJsonAsync<LoginResponse>(Json))!.AccessToken;
    }

    /// <summary>Registers and logs in a fresh account; returns a client whose requests carry its token.</summary>
    public async Task<ApiClient> AsNewAccountAsync(string username, string password = "correct horse battery")
        => WithToken(await RegisterAndLoginAsync(username, password));

    public ApiClient WithToken(string token) => new(http, token);

    public Task<HttpResponseMessage> CreateCharacterAsync(long worldId, string name, int classId = 1)
        => SendAsync(HttpMethod.Post, $"/api/v1/worlds/{worldId}/characters", new { name, classId });

    public Task<HttpResponseMessage> GetCharactersAsync(long worldId)
        => SendAsync(HttpMethod.Get, $"/api/v1/worlds/{worldId}/characters", body: null);

    public async Task<IReadOnlyList<CharacterResponse>> ListCharactersAsync(long worldId)
    {
        HttpResponseMessage response = await GetCharactersAsync(worldId);
        response.EnsureSuccessStatusCode();
        return (await response.Content.ReadFromJsonAsync<List<CharacterResponse>>(Json))!;
    }

    public Task<HttpResponseMessage> GetStatsAsync(long worldId, long characterId)
        => SendAsync(HttpMethod.Get, $"/api/v1/worlds/{worldId}/characters/{characterId}/stats", body: null);

    public Task<HttpResponseMessage> EnterAsync(long worldId, long characterId)
        => SendAsync(HttpMethod.Post, $"/api/v1/worlds/{worldId}/characters/{characterId}/enter", body: null);

    /// <summary>Requests entry for an existing character; returns the signed ticket bytes.</summary>
    public async Task<byte[]> TicketForAsync(long worldId, long characterId)
    {
        HttpResponseMessage enter = await EnterAsync(worldId, characterId);
        enter.EnsureSuccessStatusCode();
        EnterResponse body = (await enter.Content.ReadFromJsonAsync<EnterResponse>(Json))!;
        return Convert.FromBase64String(body.EntryTicket);
    }

    /// <summary>Creates a character and requests entry; returns the signed ticket bytes and the character id.</summary>
    public async Task<(byte[] SignedTicket, long CharacterId)> CreateCharacterAndEnterAsync(long worldId, string name)
    {
        HttpResponseMessage created = await CreateCharacterAsync(worldId, name);
        created.EnsureSuccessStatusCode();
        long characterId = (await created.Content.ReadFromJsonAsync<CharacterResponse>(Json))!.CharacterId;
        HttpResponseMessage enter = await EnterAsync(worldId, characterId);
        enter.EnsureSuccessStatusCode();
        EnterResponse body = (await enter.Content.ReadFromJsonAsync<EnterResponse>(Json))!;
        return (Convert.FromBase64String(body.EntryTicket), characterId);
    }

    public static async Task<string> ProblemTypeOf(HttpResponseMessage response)
    {
        using JsonDocument doc = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
        return doc.RootElement.GetProperty("type").GetString() ?? string.Empty;
    }

    private async Task<HttpResponseMessage> SendAsync(HttpMethod method, string path, object? body)
    {
        using var request = new HttpRequestMessage(method, new Uri(path, UriKind.Relative));
        if (body is not null)
        {
            request.Content = JsonContent.Create(body, options: Json);
        }

        if (bearerToken is not null)
        {
            request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", bearerToken);
        }

        return await http.SendAsync(request);
    }

    public sealed record LoginResponse(string AccessToken, DateTimeOffset ExpiresAt);

    public sealed record CharacterResponse(long CharacterId, long WorldId, string Name, int ClassId);

    public sealed record GatewayEndpoint(string Host, int Port, int WebSocketPort);

    public sealed record EnterResponse(string EntryTicket, GatewayEndpoint Gateway, DateTimeOffset ExpiresAt);

    public sealed record StatsResponse(long Kills, long ItemsAcquired, long GoldEarned, long AsOfEventSeq);
}

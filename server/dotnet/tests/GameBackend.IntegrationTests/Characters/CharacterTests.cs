using System.Net;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Characters;

[Collection(SharedPostgres.Name)]
public sealed class CharacterTests(PostgresFixture postgres) : IDisposable
{
    private const long World = 1;
    private const long MissingWorld = 999;
    private const int MaxPerWorld = 3;
    private const int UndefinedClass = 99;  // game-data defines classes 1-3

    private readonly BackendFactory _factory = new(postgres.ConnectionString, new BackendSettings(MaxCharactersPerWorld: MaxPerWorld));

    private ApiClient Api => new(_factory.CreateClient());

    [Fact]
    public async Task Created_character_is_persisted_and_listed_for_its_owner_only()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        ApiClient other = await Api.AsNewAccountAsync(Unique.Username());
        string name = Unique.CharacterName();

        HttpResponseMessage created = await owner.CreateCharacterAsync(World, name);

        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        Assert.Contains(await owner.ListCharactersAsync(World), c => c.Name == name && c.WorldId == World);
        Assert.DoesNotContain(await other.ListCharactersAsync(World), c => c.Name == name);
        long rows = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.characters WHERE world_id = @World AND name = @name", new { World, name });
        Assert.Equal(1, rows);
    }

    [Fact]
    public async Task Character_names_are_unique_per_world_ignoring_case()
    {
        ApiClient a = await Api.AsNewAccountAsync(Unique.Username());
        ApiClient b = await Api.AsNewAccountAsync(Unique.Username());
        string name = "Hero" + Unique.CharacterName()[..6];

        (await a.CreateCharacterAsync(World, name)).EnsureSuccessStatusCode();
        HttpResponseMessage clash = await b.CreateCharacterAsync(World, name.ToUpperInvariant());

        Assert.Equal(HttpStatusCode.Conflict, clash.StatusCode);
        Assert.Equal("urn:sa:problem:character-name-taken", await ApiClient.ProblemTypeOf(clash));
        long rows = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.characters WHERE world_id = @World AND lower(name) = lower(@name)", new { World, name });
        Assert.Equal(1, rows);
    }

    [Fact]
    public async Task Per_world_limit_holds_under_concurrent_creation()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        const int attempts = MaxPerWorld * 4;

        HttpResponseMessage[] responses = await Task.WhenAll(
            Enumerable.Range(0, attempts).Select(_ => owner.CreateCharacterAsync(World, Unique.CharacterName())));

        int created = responses.Count(r => r.StatusCode == HttpStatusCode.Created);
        int refused = responses.Count(r => r.StatusCode == HttpStatusCode.UnprocessableEntity);
        Assert.Equal(MaxPerWorld, created);
        Assert.Equal(attempts - MaxPerWorld, refused);
        Assert.Equal(MaxPerWorld, (await owner.ListCharactersAsync(World)).Count);
    }

    [Fact]
    public async Task Unknown_world_is_404_and_writes_nothing()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        string name = Unique.CharacterName();

        HttpResponseMessage response = await owner.CreateCharacterAsync(MissingWorld, name);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        long rows = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.characters WHERE name = @name", new { name });
        Assert.Equal(0, rows);
    }

    [Fact]
    public async Task A_class_the_content_does_not_define_is_422_and_writes_nothing()
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());
        string name = Unique.CharacterName();

        HttpResponseMessage response = await owner.CreateCharacterAsync(World, name, classId: UndefinedClass);

        Assert.Equal(HttpStatusCode.UnprocessableEntity, response.StatusCode);
        long rows = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.characters WHERE name = @name", new { name });
        Assert.Equal(0, rows);
    }

    [Theory]
    [InlineData("x")]            // too short
    [InlineData("has space")]    // forbidden character
    [InlineData("열일곱글자가넘어가는이름입니다요정")]  // 17 characters, too long
    public async Task Malformed_names_are_rejected(string name)
    {
        ApiClient owner = await Api.AsNewAccountAsync(Unique.Username());

        HttpResponseMessage response = await owner.CreateCharacterAsync(World, name);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }

    public void Dispose() => _factory.Dispose();
}

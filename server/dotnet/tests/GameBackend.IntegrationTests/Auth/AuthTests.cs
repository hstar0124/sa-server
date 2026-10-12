using System.Net;
using Sa.GameBackend.IntegrationTests.Harness;

namespace Sa.GameBackend.IntegrationTests.Auth;

[Collection(SharedPostgres.Name)]
public sealed class AuthTests(PostgresFixture postgres) : IDisposable
{
    private readonly BackendFactory _factory = new(postgres.ConnectionString);

    private ApiClient Api => new(_factory.CreateClient());

    [Fact]
    public async Task Register_stores_one_account_with_a_hashed_password()
    {
        string username = Unique.Username();
        const string password = "correct horse battery";

        HttpResponseMessage response = await Api.RegisterAsync(username, password);

        Assert.Equal(HttpStatusCode.Created, response.StatusCode);
        long rows = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.accounts WHERE username = @username", new { username });
        Assert.Equal(1, rows);
        string hash = await Db.ScalarAsync<string>(postgres.ConnectionString,
            "SELECT password_hash FROM game.accounts WHERE username = @username", new { username });
        Assert.DoesNotContain(password, hash, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Duplicate_username_is_rejected_by_the_unique_constraint_leaving_one_row()
    {
        string username = Unique.Username();
        (await Api.RegisterAsync(username, "correct horse battery")).EnsureSuccessStatusCode();

        HttpResponseMessage second = await Api.RegisterAsync(username, "another password");

        Assert.Equal(HttpStatusCode.Conflict, second.StatusCode);
        Assert.Equal("urn:sa:problem:username-taken", await ApiClient.ProblemTypeOf(second));
        long rows = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.accounts WHERE username = @username", new { username });
        Assert.Equal(1, rows);
    }

    [Theory]
    [InlineData("ab", "correct horse battery")]            // too short
    [InlineData("Has_Upper", "correct horse battery")]     // uppercase
    [InlineData("valid_user", "short")]                    // password below minimum
    public async Task Malformed_credentials_are_rejected_before_touching_the_database(string username, string password)
    {
        HttpResponseMessage response = await Api.RegisterAsync(username, password);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        long rows = await Db.ScalarAsync<long>(postgres.ConnectionString,
            "SELECT count(*) FROM game.accounts WHERE username = @username", new { username });
        Assert.Equal(0, rows);
    }

    [Fact]
    public async Task Login_returns_a_bearer_token_that_authorises_protected_endpoints()
    {
        string username = Unique.Username();
        ApiClient authed = await Api.AsNewAccountAsync(username);

        HttpResponseMessage response = await authed.GetCharactersAsync(1);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task Wrong_password_and_unknown_user_are_indistinguishable_401s()
    {
        string username = Unique.Username();
        (await Api.RegisterAsync(username, "correct horse battery")).EnsureSuccessStatusCode();

        HttpResponseMessage wrongPassword = await Api.LoginAsync(username, "wrong password!");
        HttpResponseMessage unknownUser = await Api.LoginAsync(Unique.Username("ghost"), "whatever password");

        Assert.Equal(HttpStatusCode.Unauthorized, wrongPassword.StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, unknownUser.StatusCode);
        Assert.Equal(await ApiClient.ProblemTypeOf(wrongPassword), await ApiClient.ProblemTypeOf(unknownUser));
    }

    [Fact]
    public async Task Protected_endpoint_without_token_is_401()
    {
        HttpResponseMessage response = await Api.GetCharactersAsync(1);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task Token_signed_with_another_key_is_401()
    {
        using var otherBackend = new BackendFactory(postgres.ConnectionString);
        string foreignToken = await new ApiClient(otherBackend.CreateClient()).RegisterAndLoginAsync(Unique.Username());

        HttpResponseMessage response = await Api.WithToken(foreignToken).GetCharactersAsync(1);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    public void Dispose() => _factory.Dispose();
}

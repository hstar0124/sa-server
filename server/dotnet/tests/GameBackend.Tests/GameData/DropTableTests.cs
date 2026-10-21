using Sa.GameBackend.Common;
using Sa.GameBackend.Infrastructure.GameData;

namespace Sa.GameBackend.Tests.GameData;

public sealed class DropTableTests
{
    // Plain ints so the theories can name them in attributes.
    private const int Potion = 1;
    private const int Ore = 2;
    private const int Unlisted = 3;

    // Two entries name the potion, one names the ore: a kill yields at most two potions and one ore.
    private static readonly DropTable Table = new(new MonsterTemplateId(1), Gold: 5,
        new Dictionary<ItemTemplateId, int> { [new(Potion)] = 2, [new(Ore)] = 1 });

    private static bool Accepts(params int[] rolled) => Table.Accepts(rolled.Select(id => new ItemTemplateId(id)));

    [Theory]
    [InlineData]
    [InlineData(Potion)]
    [InlineData(Potion, Potion, Ore)]
    public void Accepts_rolls_within_the_entries(params int[] rolled) => Assert.True(Accepts(rolled));

    [Theory]
    [InlineData(Potion, Potion, Potion)]
    [InlineData(Ore, Ore)]
    public void Refuses_more_of_an_item_than_its_entries(params int[] rolled) => Assert.False(Accepts(rolled));

    [Fact]
    public void Refuses_an_item_the_table_does_not_list() => Assert.False(Accepts(Potion, Unlisted));
}

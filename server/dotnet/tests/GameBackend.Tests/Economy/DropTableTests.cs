using Sa.GameBackend.Modules.Economy;

namespace Sa.GameBackend.Tests.Economy;

public sealed class DropTableTests
{
    private const int Potion = 1;
    private const int Ore = 2;
    private const int Unlisted = 3;

    // Two entries name the potion, one names the ore: a kill yields at most two potions and one ore.
    private static readonly DropTable Table = new(MonsterTemplateId: 1, Gold: 5,
        new Dictionary<int, int> { [Potion] = 2, [Ore] = 1 });

    [Theory]
    [InlineData]
    [InlineData(Potion)]
    [InlineData(Potion, Potion, Ore)]
    public void Accepts_rolls_within_the_entries(params int[] rolled) => Assert.True(Table.Accepts(rolled));

    [Theory]
    [InlineData(Potion, Potion, Potion)]
    [InlineData(Ore, Ore)]
    public void Refuses_more_of_an_item_than_its_entries(params int[] rolled) => Assert.False(Table.Accepts(rolled));

    [Fact]
    public void Refuses_an_item_the_table_does_not_list() => Assert.False(Table.Accepts([Potion, Unlisted]));
}

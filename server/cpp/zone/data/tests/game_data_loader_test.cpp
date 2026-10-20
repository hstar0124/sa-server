#include "sa/zone/data/game_data_loader.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>

namespace sa::zone::data {
namespace {

const std::filesystem::path kGameData{SA_GAME_DATA_DIR};

TEST(GameDataLoader, ShippedMapAndRulesAreValid) {
    const auto map = LoadMap(kGameData / "maps" / "zone-1.toml");
    ASSERT_TRUE(map.has_value()) << map.error().message;
    EXPECT_GT(map->obstacles.size(), 0U);
    EXPECT_GT(map->spawn_points.size(), 0U);

    const auto rules = LoadMovementRules(kGameData / "rules" / "movement.toml");
    ASSERT_TRUE(rules.has_value()) << rules.error().message;
    EXPECT_GE(rules->aoi_leave_radius_m, rules->aoi_enter_radius_m);
    const auto combat = LoadCombatRules(kGameData / "rules" / "movement.toml");
    ASSERT_TRUE(combat.has_value()) << combat.error().message;
    const auto walkable =
        map->ValidateWalkable(rules->body_radius_m, combat->monster_pack_ring_radius_m);
    EXPECT_TRUE(walkable.has_value()) << walkable.error();
}

TEST(GameDataLoader, ParsesMinimalMap) {
    const auto map = ParseMap(R"(
[map]
width = 10.0
height = 20.0
cell_size = 5.0
obstacles = [[1.0, 1.0, 2.0, 2.0]]
spawn_points = [[5.0, 5.0]]
)");
    ASSERT_TRUE(map.has_value()) << map.error().message;
    EXPECT_EQ(map->size, (common::Vec2{10.0F, 20.0F}));
    ASSERT_EQ(map->obstacles.size(), 1U);
    EXPECT_EQ(map->obstacles[0].max, (common::Vec2{2.0F, 2.0F}));
}

TEST(GameDataLoader, RejectsMalformedTuplesAndSemanticErrors) {
    EXPECT_FALSE(ParseMap("[map]\nwidth = 10.0\nheight = 10.0\ncell_size = 5.0\nobstacles = [[1.0, "
                          "2.0]]\nspawn_points = [[5.0, 5.0]]\n")
                     .has_value());
    EXPECT_FALSE(ParseMap("[map]\nwidth = 10.0\nheight = 10.0\ncell_size = 5.0\nobstacles = "
                          "[]\nspawn_points = [[50.0, 5.0]]\n")
                     .has_value())
        << "spawn outside map";
    EXPECT_FALSE(ParseMap("[map]\nwidth = 10.0\n").has_value());
    EXPECT_FALSE(ParseMap("[map\n").has_value());
}

TEST(GameDataLoader, RejectsNumbersThatAreNotFinite) {
    // TOML allows nan and inf; neither is a size or a speed. A NaN cell size passes the "<= 0"
    // check and reaches an undefined float-to-integer cast in the spatial grid.
    const auto map = ParseMap(
        "[map]\nwidth = 10.0\nheight = 10.0\ncell_size = nan\nobstacles = []\n"
        "spawn_points = [[5.0, 5.0]]\n");
    ASSERT_FALSE(map.has_value());
    EXPECT_NE(map.error().message.find("map.cell_size"), std::string::npos);
    EXPECT_FALSE(ParseMap("[map]\nwidth = 10.0\nheight = 10.0\ncell_size = 5.0\nobstacles = "
                          "[]\nspawn_points = [[inf, 5.0]]\n")
                     .has_value());
    EXPECT_FALSE(ParseMap("[map]\nwidth = 1e300\nheight = 10.0\ncell_size = 5.0\nobstacles = "
                          "[]\nspawn_points = [[5.0, 5.0]]\n")
                     .has_value())
        << "beyond the float range";
}

TEST(GameDataLoader, RejectsRulesWithLeaveBelowEnter) {
    const auto rules = ParseMovementRules(R"(
[movement]
max_speed_mps = 6.0
body_radius_m = 0.4
[aoi]
enter_radius_m = 40.0
leave_radius_m = 30.0
[input]
commands_per_tick_budget = 10
max_queued_per_player = 4
)");
    ASSERT_FALSE(rules.has_value());
    EXPECT_NE(rules.error().message.find("leave_radius_m"), std::string::npos);
}

TEST(GameDataLoader, ShippedContentBundleIsValidAndMatchesTheMap) {
    const auto content = LoadContent(kGameData);
    ASSERT_TRUE(content.has_value()) << content.error().message;
    EXPECT_FALSE(content->version.empty());
    EXPECT_GE(content->skills.size(), 3U);
    EXPECT_GE(content->classes.size(), 1U);
    EXPECT_GE(content->monsters.size(), 1U);

    const auto map = LoadMap(kGameData / "maps" / "zone-1.toml");
    ASSERT_TRUE(map.has_value());
    EXPECT_FALSE(map->monster_spawns.empty());
    EXPECT_TRUE(map->ValidateAgainst(*content).has_value());

    const auto combat = LoadCombatRules(kGameData / "rules" / "movement.toml");
    ASSERT_TRUE(combat.has_value()) << combat.error().message;
    EXPECT_GT(combat->monster_think_hz, 0U);
}

constexpr std::string_view kVersion = "[content]\nversion = \"test\"\n";
constexpr std::string_view kSkills = R"(
[[skills]]
id = 1
name = "strike"
kind = "targeted"
cost_mp = 0
cooldown_ms = 500
cast_time_ms = 0
range_m = 2.0
damage = 10
)";
constexpr std::string_view kMonsters = R"(
[[monsters]]
template_id = 7
name = "rat"
max_hp = 10
speed_mps = 2.0
aggro_radius_m = 5.0
leash_radius_m = 10.0
attack_range_m = 1.0
attack_damage = 1
attack_cooldown_ms = 1000
respawn_ms = 1000
)";

TEST(GameDataLoader, ContentRejectsUnknownSkillReferencesAndBadProjectiles) {
    constexpr std::string_view kClassWithSkill99 =
        "[[classes]]\nid = 1\nname = \"w\"\nmax_hp = 10\nmax_mp = 0\nmp_regen_per_sec = "
        "0.0\nskills = [99]\n";
    constexpr std::string_view kClassWithSkill1 =
        "[[classes]]\nid = 1\nname = \"w\"\nmax_hp = 10\nmax_mp = 0\nmp_regen_per_sec = "
        "0.0\nskills = [1]\n";
    constexpr std::string_view kProjectileWithoutSpeed = R"(
[[skills]]
id = 3
name = "arrow"
kind = "projectile"
cost_mp = 1
cooldown_ms = 1
cast_time_ms = 0
range_m = 10.0
damage = 1
)";

    const auto unknown_skill = ParseContent({.version = kVersion,
                                             .classes = kClassWithSkill99,
                                             .skills = kSkills,
                                             .monsters = kMonsters});
    EXPECT_FALSE(unknown_skill.has_value());

    const auto projectile_without_speed = ParseContent(
        {.version = kVersion, .skills = kProjectileWithoutSpeed, .monsters = kMonsters});
    EXPECT_FALSE(projectile_without_speed.has_value());

    const auto ok = ParseContent({.version = kVersion,
                                  .classes = kClassWithSkill1,
                                  .skills = kSkills,
                                  .monsters = kMonsters});
    ASSERT_TRUE(ok.has_value()) << ok.error().message;
    EXPECT_EQ(ok->skills.at(sim::SkillId{1}).cooldown.count(), 500);
}

TEST(GameDataLoader, ShippedItemsDropsAndEconomyRulesLoad) {
    const auto content = LoadContent(kGameData);
    ASSERT_TRUE(content.has_value()) << content.error().message;
    EXPECT_FALSE(content->items.empty());
    EXPECT_FALSE(content->drops.empty());
    for (const auto& [monster, table] : content->drops) {
        EXPECT_TRUE(content->monsters.contains(monster));
        for (const auto& entry : table.entries) {
            EXPECT_TRUE(content->items.contains(entry.item));
        }
    }
    const auto economy = LoadEconomyRules(kGameData / "rules" / "economy.toml");
    ASSERT_TRUE(economy.has_value()) << economy.error().message;
    EXPECT_GT(economy->inventory_capacity, 0U);
    EXPECT_GT(economy->pickup_range_m, 0.0F);
    EXPECT_LT(economy->drop_scatter_radius_m, economy->pickup_range_m)
        << "a player at the corpse reaches every drop";
    EXPECT_FALSE(economy->primary_currency.empty());
}

TEST(GameDataLoader, DropTablesMustReferenceKnownItemsWithChancesInRange) {
    constexpr std::string_view kItems = "[[items]]\nid = 5\nname = \"gel\"\n";
    const auto drops_for = [](std::string_view item, std::string_view chance) {
        return std::format(
            "[[drop_tables]]\nmonster_template_id = 7\ngold = 1\n[[drop_tables.entries]]\n"
            "item_template_id = {}\nchance = {}\n",
            item, chance);
    };

    const auto unknown_item = drops_for("99", "0.5");
    EXPECT_FALSE(ParseContent({.version = kVersion,
                               .skills = kSkills,
                               .monsters = kMonsters,
                               .items = kItems,
                               .drops = unknown_item})
                     .has_value());

    const auto bad_chance = drops_for("5", "1.5");
    EXPECT_FALSE(ParseContent({.version = kVersion,
                               .skills = kSkills,
                               .monsters = kMonsters,
                               .items = kItems,
                               .drops = bad_chance})
                     .has_value());

    constexpr std::string_view kUnknownMonster =
        "[[drop_tables]]\nmonster_template_id = 8\ngold = 1\n";
    EXPECT_FALSE(ParseContent({.version = kVersion,
                               .skills = kSkills,
                               .monsters = kMonsters,
                               .items = kItems,
                               .drops = kUnknownMonster})
                     .has_value());

    const auto valid = drops_for("5", "0.5");
    const auto ok = ParseContent({.version = kVersion,
                                  .skills = kSkills,
                                  .monsters = kMonsters,
                                  .items = kItems,
                                  .drops = valid});
    ASSERT_TRUE(ok.has_value()) << ok.error().message;
    EXPECT_EQ(ok->drops.at(sim::MonsterTemplateId{7}).entries.size(), 1U);
}

TEST(GameDataLoader, AnItemsStackableFlagMustBeABooleanWhenGiven) {
    const auto plain =
        ParseContent({.version = kVersion, .items = "[[items]]\nid = 5\nname = \"gel\"\n"});
    ASSERT_TRUE(plain.has_value()) << plain.error().message;
    EXPECT_FALSE(plain->items.at(sim::ItemTemplateId{5}).stackable) << "absent means not stackable";

    const auto mistyped = ParseContent(
        {.version = kVersion, .items = "[[items]]\nid = 5\nname = \"gel\"\nstackable = \"yes\"\n"});
    EXPECT_FALSE(mistyped.has_value());
}

TEST(GameDataLoader, MapRejectsSpawnOfUnknownMonsterTemplate) {
    const auto map = ParseMap(R"(
[map]
width = 10.0
height = 10.0
cell_size = 5.0
obstacles = []
spawn_points = [[5.0, 5.0]]
[[monster_spawns]]
template_id = 42
position = [2.0, 2.0]
count = 1
)");
    ASSERT_TRUE(map.has_value()) << map.error().message;
    const auto content =
        ParseContent({.version = kVersion, .skills = kSkills, .monsters = kMonsters});
    ASSERT_TRUE(content.has_value());
    EXPECT_FALSE(map->ValidateAgainst(*content).has_value());
}

}  // namespace
}  // namespace sa::zone::data

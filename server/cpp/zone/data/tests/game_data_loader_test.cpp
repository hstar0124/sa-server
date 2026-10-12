#include "sa/zone/data/game_data_loader.hpp"

#include <gtest/gtest.h>

#include <filesystem>

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

TEST(GameDataLoader, RejectsRulesWithLeaveBelowEnter) {
    const auto rules = ParseMovementRules(R"(
[movement]
max_speed_mps = 6.0
[aoi]
enter_radius_m = 40.0
leave_radius_m = 30.0
[input]
commands_per_tick_budget = 10
)");
    EXPECT_FALSE(rules.has_value());
}

}  // namespace
}  // namespace sa::zone::data

namespace sa::zone::data {
namespace {

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
    const auto unknown_skill =
        ParseContent(kVersion,
                     "[[classes]]\nid = 1\nname = \"w\"\nmax_hp = 10\nmax_mp = 0\nmp_regen_per_sec "
                     "= 0.0\nskills = [99]\n",
                     kSkills, kMonsters);
    EXPECT_FALSE(unknown_skill.has_value());

    const auto projectile_without_speed = ParseContent(kVersion, "", R"(
[[skills]]
id = 3
name = "arrow"
kind = "projectile"
cost_mp = 1
cooldown_ms = 1
cast_time_ms = 0
range_m = 10.0
damage = 1
)",
                                                       kMonsters);
    EXPECT_FALSE(projectile_without_speed.has_value());

    const auto ok = ParseContent(kVersion,
                                 "[[classes]]\nid = 1\nname = \"w\"\nmax_hp = 10\nmax_mp = "
                                 "0\nmp_regen_per_sec = 0.0\nskills = [1]\n",
                                 kSkills, kMonsters);
    ASSERT_TRUE(ok.has_value()) << ok.error().message;
    EXPECT_EQ(ok->skills.at(sim::SkillId{1}).cooldown.count(), 500);
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
    const auto content = ParseContent(kVersion, "", kSkills, kMonsters);
    ASSERT_TRUE(content.has_value());
    EXPECT_FALSE(map->ValidateAgainst(*content).has_value());
}

}  // namespace
}  // namespace sa::zone::data

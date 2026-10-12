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

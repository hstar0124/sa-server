#include "sa/zone/sim/map_data.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

namespace sa::zone::sim {
namespace {

TEST(MapData, ValidFixturesPass) {
    EXPECT_TRUE(test::OpenField().Validate().has_value());
    EXPECT_TRUE(test::FieldWithWall().Validate().has_value());
    EXPECT_TRUE(test::Rules().Validate().has_value());
}

TEST(MapData, RejectsObstacleOutsideMapAndSpawnInsideObstacle) {
    MapData outside = test::OpenField();
    outside.obstacles.push_back(common::Aabb{.min = {90.0F, 90.0F}, .max = {110.0F, 95.0F}});
    EXPECT_FALSE(outside.Validate().has_value());

    MapData blocked = test::OpenField();
    blocked.obstacles.push_back(
        common::Aabb{.min = {5.0F, 5.0F}, .max = {15.0F, 15.0F}});  // covers spawn (10,10)
    EXPECT_FALSE(blocked.Validate().has_value());
}

TEST(MapData, RejectsMissingSpawnPointsAndBadCellSize) {
    MapData no_spawn = test::OpenField();
    no_spawn.spawn_points.clear();
    EXPECT_FALSE(no_spawn.Validate().has_value());

    MapData bad_cell = test::OpenField();
    bad_cell.cell_size = 0.0F;
    EXPECT_FALSE(bad_cell.Validate().has_value());
}

TEST(MovementRules, LeaveRadiusMustNotBeSmallerThanEnterRadius) {
    MovementRules rules = test::Rules();
    rules.aoi_leave_radius_m = rules.aoi_enter_radius_m - 1.0F;
    EXPECT_FALSE(rules.Validate().has_value());
    rules.aoi_leave_radius_m = rules.aoi_enter_radius_m;
    EXPECT_TRUE(rules.Validate().has_value());
}

}  // namespace
}  // namespace sa::zone::sim

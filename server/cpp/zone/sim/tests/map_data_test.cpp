#include "sa/zone/sim/map_data.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace sa::zone::sim {
namespace {

TEST(MapData, ValidFixturesPass) {
    EXPECT_TRUE(test::OpenField().Validate().has_value());
    EXPECT_TRUE(test::FieldWithWall().Validate().has_value());
    EXPECT_TRUE(test::Rules().Validate().has_value());
}

TEST(MapData, RejectsObstacleOutsideTheMap) {
    MapData outside = test::OpenField();
    outside.obstacles.push_back(common::Aabb{.min = {90.0F, 90.0F}, .max = {110.0F, 95.0F}});
    EXPECT_FALSE(outside.Validate().has_value());
}

// Spawns are judged by the same rule bodies move by: a body may not overlap an obstacle or leave
// the map, so a spawn too close to either would start stuck.
constexpr float kBody = 0.4F;
constexpr float kRing = 1.5F;

TEST(MapData, WalkabilityLeavesRoomForTheBody) {
    const MapData map = test::FieldWithWall();  // wall x 50..52, y 0..60
    EXPECT_TRUE(map.IsWalkable({49.5F, 30.0F}, kBody));
    EXPECT_FALSE(map.IsWalkable({49.7F, 30.0F}, kBody)) << "the body would touch the wall";
    EXPECT_FALSE(map.IsWalkable({0.0F, 0.0F}, kBody)) << "the body would leave the map";
    EXPECT_FALSE(map.IsWalkable({std::numeric_limits<float>::quiet_NaN(), 30.0F}, kBody));
}

TEST(MapData, RejectsSpawnsAndHomesABodyCannotStandOn) {
    EXPECT_TRUE(test::FieldWithSlime().ValidateWalkable(kBody, kRing).has_value());

    MapData blocked = test::OpenField();
    blocked.obstacles.push_back(
        common::Aabb{.min = {5.0F, 5.0F}, .max = {15.0F, 15.0F}});  // covers spawn (10,10)
    EXPECT_FALSE(blocked.ValidateWalkable(kBody, kRing).has_value());

    MapData on_edge = test::OpenField();
    on_edge.spawn_points.emplace_back(0.0F, 0.0F);  // inside the map, but no room for a body
    EXPECT_TRUE(on_edge.Validate().has_value());
    EXPECT_FALSE(on_edge.ValidateWalkable(kBody, kRing).has_value());

    MapData pack_in_wall = test::FieldWithWall();
    pack_in_wall.monster_spawns.push_back(MonsterSpawn{
        .template_id = test::kSlime, .position = {53.0F, 30.0F}, .count = 2});  // ring hits x 51.5
    EXPECT_FALSE(pack_in_wall.ValidateWalkable(kBody, kRing).has_value());
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

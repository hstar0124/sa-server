#include "sa/zone/sim/spatial_grid.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace sa::zone::sim {
namespace {

using common::EntityId;
using common::Vec2;

std::vector<EntityId> Query(const SpatialGrid& grid, Vec2 center, float radius) {
    std::vector<EntityId> out;
    grid.QueryCandidates(center, radius, out);
    std::ranges::sort(out);
    return out;
}

TEST(SpatialGrid, DimensionsRoundUpToCoverTheMap) {
    const SpatialGrid grid{{100.0F, 55.0F}, 10.0F};
    EXPECT_EQ(grid.columns(), 10U);
    EXPECT_EQ(grid.rows(), 6U);
}

TEST(SpatialGrid, QueryReturnsNeighboursButNotFarEntities) {
    SpatialGrid grid{{100.0F, 100.0F}, 10.0F};
    grid.Insert(EntityId{1}, {15.0F, 15.0F});
    grid.Insert(EntityId{2}, {24.0F, 15.0F});  // adjacent cell
    grid.Insert(EntityId{3}, {85.0F, 85.0F});  // far away

    EXPECT_EQ(Query(grid, {15.0F, 15.0F}, 10.0F), (std::vector{EntityId{1}, EntityId{2}}));
    EXPECT_EQ(Query(grid, {85.0F, 85.0F}, 5.0F), (std::vector{EntityId{3}}));
}

TEST(SpatialGrid, MoveAcrossCellsAndRemove) {
    SpatialGrid grid{{100.0F, 100.0F}, 10.0F};
    grid.Insert(EntityId{7}, {5.0F, 5.0F});
    grid.Move(EntityId{7}, {95.0F, 95.0F});

    EXPECT_TRUE(Query(grid, {5.0F, 5.0F}, 1.0F).empty());
    EXPECT_EQ(Query(grid, {95.0F, 95.0F}, 1.0F), (std::vector{EntityId{7}}));

    grid.Remove(EntityId{7});
    EXPECT_TRUE(Query(grid, {95.0F, 95.0F}, 1.0F).empty());
    EXPECT_EQ(grid.size(), 0U);
}

TEST(SpatialGrid, PositionsOutsideTheMapClampToEdgeCells) {
    SpatialGrid grid{{100.0F, 100.0F}, 10.0F};
    grid.Insert(EntityId{1}, {-50.0F, 500.0F});
    EXPECT_EQ(Query(grid, {0.0F, 99.0F}, 1.0F), (std::vector{EntityId{1}}));
}

}  // namespace
}  // namespace sa::zone::sim

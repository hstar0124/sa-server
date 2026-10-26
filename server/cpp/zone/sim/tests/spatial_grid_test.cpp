#include "sa/zone/sim/spatial_grid.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sa::zone::sim {
namespace {

using common::EntityId;
using common::Vec2;

std::vector<EntityId> Query(const SpatialGrid& grid, Vec2 center, float radius) {
    std::vector<SpatialGrid::Entry> found;
    grid.QueryWithin(center, radius, found);
    std::vector<EntityId> out;
    out.reserve(found.size());
    for (const SpatialGrid::Entry& entry : found) {
        out.push_back(entry.id);
    }
    std::ranges::sort(out);
    return out;
}

TEST(SpatialGrid, DimensionsRoundUpToCoverTheMap) {
    const SpatialGrid grid{{100.0F, 55.0F}, 10.0F};
    EXPECT_EQ(grid.columns(), 10U);
    EXPECT_EQ(grid.rows(), 6U);
}

TEST(SpatialGrid, QueryReturnsWhatIsWithinTheRadiusWithItsPosition) {
    SpatialGrid grid{{100.0F, 100.0F}, 10.0F};
    grid.Insert(EntityId{1}, {15.0F, 15.0F});
    grid.Insert(EntityId{2}, {24.0F, 15.0F});  // adjacent cell, 9 m away
    grid.Insert(EntityId{4}, {19.0F, 22.0F});  // a cell the circle overlaps, but 8.1 m away
    grid.Insert(EntityId{3}, {85.0F, 85.0F});  // far away

    EXPECT_EQ(Query(grid, {15.0F, 15.0F}, 10.0F),
              (std::vector{EntityId{1}, EntityId{2}, EntityId{4}}));
    EXPECT_EQ(Query(grid, {15.0F, 15.0F}, 8.0F), (std::vector{EntityId{1}}));
    std::vector<SpatialGrid::Entry> found;
    grid.QueryWithin({85.0F, 85.0F}, 5.0F, found);
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found.front().position, (Vec2{85.0F, 85.0F}));
}

TEST(SpatialGrid, AMoveWithinACellUpdatesThePositionAndRemovalKeepsTheOthers) {
    SpatialGrid grid{{100.0F, 100.0F}, 10.0F};
    for (std::uint64_t id = 1; id <= 4; ++id) {
        grid.Insert(EntityId{id}, {static_cast<float>(id), 5.0F});  // all in cell (0, 0)
    }
    grid.Move(EntityId{2}, {2.5F, 6.0F});
    grid.Remove(EntityId{1});  // the cell's last entry takes its place
    grid.Move(EntityId{4}, {4.0F, 9.0F});

    std::vector<SpatialGrid::Entry> found;
    grid.QueryWithin({5.0F, 5.0F}, 9.0F, found);
    std::ranges::sort(found, {}, &SpatialGrid::Entry::id);
    ASSERT_EQ(found.size(), 3U);
    EXPECT_EQ(found[0].position, (Vec2{2.5F, 6.0F}));
    EXPECT_EQ(found[1].position, (Vec2{3.0F, 5.0F}));
    EXPECT_EQ(found[2].position, (Vec2{4.0F, 9.0F}));
    EXPECT_EQ(grid.size(), 3U);
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

TEST(SpatialGrid, InsertingAnEntityAgainMovesItInsteadOfLeavingACopy) {
    SpatialGrid grid{{100.0F, 100.0F}, 10.0F};
    grid.Insert(EntityId{1}, {5.0F, 5.0F});
    grid.Insert(EntityId{1}, {95.0F, 95.0F});
    EXPECT_TRUE(Query(grid, {5.0F, 5.0F}, 1.0F).empty());
    EXPECT_EQ(Query(grid, {95.0F, 95.0F}, 1.0F), (std::vector{EntityId{1}}));
    EXPECT_EQ(grid.size(), 1U);
}

TEST(SpatialGrid, PositionsOutsideTheMapClampToEdgeCells) {
    SpatialGrid grid{{100.0F, 100.0F}, 10.0F};
    grid.Insert(EntityId{1}, {-50.0F, 500.0F});
    // Kept in an edge cell and found by a radius that reaches its real position.
    EXPECT_EQ(Query(grid, {0.0F, 99.0F}, 500.0F), (std::vector{EntityId{1}}));
    EXPECT_TRUE(Query(grid, {0.0F, 99.0F}, 1.0F).empty());
}

}  // namespace
}  // namespace sa::zone::sim

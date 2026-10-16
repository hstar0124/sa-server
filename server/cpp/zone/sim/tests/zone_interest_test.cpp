#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace sa::zone::sim {
namespace {

using common::CharacterId;
using common::EntityId;
using common::Vec2;

bool HasId(const std::vector<EntitySnapshot>& list, EntityId id) {
    return std::ranges::any_of(list, [id](const EntitySnapshot& s) { return s.id == id; });
}

TEST(ZoneInterest, FirstViewSpawnsNeighboursAndNeverTheObserver) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});  // 10 m: inside 20 m
    const EntityId far = test::Spawn(zone, 3, Vec2{95.0F, 95.0F});
    zone.Tick();

    const PlayerView view = zone.CollectView(a);
    EXPECT_TRUE(HasId(view.spawned, b));
    EXPECT_FALSE(HasId(view.spawned, a));
    EXPECT_FALSE(HasId(view.spawned, far));
    EXPECT_TRUE(view.updated.empty());
    EXPECT_TRUE(view.despawned.empty());
    EXPECT_EQ(view.spawned.front().template_id, test::kWarrior.value());
}

TEST(ZoneInterest, SecondViewReportsOnlyMovementAsUpdates) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});
    zone.Tick();
    (void)zone.CollectView(a);

    zone.Tick();  // nobody moved
    EXPECT_TRUE(zone.CollectView(a).IsEmpty());

    zone.Submit(MoveCommand{.player = b, .input_seq = 1, .direction = {0.0F, 1.0F}});
    zone.Tick();
    const PlayerView view = zone.CollectView(a);
    EXPECT_TRUE(view.spawned.empty());
    EXPECT_TRUE(HasId(view.updated, b));
    EXPECT_NEAR(view.updated.front().position.y, 51.0F, 1e-3F);
}

TEST(ZoneInterest, EnterAndLeaveWithHysteresis) {
    Zone zone = test::MakeZone();  // enter 20, leave 25, 1 m per tick
    const EntityId a = test::Spawn(zone, 1, Vec2{10.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{40.0F, 50.0F});  // 30 m away
    zone.Tick();
    EXPECT_TRUE(zone.CollectView(a).spawned.empty());

    // b walks toward a: visible once distance <= 20.
    zone.Submit(MoveCommand{.player = b, .input_seq = 1, .direction = {-1.0F, 0.0F}});
    test::TickN(zone, 9);  // 21 m
    EXPECT_TRUE(zone.CollectView(a).spawned.empty());
    test::TickN(zone, 1);  // 20 m
    EXPECT_TRUE(HasId(zone.CollectView(a).spawned, b));

    // b walks away: stays visible through the band (20, 25], leaves after 25.
    zone.Submit(MoveCommand{.player = b, .input_seq = 2, .direction = {1.0F, 0.0F}});
    test::TickN(zone, 5);  // 25 m
    EXPECT_TRUE(zone.CollectView(a).despawned.empty());
    test::TickN(zone, 1);  // 26 m
    const PlayerView gone = zone.CollectView(a);
    ASSERT_EQ(gone.despawned.size(), 1U);
    EXPECT_EQ(gone.despawned.front(), b);
}

TEST(ZoneInterest, DespawnedPlayerDisappearsFromObservers) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{55.0F, 50.0F});
    zone.Tick();
    (void)zone.CollectView(a);

    zone.DespawnPlayer(b);
    zone.Tick();
    const PlayerView view = zone.CollectView(a);
    EXPECT_EQ(view.despawned, std::vector{b});
    EXPECT_FALSE(zone.PositionOf(b).has_value());
}

TEST(ZoneInterest, ViewsAreSymmetricForTwoPlayersInRange) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{58.0F, 56.0F});
    zone.Tick();
    EXPECT_TRUE(HasId(zone.CollectView(a).spawned, b));
    EXPECT_TRUE(HasId(zone.CollectView(b).spawned, a));
}

TEST(ZoneInterest, CrowdOfPlayersSeesOnlyThoseInRange) {
    Zone zone = test::MakeZone();
    constexpr int kCount = 10;
    std::vector<EntityId> ids;
    ids.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        // A line along x, 7 m apart: 5, 12, 19, ... 68.
        ids.push_back(test::Spawn(zone, static_cast<std::uint64_t>(i) + 1,
                                  Vec2{5.0F + (7.0F * static_cast<float>(i)), 50.0F}));
    }
    zone.Tick();
    const PlayerView view = zone.CollectView(ids[0]);  // at x=5: sees x<=25 -> indices 1,2
    EXPECT_EQ(view.spawned.size(), 2U);
    // at x=40: 26, 33, 47, 54 are within 20 m; 19 and 61 are 21 m away and excluded.
    const PlayerView middle = zone.CollectView(ids[5]);
    EXPECT_EQ(middle.spawned.size(), 4U);
}

}  // namespace
}  // namespace sa::zone::sim

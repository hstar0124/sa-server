#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

// Views at snapshot cadence (every second tick here): a change on the tick between two views must
// still reach the next view.
TEST(ZoneInterest, AStepOnATickWithoutAViewReachesTheNextView) {
    Zone zone = test::MakeZone();  // 1 m per tick
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});
    zone.Tick();
    (void)zone.CollectView(a);

    zone.Submit(MoveCommand{.player = b, .input_seq = 1, .direction = {0.0F, 1.0F}});
    zone.Tick();  // b steps; no view this tick
    zone.Submit(MoveCommand{.player = b, .input_seq = 2, .direction = {}});
    zone.Tick();  // b stands still

    const PlayerView view = zone.CollectView(a);
    ASSERT_TRUE(HasId(view.updated, b));
    EXPECT_NEAR(view.updated.front().position.y, 51.0F, 1e-3F);
}

TEST(ZoneInterest, AStopIsReportedSoObserversStopExtrapolating) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});
    zone.Submit(MoveCommand{.player = b, .input_seq = 1, .direction = {0.0F, 1.0F}});
    test::TickN(zone, 2);
    (void)zone.CollectView(a);

    zone.Submit(MoveCommand{.player = b, .input_seq = 2, .direction = {}});
    test::TickN(zone, 2);  // the stop happens on the first of these ticks

    const PlayerView view = zone.CollectView(a);
    ASSERT_TRUE(HasId(view.updated, b));
    EXPECT_EQ(view.updated.front().velocity, Vec2{});
    test::TickN(zone, 2);
    EXPECT_TRUE(zone.CollectView(a).IsEmpty()) << "a body at rest is reported once";
}

TEST(ZoneInterest, DeadReckoningReportsAStraightWalkOnce) {
    Zone zone = test::MakeZone();  // 1 m per tick
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});
    zone.Tick();
    (void)zone.CollectView(a);

    zone.Submit(MoveCommand{.player = b, .input_seq = 1, .direction = {0.0F, 1.0F}});
    zone.Tick();
    const PlayerView start = zone.CollectView(a);
    ASSERT_TRUE(HasId(start.updated, b)) << "starting to walk changes the velocity";
    EXPECT_NEAR(start.updated.front().velocity.y, 10.0F, 1e-3F);

    for (int i = 0; i < 5; ++i) {
        zone.Tick();
        EXPECT_TRUE(zone.CollectView(a).IsEmpty()) << "observers extrapolate the walk, tick " << i;
    }
    EXPECT_NEAR(test::PosOf(zone, b).y, 56.0F, 1e-3F) << "the zone kept moving it";
}

TEST(ZoneInterest, SlidingAlongAWallReportsTheAchievedVelocity) {
    Zone zone = test::MakeZone(test::FieldWithWall());  // wall from x = 50
    const EntityId a = test::Spawn(zone, 1, Vec2{40.0F, 30.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{49.5F, 30.0F});
    zone.Tick();
    (void)zone.CollectView(a);

    zone.Submit(MoveCommand{.player = b, .input_seq = 1, .direction = {1.0F, 1.0F}});
    zone.Tick();
    const PlayerView view = zone.CollectView(a);
    ASSERT_TRUE(HasId(view.updated, b));
    // Only the y half of the diagonal survives the wall; extrapolating the intended diagonal
    // would carry b into the wall on every observer's screen.
    EXPECT_NEAR(view.updated.front().velocity.x, 0.0F, 1e-3F);
    EXPECT_NEAR(view.updated.front().velocity.y, 10.0F / std::sqrt(2.0F), 1e-3F);
    test::TickN(zone, 3);
    EXPECT_TRUE(zone.CollectView(a).IsEmpty()) << "the slide is straight: nothing new";
}

TEST(ZoneInterest, ManaAndAStraightProjectileAreNotNewsToObservers) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});
    zone.Tick();
    (void)zone.CollectView(a);

    zone.Submit(CastCommand{.player = b,
                            .request_id = common::RequestId{1},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, 1.0F}});
    zone.Tick();  // b pays 5 MP and the arrow leaves
    const PlayerView launch = zone.CollectView(a);
    ASSERT_EQ(launch.spawned.size(), 1U);
    EXPECT_EQ(launch.spawned.front().kind, EntityKind::kProjectile);
    EXPECT_NEAR(launch.spawned.front().velocity.y, 25.0F, 1e-3F) << "spawns carry velocity";
    EXPECT_TRUE(launch.updated.empty()) << "MP is b's own business";

    const std::int32_t mp_after_cast = test::VitalsOf(zone, b).mp;
    test::TickN(zone, 2);
    EXPECT_TRUE(zone.CollectView(a).IsEmpty()) << "regeneration and the flight are not news";
    EXPECT_GT(zone.CollectView(b).self_vitals.mp, mp_after_cast) << "b itself sees MP return";
}

TEST(ZoneInterest, InterestIsRecomputedOnViewTicksOnly) {
    ZoneConfig config = test::Config();
    config.ticks_per_view = 2;
    auto created =
        Zone::Create(config, test::OpenField(), test::AllRules(), test::Content(), Rng{1});
    ASSERT_TRUE(created.has_value());
    Zone& zone = *created;
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId b = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});

    zone.Tick();  // tick 1: not a view tick
    EXPECT_TRUE(zone.CollectView(a).spawned.empty());
    zone.Tick();  // tick 2
    EXPECT_TRUE(HasId(zone.CollectView(a).spawned, b));

    // b leaves after the recomputation: the view must not wait for the next one to drop it.
    zone.DespawnPlayer(b);
    EXPECT_EQ(zone.CollectView(a).despawned, std::vector{b});
}

TEST(ZoneInterest, AViewCadenceOfZeroIsRefused) {
    ZoneConfig config = test::Config();
    config.ticks_per_view = 0;
    EXPECT_FALSE(
        Zone::Create(config, test::OpenField(), test::AllRules(), test::Content(), Rng{1}));
}

TEST(ZoneInterest, DamageOnATickWithoutAViewReachesTheNextView) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    // The striker stands inside the slime's attack range, so the slime never moves: only the
    // damage can make it an update.
    const EntityId observer = test::Spawn(zone, 1, Vec2{60.0F, 70.0F});
    const EntityId striker = test::Spawn(zone, 2, Vec2{69.0F, 70.0F});
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();
    (void)zone.CollectView(observer);

    zone.Submit(CastCommand{.player = striker,
                            .request_id = common::RequestId{1},
                            .skill = test::kStrike,
                            .target = slime});
    zone.Tick();  // the hit lands; no view this tick
    zone.Tick();

    const PlayerView view = zone.CollectView(observer);
    const auto it = std::ranges::find(view.updated, slime, &EntitySnapshot::id);
    ASSERT_NE(it, view.updated.end());
    EXPECT_LT(it->hp, it->max_hp);
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

MovementRules Capped(std::uint32_t max_entities, std::uint32_t max_spawns) {
    MovementRules rules = test::Rules();
    rules.aoi_max_entities = max_entities;
    rules.aoi_max_spawns_per_view = max_spawns;
    return rules;
}

std::vector<EntityId> Ids(const std::vector<EntitySnapshot>& list) {
    std::vector<EntityId> ids;
    ids.reserve(list.size());
    for (const EntitySnapshot& s : list) {
        ids.push_back(s.id);
    }
    std::ranges::sort(ids);
    return ids;
}

TEST(ZoneInterest, AtTheViewCapTheNearestAreKept) {
    constexpr std::uint32_t kCap = 3;
    Zone zone = test::MakeZone(test::OpenField(), Capped(kCap, kCap));
    const EntityId observer = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    std::vector<EntityId> others;
    for (std::uint64_t i = 1; i <= 5; ++i) {  // 2, 4, 6, 8 and 10 m away, all in the enter radius
        others.push_back(
            test::Spawn(zone, i + 1, Vec2{50.0F + (2.0F * static_cast<float>(i)), 50.0F}));
    }
    zone.Tick();
    const PlayerView view = zone.CollectView(observer);
    EXPECT_EQ(Ids(view.spawned), (std::vector{others[0], others[1], others[2]}));
}

TEST(ZoneInterest, AVisibleEntityHoldsItsPlaceUntilANewcomerIsABandNearer) {
    // Enter 20 m, leave 25 m: a visible entity ranks 5 m closer than it stands.
    Zone zone = test::MakeZone(test::OpenField(), Capped(1, 1));
    const EntityId observer = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId visible = test::Spawn(zone, 2, Vec2{60.0F, 50.0F});  // 10 m, ranked as 5
    zone.Tick();
    ASSERT_EQ(Ids(zone.CollectView(observer).spawned), std::vector{visible});

    const EntityId nearer = test::Spawn(zone, 3, Vec2{50.0F, 58.0F});  // 8 m: not enough
    zone.Tick();
    EXPECT_TRUE(zone.CollectView(observer).IsEmpty()) << "no trading places at the edge";

    const EntityId closest = test::Spawn(zone, 4, Vec2{50.0F, 46.0F});  // 4 m: clearly nearer
    zone.Tick();
    const PlayerView view = zone.CollectView(observer);
    EXPECT_EQ(Ids(view.spawned), std::vector{closest});
    EXPECT_EQ(view.despawned, std::vector{visible});
    EXPECT_FALSE(HasId(view.spawned, nearer));
}

TEST(ZoneInterest, NewcomersArriveOverSeveralViewsNearestFirst) {
    Zone zone = test::MakeZone(test::OpenField(), Capped(64, 2));
    const EntityId observer = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    std::vector<EntityId> others;
    for (std::uint64_t i = 1; i <= 5; ++i) {
        others.push_back(
            test::Spawn(zone, i + 1, Vec2{50.0F, 50.0F + (2.0F * static_cast<float>(i))}));
    }
    zone.Tick();
    EXPECT_EQ(Ids(zone.CollectView(observer).spawned), (std::vector{others[0], others[1]}));
    zone.Tick();
    EXPECT_EQ(Ids(zone.CollectView(observer).spawned), (std::vector{others[2], others[3]}));

    zone.DespawnPlayer(others[4]);  // left before it was ever shown
    zone.Tick();
    const PlayerView last = zone.CollectView(observer);
    EXPECT_TRUE(last.spawned.empty());
    EXPECT_TRUE(last.despawned.empty()) << "never told about it, so nothing to take back";
}

}  // namespace
}  // namespace sa::zone::sim

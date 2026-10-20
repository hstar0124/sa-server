#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <array>

namespace sa::zone::sim {
namespace {

using common::EntityId;
using common::Vec2;

constexpr Vec2 kHome{70.0F, 70.0F};

TEST(ZoneMonsters, SpawnsAtHomeWithFullHealthAndAppearsInViews) {
    Zone zone = test::MakeZone(test::FieldWithSlime(kHome));
    const EntityId slime = test::OnlyMonster(zone);
    EXPECT_EQ(test::PosOf(zone, slime), kHome);
    EXPECT_EQ(test::VitalsOf(zone, slime).hp, 60);
    EXPECT_EQ(zone.KindOf(slime), EntityKind::kMonster);

    const EntityId p = test::Spawn(zone, 1, Vec2{60.0F, 70.0F});
    zone.Tick();
    const PlayerView view = zone.CollectView(p);
    ASSERT_EQ(view.spawned.size(), 1U);
    EXPECT_EQ(view.spawned.front().kind, EntityKind::kMonster);
    EXPECT_EQ(view.spawned.front().template_id, test::kSlime.value());
    EXPECT_EQ(view.spawned.front().max_hp, 60);
}

TEST(ZoneMonsters, IdleUntilAPlayerEntersAggroThenChasesAndAttacks) {
    Zone zone = test::MakeZone(test::FieldWithSlime(kHome));
    const EntityId slime = test::OnlyMonster(zone);
    const EntityId p = test::Spawn(zone, 1, Vec2{70.0F, 85.0F});  // 15 m: outside 8 m aggro
    test::TickN(zone, 10);
    EXPECT_EQ(test::PosOf(zone, slime), kHome) << "idle";

    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {0.0F, -1.0F}});
    test::TickN(zone, 8);  // player now ~7 m away: inside aggro
    zone.Submit(MoveCommand{.player = p, .input_seq = 2, .direction = {}});
    test::TickN(zone, 4);
    EXPECT_GT(test::PosOf(zone, slime).y, kHome.y) << "chasing upward";

    test::TickN(zone, 40);  // reaches the player and starts swinging (8 dmg / 1 s)
    EXPECT_LT(test::VitalsOf(zone, p).hp, 200);
    EXPECT_LE(common::Distance(test::PosOf(zone, slime), test::PosOf(zone, p)), 1.5F * 1.2F);
}

TEST(ZoneMonsters, LeashReturnsHomeAndRestoresHealth) {
    Zone zone = test::MakeZone(test::FieldWithSlime(kHome));
    const EntityId slime = test::OnlyMonster(zone);
    const EntityId p = test::Spawn(zone, 1, Vec2{70.0F, 75.0F});  // inside aggro
    test::TickN(zone, 3);
    zone.Submit(CastCommand{.player = p,
                            .request_id = common::RequestId{1},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, -1.0F}});
    zone.Tick();
    test::TickN(zone, 3);
    EXPECT_LT(test::VitalsOf(zone, slime).hp, 60) << "wounded";

    // Run away at 10 m/s: the slime (3 m/s) falls behind and crosses its 25 m leash.
    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {-1.0F, 0.0F}});
    test::TickN(zone, 60);
    zone.Submit(MoveCommand{.player = p, .input_seq = 2, .direction = {}});
    test::TickN(zone, 150);  // walk back home at 3 m/s
    EXPECT_LE(common::Distance(test::PosOf(zone, slime), kHome), 0.6F) << "returned";
    EXPECT_EQ(test::VitalsOf(zone, slime).hp, 60) << "healed on return";
}

// A wall straight between the slime and its target: the slime presses against it and, with no
// pathfinding, would stand there for as long as the target stays (the test stall timeout is 1 s).
TEST(ZoneMonsters, AChaseHeldByAWallTurnsIntoAReturnHome) {
    constexpr Vec2 kEastOfWall{55.0F, 30.0F};
    MapData map = test::FieldWithWall();  // wall x 50..52, y 0..60
    map.monster_spawns.push_back(
        MonsterSpawn{.template_id = test::kSlime, .position = kEastOfWall, .count = 1});
    Zone zone = test::MakeZone(std::move(map));
    const EntityId slime = test::OnlyMonster(zone);
    (void)test::Spawn(zone, 1, Vec2{48.0F, 30.0F});  // 7 m away across the wall: inside aggro

    bool reached_wall = false;
    bool came_back = false;
    for (int tick = 0; tick < 60 && !came_back; ++tick) {
        zone.Tick();
        const Vec2 at = test::PosOf(zone, slime);
        reached_wall = reached_wall || at.x < 53.0F;
        came_back = reached_wall && common::Distance(at, kEastOfWall) < 0.5F;
    }
    EXPECT_TRUE(reached_wall);
    EXPECT_TRUE(came_back) << "gave up the chase it could not finish";
}

// The slime follows its target around the end of a short wall, the target leaves, and the way
// home is straight through the wall: the stuck return puts the slime back at its home.
TEST(ZoneMonsters, AReturnHeldByAWallPutsTheMonsterBackHome) {
    constexpr Vec2 kSlimeHome{56.0F, 30.0F};
    MapData map = test::OpenField();
    map.obstacles.push_back(common::Aabb{.min = {50.0F, 20.0F}, .max = {52.0F, 40.0F}});
    map.monster_spawns.push_back(
        MonsterSpawn{.template_id = test::kSlime, .position = kSlimeHome, .count = 1});
    Zone zone = test::MakeZone(std::move(map));
    const EntityId slime = test::OnlyMonster(zone);
    const EntityId player = test::Spawn(zone, 1, Vec2{56.0F, 34.0F});  // inside aggro

    // The player steps toward the next waypoint only once the slime is within its 1.5 m attack
    // range, so the slime follows the whole way: north past the wall's end, west, then south.
    constexpr float kCaughtUpM = 1.5F;
    const std::array waypoints{Vec2{56.0F, 44.0F}, Vec2{46.0F, 44.0F}, Vec2{46.0F, 30.0F}};
    std::size_t next = 0;
    std::uint32_t seq = 0;
    for (int tick = 0; tick < 400 && next < waypoints.size(); ++tick) {
        const Vec2 at = test::PosOf(zone, player);
        const bool caught_up = common::Distance(test::PosOf(zone, slime), at) <= kCaughtUpM;
        if (common::Distance(at, waypoints.at(next)) < 0.5F) {
            ++next;
        }
        const Vec2 direction =
            caught_up && next < waypoints.size() ? waypoints.at(next) - at : Vec2{};
        zone.Submit(MoveCommand{.player = player, .input_seq = ++seq, .direction = direction});
        zone.Tick();
    }
    ASSERT_EQ(next, waypoints.size()) << "the player did not finish the walk";
    ASSERT_LT(test::PosOf(zone, slime).x, 50.0F) << "the slime is behind the wall";

    zone.DespawnPlayer(player);
    test::TickN(zone, 30);
    EXPECT_EQ(test::PosOf(zone, slime), kSlimeHome);
}

TEST(ZoneMonsters, RespawnsAfterItsTimerAsANewEntity) {
    Zone zone = test::MakeZone(test::FieldWithSlime(kHome));
    const EntityId first = test::OnlyMonster(zone);
    const EntityId p = test::Spawn(zone, 1, Vec2{68.0F, 70.0F});
    zone.Tick();
    for (std::uint64_t i = 1; i <= 3; ++i) {
        zone.Submit(CastCommand{.player = p,
                                .request_id = common::RequestId{i},
                                .skill = test::kStrike,
                                .target = first});
        zone.Tick();
        test::TickN(zone, 8);
    }
    EXPECT_TRUE(zone.LiveMonsters().empty());
    test::TickN(zone, 20);  // respawn 2 s at 10 Hz
    const EntityId second = test::OnlyMonster(zone);
    EXPECT_NE(second, first) << "a new lifecycle gets a new id";
    EXPECT_LE(common::Distance(test::PosOf(zone, second), kHome), 1.0F)
        << "back at home (and already aggroed)";
    EXPECT_EQ(test::VitalsOf(zone, second).hp, 60);
}

TEST(ZoneMonsters, PackMembersSpreadAroundTheirHome) {
    MapData map = test::OpenField();
    map.monster_spawns.push_back(
        MonsterSpawn{.template_id = test::kSlime, .position = kHome, .count = 3});
    const Zone zone = test::MakeZone(std::move(map));
    const auto monsters = zone.LiveMonsters();
    ASSERT_EQ(monsters.size(), 3U);
    for (const EntityId id : monsters) {
        const float d = common::Distance(test::PosOf(zone, id), kHome);
        EXPECT_NEAR(d, 1.5F, 1e-3F);
    }
    EXPECT_NE(test::PosOf(zone, monsters[0]), test::PosOf(zone, monsters[1]));
}

}  // namespace
}  // namespace sa::zone::sim

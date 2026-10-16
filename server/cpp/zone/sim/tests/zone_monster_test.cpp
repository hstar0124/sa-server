#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

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

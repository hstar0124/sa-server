#include "world_model.hpp"

#include <gtest/gtest.h>

namespace sa::loadgen {
namespace {

game::v1::Welcome WelcomeFor(std::uint64_t self) {
    game::v1::Welcome w;
    w.set_entity_id(self);
    w.mutable_map_size()->set_x(100.0F);
    w.mutable_map_size()->set_y(50.0F);
    return w;
}

game::v1::WorldSnapshot Snapshot(std::uint64_t tick, std::uint32_t acked) {
    game::v1::WorldSnapshot s;
    s.set_server_tick(tick);
    s.set_last_processed_input_seq(acked);
    s.mutable_self()->set_entity_id(1);
    s.mutable_self()->mutable_position()->set_x(10.0F);
    return s;
}

TEST(WorldModel, TracksSelfVisibleSetAndMaxVisible) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    EXPECT_EQ(model.map_size(), (common::Vec2{100.0F, 50.0F}));

    game::v1::WorldSnapshot s1 = Snapshot(10, 3);
    s1.add_spawned()->set_entity_id(2);
    s1.add_spawned()->set_entity_id(3);
    model.OnSnapshot(s1);
    EXPECT_EQ(model.visible_count(), 2U);
    EXPECT_FLOAT_EQ(model.self_position().x, 10.0F);
    EXPECT_EQ(model.last_acked_input(), 3U);

    game::v1::WorldSnapshot s2 = Snapshot(12, 4);
    s2.add_despawned(2);
    auto* moved = s2.add_updated();
    moved->set_entity_id(3);
    moved->mutable_position()->set_y(7.0F);
    model.OnSnapshot(s2);
    EXPECT_EQ(model.visible_count(), 1U);
    EXPECT_EQ(model.max_visible(), 2U);
    EXPECT_FLOAT_EQ(model.Visible().front().position.y, 7.0F);
    EXPECT_EQ(model.integrity().Total(), 0U);
}

TEST(WorldModel, ExposesKindsHealthAndCombatCounters) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    game::v1::WorldSnapshot s = Snapshot(1, 0);
    auto* slime = s.add_spawned();
    slime->set_entity_id(5);
    slime->set_kind(game::v1::ENTITY_KIND_MONSTER);
    slime->set_hp(60);
    slime->set_max_hp(60);
    s.mutable_self_vitals()->set_hp(150);
    s.mutable_self_vitals()->set_max_hp(200);
    model.OnSnapshot(s);
    ASSERT_EQ(model.Visible().size(), 1U);
    EXPECT_EQ(model.Visible().front().kind, bot::SeenKind::kMonster);
    EXPECT_EQ(model.Visible().front().hp, 60);
    EXPECT_EQ(model.self_hp(), 150);

    game::v1::CommandResult accepted;
    accepted.set_status(game::v1::COMMAND_STATUS_ACCEPTED);
    model.OnCommandResult(accepted);
    game::v1::CombatEvent hit;
    hit.set_attacker_id(1);
    hit.set_target_id(5);
    hit.set_killed(true);
    model.OnCombatEvent(hit);
    game::v1::CombatEvent died;
    died.set_attacker_id(5);
    died.set_target_id(1);
    died.set_killed(true);
    model.OnCombatEvent(died);
    EXPECT_EQ(model.combat().casts_accepted, 1U);
    EXPECT_EQ(model.combat().hits_dealt, 1U);
    EXPECT_EQ(model.combat().kills, 1U);
    EXPECT_EQ(model.combat().deaths, 1U);
}

TEST(WorldModel, CountsEveryKindOfContradiction) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    game::v1::WorldSnapshot s = Snapshot(5, 2);
    s.add_spawned()->set_entity_id(1);  // self in view
    s.add_spawned()->set_entity_id(9);
    s.add_spawned()->set_entity_id(9);   // duplicate spawn
    s.add_updated()->set_entity_id(77);  // never spawned
    s.add_despawned(55);                 // never spawned
    model.OnSnapshot(s);
    model.OnSnapshot(Snapshot(6, 1));  // ack regressed

    const WorldModel::Integrity& i = model.integrity();
    EXPECT_EQ(i.self_in_view, 1U);
    EXPECT_EQ(i.spawn_of_known, 1U);
    EXPECT_EQ(i.update_of_unknown, 1U);
    EXPECT_EQ(i.despawn_of_unknown, 1U);
    EXPECT_EQ(i.input_seq_regressed, 1U);
    EXPECT_EQ(i.Total(), 5U);
    EXPECT_EQ(model.visible_count(), 1U);
}

}  // namespace
}  // namespace sa::loadgen

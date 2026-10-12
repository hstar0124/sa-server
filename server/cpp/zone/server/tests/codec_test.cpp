#include "codec.hpp"

#include <gtest/gtest.h>

#include <variant>

namespace sa::zone::codec {
namespace {

TEST(Codec, MoveInputBecomesMoveCommand) {
    game::v1::MoveInput input;
    input.set_input_seq(7);
    input.mutable_direction()->set_x(0.0F);
    input.mutable_direction()->set_y(-2.0F);

    const sim::MoveCommand cmd = ToMoveCommand(common::EntityId{3}, input);
    EXPECT_EQ(cmd.player, common::EntityId{3});
    EXPECT_EQ(cmd.input_seq, 7U);
    EXPECT_EQ(cmd.direction, (common::Vec2{0.0F, -2.0F})) << "normalisation is the zone's job";
}

TEST(Codec, WorldSnapshotCarriesDeltasAndSelf) {
    sim::PlayerView view{.player = common::EntityId{1}, .last_processed_input_seq = 9};
    view.spawned.emplace_back(sim::EntitySnapshot{.id = common::EntityId{2},
                                                  .kind = sim::EntityKind::kPlayer,
                                                  .template_id = 4,
                                                  .position = {1.0F, 2.0F}});
    view.updated.emplace_back(sim::EntitySnapshot{
        .id = common::EntityId{3}, .position = {5.0F, 6.0F}, .velocity = {1.0F, 0.0F}});
    view.despawned.emplace_back(4);
    view.self = sim::EntitySnapshot{
        .id = common::EntityId{1}, .position = {9.0F, 9.0F}, .hp = 150, .max_hp = 200};
    view.self_vitals = sim::Vitals{.hp = 150, .max_hp = 200, .mp = 20, .max_mp = 50};

    const game::v1::WorldSnapshot snapshot = ToWorldSnapshot(view, common::TickIndex{100});

    EXPECT_EQ(snapshot.server_tick(), 100U);
    EXPECT_EQ(snapshot.last_processed_input_seq(), 9U);
    ASSERT_EQ(snapshot.spawned_size(), 1);
    EXPECT_EQ(snapshot.spawned(0).entity_id(), 2U);
    EXPECT_EQ(snapshot.spawned(0).kind(), game::v1::ENTITY_KIND_PLAYER);
    EXPECT_EQ(snapshot.spawned(0).template_id(), 4U);
    ASSERT_EQ(snapshot.updated_size(), 1);
    EXPECT_FLOAT_EQ(snapshot.updated(0).velocity().x(), 1.0F);
    ASSERT_EQ(snapshot.despawned_size(), 1);
    EXPECT_EQ(snapshot.despawned(0), 4U);
    EXPECT_FLOAT_EQ(snapshot.self().position().y(), 9.0F);
    EXPECT_EQ(snapshot.self().entity_id(), 1U);
    EXPECT_EQ(snapshot.self().hp(), 150U);
    EXPECT_EQ(snapshot.self_vitals().mp(), 20U);
    EXPECT_EQ(snapshot.self_vitals().max_mp(), 50U);
}

TEST(Codec, CastSkillBecomesACastCommandWithEitherTargetKind) {
    game::v1::CastSkill targeted;
    targeted.set_request_id(9);
    targeted.set_skill_id(1);
    targeted.set_target_entity_id(77);
    const sim::CastCommand a = ToCastCommand(common::EntityId{3}, targeted);
    EXPECT_EQ(a.request_id, common::RequestId{9});
    EXPECT_EQ(a.skill, sim::SkillId{1});
    EXPECT_EQ(std::get<common::EntityId>(a.target), common::EntityId{77});

    game::v1::CastSkill aimed;
    aimed.set_request_id(10);
    aimed.set_skill_id(3);
    aimed.mutable_direction()->set_x(0.0F);
    aimed.mutable_direction()->set_y(1.0F);
    const sim::CastCommand b = ToCastCommand(common::EntityId{3}, aimed);
    EXPECT_EQ(std::get<common::Vec2>(b.target), (common::Vec2{0.0F, 1.0F}));
}

TEST(Codec, EventsWrapIntoTheServerEnvelope) {
    const game::v1::ServerMessage rejected = ToServerMessage(sim::CommandResultEvent{
        .request_id = common::RequestId{5}, .reason = sim::RejectReason::kOnCooldown});
    ASSERT_TRUE(rejected.has_command_result());
    EXPECT_EQ(rejected.command_result().status(), game::v1::COMMAND_STATUS_REJECTED);
    EXPECT_EQ(rejected.command_result().reject_code(), game::v1::REJECT_CODE_ON_COOLDOWN);

    const game::v1::ServerMessage accepted =
        ToServerMessage(sim::CommandResultEvent{.request_id = common::RequestId{6}});
    EXPECT_EQ(accepted.command_result().status(), game::v1::COMMAND_STATUS_ACCEPTED);

    const game::v1::ServerMessage hit =
        ToServerMessage(sim::CombatEvent{.attacker = common::EntityId{1},
                                         .target = common::EntityId{2},
                                         .skill = sim::SkillId{1},
                                         .damage = 20,
                                         .target_hp = 0,
                                         .killed = true});
    ASSERT_TRUE(hit.has_combat_event());
    EXPECT_EQ(hit.combat_event().damage(), 20U);
    EXPECT_TRUE(hit.combat_event().killed());
}

TEST(Codec, EveryRejectReasonMaps) {
    for (int i = 1; i <= static_cast<int>(sim::RejectReason::kOutOfRange); ++i) {
        EXPECT_NE(ToProto(static_cast<sim::RejectReason>(i)), game::v1::REJECT_CODE_UNSPECIFIED)
            << i;
    }
}

TEST(Codec, EveryEntityKindMaps) {
    EXPECT_EQ(ToProto(sim::EntityKind::kMonster), game::v1::ENTITY_KIND_MONSTER);
    EXPECT_EQ(ToProto(sim::EntityKind::kDroppedItem), game::v1::ENTITY_KIND_DROPPED_ITEM);
    EXPECT_EQ(ToProto(sim::EntityKind::kProjectile), game::v1::ENTITY_KIND_PROJECTILE);
}

}  // namespace
}  // namespace sa::zone::codec

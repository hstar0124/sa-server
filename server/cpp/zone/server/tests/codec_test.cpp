#include "codec.hpp"

#include <gtest/gtest.h>

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
    const sim::EntitySnapshot self{.id = common::EntityId{1}, .position = {9.0F, 9.0F}};

    const game::v1::WorldSnapshot snapshot = ToWorldSnapshot(view, self, common::TickIndex{100});

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
}

TEST(Codec, EveryEntityKindMaps) {
    EXPECT_EQ(ToProto(sim::EntityKind::kMonster), game::v1::ENTITY_KIND_MONSTER);
    EXPECT_EQ(ToProto(sim::EntityKind::kDroppedItem), game::v1::ENTITY_KIND_DROPPED_ITEM);
    EXPECT_EQ(ToProto(sim::EntityKind::kProjectile), game::v1::ENTITY_KIND_PROJECTILE);
}

}  // namespace
}  // namespace sa::zone::codec

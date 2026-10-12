#include "codec.hpp"

#include <gtest/gtest.h>

#include <variant>
#include <vector>

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
    const game::v1::ServerMessage rejected = ToServerMessage(
        sim::CommandResultEvent::Decided(common::RequestId{5}, sim::RejectReason::kOnCooldown));
    ASSERT_TRUE(rejected.has_command_result());
    EXPECT_EQ(rejected.command_result().status(), game::v1::COMMAND_STATUS_REJECTED);
    EXPECT_EQ(rejected.command_result().reject_code(), game::v1::REJECT_CODE_ON_COOLDOWN);

    const game::v1::ServerMessage accepted =
        ToServerMessage(sim::CommandResultEvent{.request_id = common::RequestId{6}});
    EXPECT_EQ(accepted.command_result().status(), game::v1::COMMAND_STATUS_ACCEPTED);

    const game::v1::ServerMessage pending =
        ToServerMessage(sim::CommandResultEvent::Pending(common::RequestId{7}));
    EXPECT_EQ(pending.command_result().status(), game::v1::COMMAND_STATUS_PENDING);

    const game::v1::ServerMessage unknown =
        ToServerMessage(sim::CommandResultEvent::Unknown(common::RequestId{8}));
    EXPECT_EQ(unknown.command_result().status(), game::v1::COMMAND_STATUS_UNKNOWN);
    EXPECT_EQ(unknown.command_result().reject_code(), game::v1::REJECT_CODE_SERVICE_UNAVAILABLE);

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
    for (int i = 1; i <= static_cast<int>(sim::RejectReason::kServiceUnavailable); ++i) {
        EXPECT_NE(ToProto(static_cast<sim::RejectReason>(i)), game::v1::REJECT_CODE_UNSPECIFIED)
            << i;
    }
}

TEST(Codec, EveryEntityKindMaps) {
    EXPECT_EQ(ToProto(sim::EntityKind::kMonster), game::v1::ENTITY_KIND_MONSTER);
    EXPECT_EQ(ToProto(sim::EntityKind::kDroppedItem), game::v1::ENTITY_KIND_DROPPED_ITEM);
    EXPECT_EQ(ToProto(sim::EntityKind::kProjectile), game::v1::ENTITY_KIND_PROJECTILE);
}

TEST(Codec, PickupItemBecomesAPickupCommand) {
    game::v1::PickupItem pickup;
    pickup.set_request_id(9);
    pickup.set_item_entity_id(77);
    const sim::PickupCommand command = ToPickupCommand(common::EntityId{3}, pickup);
    EXPECT_EQ(command.player, common::EntityId{3});
    EXPECT_EQ(command.request_id, common::RequestId{9});
    EXPECT_EQ(command.item, common::EntityId{77});
}

TEST(Codec, KillRecordAndReservationCarryTheZonesAuthority) {
    const ZoneAuthority authority{.zone_instance = common::InstanceId::Generate(),
                                  .epoch = common::OwnerEpoch{4}};
    const sim::KillRecord kill{
        .killer = common::EntityId{1},
        .killer_character = common::CharacterId{42},
        .monster = sim::MonsterTemplateId{101},
        .position = {1.0F, 2.0F},
        .rolls = {sim::DropRoll{.item = sim::ItemTemplateId{2001}, .position = {1.5F, 2.0F}}}};
    const common::KillId kill_id = common::KillId::Generate();

    const backend::v1::RecordKillRequest request =
        ToRecordKillRequest(kill, kill_id, common::WorldId{1}, authority, "v1");
    EXPECT_EQ(request.kill_id(), kill_id.ToWire());
    EXPECT_EQ(request.killer_character_id(), 42U);
    EXPECT_EQ(request.authority().owner_epoch(), 4U);
    EXPECT_EQ(request.authority().zone_instance_id(), authority.zone_instance.ToWire());
    EXPECT_EQ(request.content_version(), "v1");
    ASSERT_EQ(request.drops_size(), 1);
    EXPECT_EQ(request.drops(0).item_template_id(), 2001U);
    EXPECT_FLOAT_EQ(request.drops(0).position().x(), 1.5F);

    const common::ItemInstanceId instance = common::ItemInstanceId::Generate();
    const sim::PickupReservation reservation{.player = common::EntityId{1},
                                             .character = common::CharacterId{42},
                                             .request_id = common::RequestId{9},
                                             .item = common::EntityId{5},
                                             .instance = instance};
    const backend::v1::PickupItemRequest pickup =
        ToPickupItemRequest(reservation, common::WorldId{1}, authority);
    EXPECT_EQ(pickup.item_instance_id(), instance.ToWire());
    EXPECT_EQ(pickup.character_id(), 42U);
    EXPECT_EQ(pickup.request_id().size(), common::ItemInstanceId::kBytes);
    const backend::v1::PickupItemRequest again =
        ToPickupItemRequest(reservation, common::WorldId{1}, authority);
    EXPECT_EQ(again.request_id(), pickup.request_id()) << "the same client request keeps its key";
}

TEST(Codec, BackendAnswersMapToSimulationTypes) {
    backend::v1::RecordKillResponse response;
    auto* good = response.add_items();
    good->set_item_instance_id(common::ItemInstanceId::Generate().ToWire());
    good->set_item_template_id(2001);
    good->mutable_position()->set_x(3.0F);
    response.add_items()->set_item_instance_id("short");
    const std::vector<sim::GroundItem> items = ToGroundItems(response);
    ASSERT_EQ(items.size(), 1U) << "a malformed id is dropped";
    EXPECT_EQ(items[0].item, sim::ItemTemplateId{2001});
    EXPECT_FLOAT_EQ(items[0].position.x, 3.0F);

    EXPECT_EQ(ToPickupOutcome(backend::v1::PICKUP_OUTCOME_ACQUIRED), sim::PickupOutcome::kAcquired);
    EXPECT_EQ(ToPickupOutcome(backend::v1::PICKUP_OUTCOME_ALREADY_TAKEN),
              sim::PickupOutcome::kAlreadyTaken);
    EXPECT_EQ(ToPickupOutcome(backend::v1::PICKUP_OUTCOME_INVENTORY_FULL),
              sim::PickupOutcome::kInventoryFull);
    EXPECT_EQ(ToPickupOutcome(backend::v1::PICKUP_OUTCOME_UNKNOWN_ITEM),
              sim::PickupOutcome::kUnknownItem);
    EXPECT_EQ(ToPickupOutcome(backend::v1::PICKUP_OUTCOME_STALE_AUTHORITY),
              sim::PickupOutcome::kUnavailable);

    backend::v1::GetCharacterAssetsResponse assets;
    assets.set_inventory_capacity(20);
    assets.set_inventory_version(3);
    auto* item = assets.add_items();
    item->set_item_instance_id(common::ItemInstanceId::Generate().ToWire());
    item->set_item_template_id(2101);
    item->set_slot_no(2);
    auto* gold = assets.add_wallet();
    gold->set_currency("gold");
    gold->set_balance(12);
    auto* other = assets.add_wallet();
    other->set_currency("gems");
    other->set_balance(99);
    const CharacterAssets mirrored = ToAssets(assets, "gold");
    EXPECT_EQ(mirrored.gold, 12U);
    EXPECT_EQ(mirrored.capacity, 20U);
    ASSERT_EQ(mirrored.items.size(), 1U);
    EXPECT_EQ(mirrored.items[0].slot, 2U);
    const game::v1::InventoryUpdate update = ToInventoryUpdate(mirrored);
    EXPECT_EQ(update.gold(), 12U);
    EXPECT_EQ(update.version(), 3U);
    ASSERT_EQ(update.items_size(), 1);
    EXPECT_EQ(update.items(0).item_template_id(), 2101U);
}

}  // namespace
}  // namespace sa::zone::codec

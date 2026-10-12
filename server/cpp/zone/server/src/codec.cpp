#include "codec.hpp"

#include "sa/common/bytes.hpp"

#include <array>
#include <optional>
#include <string>
#include <variant>

namespace sa::zone::codec {
namespace {

std::uint32_t Clamped(std::int32_t value) {
    return static_cast<std::uint32_t>(std::max(0, value));
}

void Fill(game::v1::Vec2& out, common::Vec2 v) {
    out.set_x(v.x);
    out.set_y(v.y);
}

void Fill(game::v1::EntityUpdate& out, const sim::EntitySnapshot& s) {
    out.set_entity_id(s.id.value());
    Fill(*out.mutable_position(), s.position);
    Fill(*out.mutable_velocity(), s.velocity);
    out.set_hp(Clamped(s.hp));
}

void Fill(game::v1::EntitySpawn& out, const sim::EntitySnapshot& s) {
    out.set_entity_id(s.id.value());
    out.set_kind(ToProto(s.kind));
    out.set_template_id(s.template_id);
    Fill(*out.mutable_position(), s.position);
    out.set_hp(Clamped(s.hp));
    out.set_max_hp(Clamped(s.max_hp));
}

}  // namespace

game::v1::EntityKind ToProto(sim::EntityKind kind) {
    switch (kind) {
        case sim::EntityKind::kPlayer:
            return game::v1::ENTITY_KIND_PLAYER;
        case sim::EntityKind::kMonster:
            return game::v1::ENTITY_KIND_MONSTER;
        case sim::EntityKind::kDroppedItem:
            return game::v1::ENTITY_KIND_DROPPED_ITEM;
        case sim::EntityKind::kProjectile:
            return game::v1::ENTITY_KIND_PROJECTILE;
    }
    return game::v1::ENTITY_KIND_UNSPECIFIED;
}

game::v1::RejectCode ToProto(sim::RejectReason reason) {
    switch (reason) {
        case sim::RejectReason::kNone:
            return game::v1::REJECT_CODE_UNSPECIFIED;
        case sim::RejectReason::kUnknownSkill:
            return game::v1::REJECT_CODE_UNKNOWN_SKILL;
        case sim::RejectReason::kNotLearned:
            return game::v1::REJECT_CODE_NOT_LEARNED;
        case sim::RejectReason::kDead:
            return game::v1::REJECT_CODE_DEAD;
        case sim::RejectReason::kAlreadyCasting:
            return game::v1::REJECT_CODE_ALREADY_CASTING;
        case sim::RejectReason::kNotEnoughMp:
            return game::v1::REJECT_CODE_NOT_ENOUGH_MP;
        case sim::RejectReason::kOnCooldown:
            return game::v1::REJECT_CODE_ON_COOLDOWN;
        case sim::RejectReason::kInvalidTarget:
            return game::v1::REJECT_CODE_INVALID_TARGET;
        case sim::RejectReason::kOutOfRange:
            return game::v1::REJECT_CODE_OUT_OF_RANGE;
        case sim::RejectReason::kItemNotFound:
            return game::v1::REJECT_CODE_ITEM_NOT_FOUND;
        case sim::RejectReason::kItemReserved:
            return game::v1::REJECT_CODE_ITEM_RESERVED;
        case sim::RejectReason::kInventoryFull:
            return game::v1::REJECT_CODE_INVENTORY_FULL;
        case sim::RejectReason::kServiceUnavailable:
            return game::v1::REJECT_CODE_SERVICE_UNAVAILABLE;
    }
    return game::v1::REJECT_CODE_UNSPECIFIED;
}

game::v1::CommandStatus ToProto(sim::CommandStatus status) {
    switch (status) {
        case sim::CommandStatus::kAccepted:
            return game::v1::COMMAND_STATUS_ACCEPTED;
        case sim::CommandStatus::kRejected:
            return game::v1::COMMAND_STATUS_REJECTED;
        case sim::CommandStatus::kPending:
            return game::v1::COMMAND_STATUS_PENDING;
        case sim::CommandStatus::kUnknown:
            return game::v1::COMMAND_STATUS_UNKNOWN;
    }
    return game::v1::COMMAND_STATUS_UNSPECIFIED;
}

sim::MoveCommand ToMoveCommand(common::EntityId player, const game::v1::MoveInput& input) {
    return sim::MoveCommand{.player = player,
                            .input_seq = input.input_seq(),
                            .direction = {input.direction().x(), input.direction().y()}};
}

sim::CastCommand ToCastCommand(common::EntityId player, const game::v1::CastSkill& cast) {
    sim::SkillTarget target = common::EntityId{};  // an absent target is rejected by the zone
    if (cast.has_target_entity_id()) {
        target = common::EntityId{cast.target_entity_id()};
    } else if (cast.has_direction()) {
        target = common::Vec2{cast.direction().x(), cast.direction().y()};
    }
    return sim::CastCommand{.player = player,
                            .request_id = common::RequestId{cast.request_id()},
                            .skill = sim::SkillId{cast.skill_id()},
                            .target = target};
}

sim::PickupCommand ToPickupCommand(common::EntityId player, const game::v1::PickupItem& pickup) {
    return sim::PickupCommand{.player = player,
                              .request_id = common::RequestId{pickup.request_id()},
                              .item = common::EntityId{pickup.item_entity_id()}};
}

game::v1::WorldSnapshot ToWorldSnapshot(const sim::PlayerView& view, common::TickIndex tick) {
    game::v1::WorldSnapshot out;
    out.set_server_tick(tick.value());
    out.set_last_processed_input_seq(view.last_processed_input_seq);
    for (const sim::EntitySnapshot& s : view.spawned) {
        Fill(*out.add_spawned(), s);
    }
    for (const sim::EntitySnapshot& s : view.updated) {
        Fill(*out.add_updated(), s);
    }
    for (const common::EntityId id : view.despawned) {
        out.add_despawned(id.value());
    }
    Fill(*out.mutable_self(), view.self);
    auto* vitals = out.mutable_self_vitals();
    vitals->set_hp(Clamped(view.self_vitals.hp));
    vitals->set_max_hp(Clamped(view.self_vitals.max_hp));
    vitals->set_mp(Clamped(view.self_vitals.mp));
    vitals->set_max_mp(Clamped(view.self_vitals.max_mp));
    return out;
}

game::v1::CommandResult ToCommandResult(const sim::CommandResultEvent& event) {
    game::v1::CommandResult out;
    out.set_request_id(event.request_id.value());
    out.set_status(ToProto(event.status));
    out.set_reject_code(ToProto(event.reason));
    return out;
}

game::v1::CombatEvent ToCombatEvent(const sim::CombatEvent& event) {
    game::v1::CombatEvent out;
    out.set_attacker_id(event.attacker.value());
    out.set_target_id(event.target.value());
    out.set_skill_id(event.skill.value());
    out.set_damage(Clamped(event.damage));
    out.set_target_hp(Clamped(event.target_hp));
    out.set_killed(event.killed);
    return out;
}

game::v1::ServerMessage ToServerMessage(const sim::ZoneEvent& event) {
    game::v1::ServerMessage out;
    std::visit(
        [&out](const auto& e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, sim::CommandResultEvent>) {
                *out.mutable_command_result() = ToCommandResult(e);
            } else {
                *out.mutable_combat_event() = ToCombatEvent(e);
            }
        },
        event);
    return out;
}

game::v1::Welcome ToWelcome(const WelcomeInfo& info) {
    game::v1::Welcome out;
    out.set_character_id(info.character.value());
    out.set_zone_id(info.zone.value());
    out.set_simulation_hz(info.simulation_hz);
    out.set_server_tick(info.tick.value());
    out.set_entity_id(info.entity.value());
    Fill(*out.mutable_map_size(), info.map_size);
    return out;
}

// ---- zone <-> backend (Economy) ---------------------------------------------------------------

namespace {

void Fill(backend::v1::ZoneAuthority& out, const ZoneAuthority& authority) {
    out.set_zone_instance_id(authority.zone_instance.ToWire());
    out.set_owner_epoch(authority.epoch.value());
}

}  // namespace

backend::v1::RecordKillRequest ToRecordKillRequest(const sim::KillRecord& kill,
                                                   common::KillId kill_id, common::WorldId world,
                                                   const ZoneAuthority& authority,
                                                   std::string_view content_version) {
    backend::v1::RecordKillRequest out;
    out.set_kill_id(kill_id.ToWire());
    out.set_world_id(world.value());
    out.set_killer_character_id(kill.killer_character.value());
    Fill(*out.mutable_authority(), authority);
    out.set_monster_template_id(kill.monster.value());
    out.set_content_version(std::string{content_version});
    for (const sim::DropRoll& roll : kill.rolls) {
        auto* drop = out.add_drops();
        drop->set_item_template_id(roll.item.value());
        drop->mutable_position()->set_x(roll.position.x);
        drop->mutable_position()->set_y(roll.position.y);
    }
    return out;
}

std::vector<sim::GroundItem> ToGroundItems(const backend::v1::RecordKillResponse& response) {
    std::vector<sim::GroundItem> items;
    items.reserve(static_cast<std::size_t>(response.items_size()));
    for (const backend::v1::DroppedItem& dropped : response.items()) {
        const auto instance = common::ItemInstanceId::FromString(dropped.item_instance_id());
        if (!instance) {
            continue;
        }
        items.push_back(
            sim::GroundItem{.instance = *instance,
                            .item = sim::ItemTemplateId{dropped.item_template_id()},
                            .position = {dropped.position().x(), dropped.position().y()}});
    }
    return items;
}

backend::v1::PickupItemRequest ToPickupItemRequest(const sim::PickupReservation& pickup,
                                                   common::WorldId world,
                                                   const ZoneAuthority& authority) {
    backend::v1::PickupItemRequest out;
    // The client's per-session request id is widened into the 16-byte key the Backend stores:
    // character id and request id together are unique for one character's lifetime.
    std::array<std::byte, common::ItemInstanceId::kBytes> key{};
    const std::uint64_t character = pickup.character.value();
    const std::uint64_t request = pickup.request_id.value();
    for (std::size_t i = 0; i < sizeof character; ++i) {
        key.at(i) = static_cast<std::byte>((character >> (8 * i)) & 0xFFU);
        key.at(i + sizeof character) = static_cast<std::byte>((request >> (8 * i)) & 0xFFU);
    }
    out.set_request_id(common::ToWireString(key));
    out.set_world_id(world.value());
    out.set_character_id(character);
    Fill(*out.mutable_authority(), authority);
    out.set_item_instance_id(pickup.instance.ToWire());
    return out;
}

sim::PickupOutcome ToPickupOutcome(backend::v1::PickupOutcome outcome) {
    switch (outcome) {
        case backend::v1::PICKUP_OUTCOME_ACQUIRED:
            return sim::PickupOutcome::kAcquired;
        case backend::v1::PICKUP_OUTCOME_ALREADY_TAKEN:
            return sim::PickupOutcome::kAlreadyTaken;
        case backend::v1::PICKUP_OUTCOME_INVENTORY_FULL:
            return sim::PickupOutcome::kInventoryFull;
        case backend::v1::PICKUP_OUTCOME_UNKNOWN_ITEM:
            return sim::PickupOutcome::kUnknownItem;
        case backend::v1::PICKUP_OUTCOME_STALE_AUTHORITY:
        case backend::v1::PICKUP_OUTCOME_REQUEST_MISMATCH:
        case backend::v1::PICKUP_OUTCOME_UNSPECIFIED:
        default:
            // Not a verdict on the item: the reservation is released and the client may retry.
            return sim::PickupOutcome::kUnavailable;
    }
}

std::optional<InventoryEntry> ToInventoryEntry(const backend::v1::InventoryItem& item) {
    const auto instance = common::ItemInstanceId::FromString(item.item_instance_id());
    if (!instance) {
        return std::nullopt;
    }
    return InventoryEntry{.instance = *instance,
                          .item = sim::ItemTemplateId{item.item_template_id()},
                          .slot = item.slot_no()};
}

CharacterAssets ToAssets(const backend::v1::GetCharacterAssetsResponse& response,
                         std::string_view primary_currency) {
    CharacterAssets assets{.capacity = response.inventory_capacity(),
                           .version = response.inventory_version()};
    for (const backend::v1::InventoryItem& item : response.items()) {
        if (const auto entry = ToInventoryEntry(item)) {
            assets.items.push_back(*entry);
        }
    }
    for (const backend::v1::WalletEntry& entry : response.wallet()) {
        if (entry.currency() == primary_currency) {
            assets.gold = entry.balance();
        }
    }
    return assets;
}

game::v1::InventoryUpdate ToInventoryUpdate(const CharacterAssets& assets) {
    game::v1::InventoryUpdate out;
    for (const InventoryEntry& entry : assets.items) {
        auto* item = out.add_items();
        item->set_item_instance_id(entry.instance.ToWire());
        item->set_item_template_id(entry.item.value());
        item->set_slot_no(entry.slot);
    }
    out.set_capacity(assets.capacity);
    out.set_version(assets.version);
    out.set_gold(assets.gold);
    return out;
}

}  // namespace sa::zone::codec

#include "codec.hpp"

#include "sa/common/bytes.hpp"
#include "sa/common/hmac.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
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

// 16 bytes naming one command of one character for its lifetime: the client's request id is
// unique only within one session, and the ownership epoch changes on every entry and transfer.
// A retry within the session reproduces the same digest; a new session cannot collide with an
// old one even when its counter restarts at the same number. `domain` keeps kinds apart.
common::HmacSha256Digest CommandDigest(std::string_view domain, common::CharacterId character,
                                       common::OwnerEpoch epoch, common::RequestId request) {
    std::array<std::byte, sizeof(std::uint64_t) * 3> material{};
    std::size_t offset = 0;
    for (const std::uint64_t value : {character.value(), epoch.value(), request.value()}) {
        for (std::size_t i = 0; i < sizeof value; ++i) {
            material.at(offset++) = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
        }
    }
    return common::HmacSha256(common::AsBytes(domain), material);
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
        case sim::RejectReason::kInvalidRequest:
            return game::v1::REJECT_CODE_INVALID_REQUEST;
        case sim::RejectReason::kItemNotFound:
            return game::v1::REJECT_CODE_ITEM_NOT_FOUND;
        case sim::RejectReason::kItemReserved:
            return game::v1::REJECT_CODE_ITEM_RESERVED;
        case sim::RejectReason::kInventoryFull:
            return game::v1::REJECT_CODE_INVENTORY_FULL;
        case sim::RejectReason::kServiceUnavailable:
            return game::v1::REJECT_CODE_SERVICE_UNAVAILABLE;
        case sim::RejectReason::kNotOwner:
            return game::v1::REJECT_CODE_NOT_OWNER;
        case sim::RejectReason::kUnknownPortal:
            return game::v1::REJECT_CODE_UNKNOWN_PORTAL;
        case sim::RejectReason::kOutOfPortalRange:
            return game::v1::REJECT_CODE_OUT_OF_PORTAL_RANGE;
        case sim::RejectReason::kTransferInProgress:
            return game::v1::REJECT_CODE_TRANSFER_IN_PROGRESS;
        case sim::RejectReason::kZoneUnavailable:
            return game::v1::REJECT_CODE_ZONE_UNAVAILABLE;
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

std::expected<sim::MoveCommand, InputError> ToMoveCommand(common::EntityId player,
                                                          const game::v1::MoveInput& input) {
    const common::Vec2 direction{input.direction().x(), input.direction().y()};
    if (!direction.IsFinite()) {
        return std::unexpected(InputError::kNonFiniteVector);
    }
    return sim::MoveCommand{
        .player = player, .input_seq = input.input_seq(), .direction = direction};
}

std::expected<sim::CastCommand, InputError> ToCastCommand(common::EntityId player,
                                                          const game::v1::CastSkill& cast) {
    sim::SkillTarget target = common::EntityId{};  // an absent target is rejected by the zone
    if (cast.has_target_entity_id()) {
        target = common::EntityId{cast.target_entity_id()};
    } else if (cast.has_direction()) {
        const common::Vec2 direction{cast.direction().x(), cast.direction().y()};
        if (!direction.IsFinite()) {
            return std::unexpected(InputError::kNonFiniteVector);
        }
        target = direction;
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

sim::ChangeZoneCommand ToChangeZoneCommand(common::EntityId player,
                                           const game::v1::ChangeZone& change) {
    return sim::ChangeZoneCommand{.player = player,
                                  .request_id = common::RequestId{change.request_id()},
                                  .portal = sim::PortalId{change.portal_id()}};
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
    for (const sim::Portal& portal : info.portals) {
        auto* p = out.add_portals();
        p->set_portal_id(portal.id.value());
        Fill(*p->mutable_position(), portal.position);
        p->set_radius_m(portal.radius_m);
        p->set_to_zone_id(portal.to_zone.value());
    }
    return out;
}

// ---- zone <-> backend ---------------------------------------------------------------------------

namespace {

void Fill(backend::v1::ZoneAuthority& out, const ZoneAuthority& authority) {
    out.set_zone_instance_id(authority.zone_instance.ToWire());
    out.set_owner_epoch(authority.epoch.value());
}

}  // namespace

// ---- WorldDirectory (placement) ----------------------------------------------------------------

backend::v1::SaveCheckpointRequest ToSaveCheckpointRequest(const CheckpointInfo& checkpoint,
                                                           const ZoneAuthority& authority) {
    backend::v1::SaveCheckpointRequest out;
    out.set_world_id(checkpoint.world.value());
    out.set_character_id(checkpoint.character.value());
    Fill(*out.mutable_authority(), authority);
    out.set_zone_id(checkpoint.zone.value());
    Fill(*out.mutable_position(), checkpoint.position);
    out.set_checkpoint_version(checkpoint.version);
    return out;
}

common::TransferId TransferIdFor(const sim::ZoneTransferRequest& transfer,
                                 common::OwnerEpoch epoch) {
    static constexpr std::string_view kDomain = "zone-transfer";
    const common::HmacSha256Digest digest =
        CommandDigest(kDomain, transfer.character, epoch, transfer.request_id);
    return common::TransferId::FromExact(std::span{digest}.first<common::TransferId::kBytes>());
}

backend::v1::TransferCharacterRequest ToTransferRequest(const sim::ZoneTransferRequest& transfer,
                                                        common::WorldId world,
                                                        const ZoneAuthority& authority) {
    backend::v1::TransferCharacterRequest out;
    out.set_transfer_id(TransferIdFor(transfer, authority.epoch).ToWire());
    out.set_world_id(world.value());
    out.set_character_id(transfer.character.value());
    Fill(*out.mutable_authority(), authority);
    out.set_to_zone_id(transfer.to_zone.value());
    Fill(*out.mutable_arrival_position(), transfer.arrival);
    return out;
}

sim::TransferOutcome ToTransferOutcome(backend::v1::TransferOutcome outcome) {
    switch (outcome) {
        case backend::v1::TRANSFER_OUTCOME_TRANSFERRED:
        case backend::v1::TRANSFER_OUTCOME_ALREADY_TRANSFERRED:
            return sim::TransferOutcome::kTransferred;
        case backend::v1::TRANSFER_OUTCOME_NO_ZONE_AVAILABLE:
        case backend::v1::TRANSFER_OUTCOME_SAME_ZONE:
            return sim::TransferOutcome::kZoneUnavailable;
        case backend::v1::TRANSFER_OUTCOME_STALE_AUTHORITY:
            return sim::TransferOutcome::kNotOwner;
        case backend::v1::TRANSFER_OUTCOME_UNSPECIFIED:
        default:
            return sim::TransferOutcome::kUnavailable;
    }
}

internal::v1::SessionTransfer ToSessionTransfer(
    const common::InstanceId& session_id, const backend::v1::TransferCharacterResponse& response,
    common::Vec2 arrival) {
    internal::v1::SessionTransfer out;
    out.set_gateway_session_id(session_id.ToWire());
    out.set_to_zone_id(response.to_zone().zone_id());
    out.set_to_zone_instance_id(response.to_zone().zone_instance_id());
    out.set_owner_epoch(response.owner_epoch());
    Fill(*out.mutable_arrival_position(), arrival);
    return out;
}

// ---- Economy ------------------------------------------------------------------------------------

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
    out.set_request_id(PickupRequestKey(pickup, authority.epoch));
    out.set_world_id(world.value());
    out.set_character_id(pickup.character.value());
    Fill(*out.mutable_authority(), authority);
    out.set_item_instance_id(pickup.instance.ToWire());
    return out;
}

std::string PickupRequestKey(const sim::PickupReservation& pickup, common::OwnerEpoch epoch) {
    static constexpr std::string_view kDomain = "pickup-request";
    const common::HmacSha256Digest digest =
        CommandDigest(kDomain, pickup.character, epoch, pickup.request_id);
    return common::RequestKey::FromExact(std::span{digest}.first<common::RequestKey::kBytes>())
        .ToWire();
}

common::KillId KillIdFor(common::InstanceId zone_instance, std::uint64_t sequence) {
    // Unique without asking the OS for randomness on every kill: the instance id is random per
    // process start and the sequence never repeats within it. No exception path in the tick.
    std::array<std::byte, sizeof sequence> counter{};
    for (std::size_t i = 0; i < sizeof sequence; ++i) {
        counter.at(i) = static_cast<std::byte>((sequence >> (8 * i)) & 0xFFU);
    }
    const common::HmacSha256Digest digest = common::HmacSha256(zone_instance.bytes(), counter);
    return common::KillId::FromExact(std::span{digest}.first<common::KillId::kBytes>());
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
            // Final: this zone no longer owns the character, so a retry here cannot succeed.
            return sim::PickupOutcome::kNotOwner;
        case backend::v1::PICKUP_OUTCOME_REQUEST_MISMATCH:
            return sim::PickupOutcome::kRequestMismatch;
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

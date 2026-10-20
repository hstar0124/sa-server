#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/zone/sim/entities.hpp"
#include "sa/zone/sim/events.hpp"
#include "sa/zone/sim/player_view.hpp"

#include <backend/economy.pb.h>
#include <game/client_messages.pb.h>
#include <game/server_messages.pb.h>

#include <cstdint>
#include <string_view>
#include <vector>

// Protobuf <-> simulation conversions (ADR-0008: the simulation never sees protobuf). Pure
// functions; tested without sockets.
namespace sa::zone::codec {

// ---- client <-> zone --------------------------------------------------------------------------

[[nodiscard]] sim::MoveCommand ToMoveCommand(common::EntityId player,
                                             const game::v1::MoveInput& input);
[[nodiscard]] sim::CastCommand ToCastCommand(common::EntityId player,
                                             const game::v1::CastSkill& cast);
[[nodiscard]] sim::PickupCommand ToPickupCommand(common::EntityId player,
                                                 const game::v1::PickupItem& pickup);

[[nodiscard]] game::v1::WorldSnapshot ToWorldSnapshot(const sim::PlayerView& view,
                                                      common::TickIndex tick);
[[nodiscard]] game::v1::CommandResult ToCommandResult(const sim::CommandResultEvent& event);
[[nodiscard]] game::v1::CombatEvent ToCombatEvent(const sim::CombatEvent& event);
// Wraps either zone event in the client envelope.
[[nodiscard]] game::v1::ServerMessage ToServerMessage(const sim::ZoneEvent& event);

struct WelcomeInfo {
    common::CharacterId character;
    common::ZoneId zone;
    std::uint32_t simulation_hz = 0;
    common::TickIndex tick;
    common::EntityId entity;
    common::Vec2 map_size;
};
[[nodiscard]] game::v1::Welcome ToWelcome(const WelcomeInfo& info);

[[nodiscard]] game::v1::EntityKind ToProto(sim::EntityKind kind);
[[nodiscard]] game::v1::RejectCode ToProto(sim::RejectReason reason);
[[nodiscard]] game::v1::CommandStatus ToProto(sim::CommandStatus status);

// ---- zone <-> backend (Economy) ---------------------------------------------------------------

// What the zone claims when it acts for a character: its process identity and the ownership
// generation the character was attached under (see README "소유권과 lease").
struct ZoneAuthority {
    common::InstanceId zone_instance;
    common::OwnerEpoch epoch;
};

[[nodiscard]] backend::v1::RecordKillRequest ToRecordKillRequest(const sim::KillRecord& kill,
                                                                 common::KillId kill_id,
                                                                 common::WorldId world,
                                                                 const ZoneAuthority& authority,
                                                                 std::string_view content_version);
// Items the Backend committed; entries with malformed ids are dropped.
[[nodiscard]] std::vector<sim::GroundItem> ToGroundItems(
    const backend::v1::RecordKillResponse& response);
[[nodiscard]] backend::v1::PickupItemRequest ToPickupItemRequest(
    const sim::PickupReservation& pickup, common::WorldId world, const ZoneAuthority& authority);
[[nodiscard]] sim::PickupOutcome ToPickupOutcome(backend::v1::PickupOutcome outcome);

// The inventory and wallet of one character as the host mirrors them for its client. The
// client gets the full state every time (see README "자산과 멱등성").
struct InventoryEntry {
    common::ItemInstanceId instance;
    sim::ItemTemplateId item;
    std::uint32_t slot = 0;
};
struct CharacterAssets {
    std::vector<InventoryEntry> items;
    std::uint32_t capacity = 0;
    std::uint32_t version = 0;
    std::uint64_t gold = 0;
};
[[nodiscard]] CharacterAssets ToAssets(const backend::v1::GetCharacterAssetsResponse& response,
                                       std::string_view primary_currency);
[[nodiscard]] std::optional<InventoryEntry> ToInventoryEntry(
    const backend::v1::InventoryItem& item);
[[nodiscard]] game::v1::InventoryUpdate ToInventoryUpdate(const CharacterAssets& assets);

}  // namespace sa::zone::codec

#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/zone/sim/entities.hpp"
#include "sa/zone/sim/events.hpp"
#include "sa/zone/sim/map_data.hpp"
#include "sa/zone/sim/player_view.hpp"

#include <backend/economy.pb.h>
#include <backend/world_directory.pb.h>
#include <game/client_messages.pb.h>
#include <game/server_messages.pb.h>
#include <internal/zone_link.pb.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Protobuf <-> simulation conversions (ADR-0008: the simulation never sees protobuf). Pure
// functions; tested without sockets.
namespace sa::zone::codec {

// ---- client <-> zone --------------------------------------------------------------------------

// A client input refused before it reaches the simulation.
enum class InputError : std::uint8_t {
    kNonFiniteVector,  // a NaN or infinite coordinate; no honest client sends one
};

[[nodiscard]] std::expected<sim::MoveCommand, InputError> ToMoveCommand(
    common::EntityId player, const game::v1::MoveInput& input);
[[nodiscard]] std::expected<sim::CastCommand, InputError> ToCastCommand(
    common::EntityId player, const game::v1::CastSkill& cast);
[[nodiscard]] sim::PickupCommand ToPickupCommand(common::EntityId player,
                                                 const game::v1::PickupItem& pickup);
[[nodiscard]] sim::ChangeZoneCommand ToChangeZoneCommand(common::EntityId player,
                                                         const game::v1::ChangeZone& change);

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
    std::span<const sim::Portal> portals;
};
[[nodiscard]] game::v1::Welcome ToWelcome(const WelcomeInfo& info);

[[nodiscard]] game::v1::EntityKind ToProto(sim::EntityKind kind);
[[nodiscard]] game::v1::RejectCode ToProto(sim::RejectReason reason);
[[nodiscard]] game::v1::CommandStatus ToProto(sim::CommandStatus status);

// ---- zone <-> backend ---------------------------------------------------------------------------

// What the zone claims when it acts for a character: its process identity and the ownership
// generation the character was attached under (see README "소유권과 lease").
struct ZoneAuthority {
    common::InstanceId zone_instance;
    common::OwnerEpoch epoch;
};

// ---- WorldDirectory (placement) ----------------------------------------------------------------

struct CheckpointInfo {
    common::WorldId world;
    common::CharacterId character;
    common::ZoneId zone;
    common::Vec2 position;
    std::uint64_t version = 0;
};
[[nodiscard]] backend::v1::SaveCheckpointRequest ToSaveCheckpointRequest(
    const CheckpointInfo& checkpoint, const ZoneAuthority& authority);
// The transfer id the Backend stores for a departure: a digest of the character, the ownership
// epoch of its session and the client's request id, so the client's retry of a departure whose
// answer was lost asks about the same transfer instead of starting another.
[[nodiscard]] common::TransferId TransferIdFor(const sim::ZoneTransferRequest& transfer,
                                               common::OwnerEpoch epoch);
[[nodiscard]] backend::v1::TransferCharacterRequest ToTransferRequest(
    const sim::ZoneTransferRequest& transfer, common::WorldId world,
    const ZoneAuthority& authority);
[[nodiscard]] sim::TransferOutcome ToTransferOutcome(backend::v1::TransferOutcome outcome);
// What the gateway needs to move the session: the destination the Backend named.
[[nodiscard]] internal::v1::SessionTransfer ToSessionTransfer(
    const common::InstanceId& session_id, const backend::v1::TransferCharacterResponse& response,
    common::Vec2 arrival);

// ---- Economy ------------------------------------------------------------------------------------

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
// Kill ids for one zone process: derived from its instance id and a per-process sequence.
[[nodiscard]] common::KillId KillIdFor(common::InstanceId zone_instance, std::uint64_t sequence);
// The 16-byte idempotency key the Backend stores for a pickup: a digest of the character, the
// ownership epoch of its session and the client's request id.
[[nodiscard]] std::string PickupRequestKey(const sim::PickupReservation& pickup,
                                           common::OwnerEpoch epoch);
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

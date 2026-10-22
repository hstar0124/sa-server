#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/content.hpp"
#include "sa/zone/sim/map_data.hpp"

#include <cstdint>
#include <variant>
#include <vector>

// Things the zone needs to tell the outside that are not part of the periodic snapshot: results
// and notifications for clients, and requests the host must settle with the Backend. The zone
// decides the recipients (see README "전투"); the host only serialises and routes.
namespace sa::zone::sim {

enum class RejectReason : std::uint8_t {
    kNone = 0,  // accepted
    kUnknownSkill,
    kNotLearned,
    kDead,
    kAlreadyCasting,
    kNotEnoughMp,
    kOnCooldown,
    kInvalidTarget,
    kOutOfRange,
    kInvalidRequest,  // no request id, or the Backend saw a different body under the same id
    // pickups (see README "자산과 멱등성")
    kItemNotFound,
    kItemReserved,
    kInventoryFull,
    kServiceUnavailable,  // the Backend did not answer; the same request_id may be retried
    kNotOwner,            // the Backend says another zone owns the character; the session ends
    // zone changes (see README "소유권과 lease")
    kUnknownPortal,
    kOutOfPortalRange,
    kTransferInProgress,
    kZoneUnavailable,  // no live destination zone right now
};

// Mirrors CommandStatus in contracts/game: a result is final (accepted/rejected), still waiting
// for the Backend (pending), or lost (unknown: the client retries with the same request_id).
enum class CommandStatus : std::uint8_t { kAccepted, kRejected, kPending, kUnknown };

struct CommandResultEvent {
    common::RequestId request_id;
    CommandStatus status = CommandStatus::kAccepted;
    RejectReason reason = RejectReason::kNone;

    [[nodiscard]] static constexpr CommandResultEvent Decided(common::RequestId id,
                                                              RejectReason reason) {
        return CommandResultEvent{.request_id = id,
                                  .status = reason == RejectReason::kNone
                                                ? CommandStatus::kAccepted
                                                : CommandStatus::kRejected,
                                  .reason = reason};
    }
    [[nodiscard]] static constexpr CommandResultEvent Pending(common::RequestId id) {
        return CommandResultEvent{.request_id = id, .status = CommandStatus::kPending};
    }
    [[nodiscard]] static constexpr CommandResultEvent Unknown(common::RequestId id) {
        return CommandResultEvent{.request_id = id,
                                  .status = CommandStatus::kUnknown,
                                  .reason = RejectReason::kServiceUnavailable};
    }

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return status == CommandStatus::kAccepted;
    }
};

struct CombatEvent {
    common::EntityId attacker;
    common::EntityId target;
    SkillId skill;  // none for a monster's basic attack
    std::int32_t damage = 0;
    std::int32_t target_hp = 0;  // after the hit
    bool killed = false;
};

using ZoneEvent = std::variant<CommandResultEvent, CombatEvent>;

struct DeliveredEvent {
    common::EntityId recipient;  // a player
    ZoneEvent event;
};

// ---- requests to the host ------------------------------------------------------------------
// The zone adjudicates and rolls; durable effects (receipts, items, gold) are the Backend's.
// The host carries these over gRPC and brings the answer back through Zone::SpawnGroundItems,
// Zone::ResolvePickup and Zone::CompleteTransfer/FailTransfer (see README "자산과 멱등성").

struct DropRoll {
    ItemTemplateId item;
    common::Vec2 position;
};

// A monster died to a player: record the kill, credit the table's gold and materialise the rolls.
struct KillRecord {
    common::EntityId killer;
    common::CharacterId killer_character;
    MonsterTemplateId monster;
    common::Vec2 position;
    std::vector<DropRoll> rolls;
};

// A player reached for a ground item; the item is reserved in the zone until the answer arrives.
struct PickupReservation {
    common::EntityId player;
    common::CharacterId character;
    common::RequestId request_id;
    common::EntityId item;
    common::ItemInstanceId instance;
};

// A player stands in a portal and asked to leave; the player is frozen until the answer arrives.
struct ZoneTransferRequest {
    common::EntityId player;
    common::CharacterId character;
    common::RequestId request_id;
    PortalId portal;
    common::ZoneId to_zone;
    common::Vec2 arrival;
};

using HostRequest = std::variant<KillRecord, PickupReservation, ZoneTransferRequest>;

}  // namespace sa::zone::sim

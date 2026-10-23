#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/content.hpp"
#include "sa/zone/sim/entities.hpp"
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
    // trades (ADR-0013)
    kTradeBusy,             // the player or the counterparty is already in a trade
    kTradeClosed,           // completed, cancelled or expired meanwhile
    kTradeVersionMismatch,  // the offer changed since the player looked
    kTradeItemUnavailable,  // not the offerer's, or locked in another trade
    kNotEnoughGold,
    kTradeCounterpartyLeft,  // the counterparty is no longer owned by this zone
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
// Zone::ResolvePickup, Zone::CompleteTransfer/FailTransfer and Zone::ResolveTrade (see README
// "자산과 멱등성").

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

// A trade step the zone validated; the Backend decides it and the host relays the outcome to both
// participants (ADR-0013).
struct TradeRequest {
    common::EntityId player;
    common::CharacterId character;
    common::RequestId request_id;
    TradeAction action = TradeAction::kOpen;
    common::CharacterId counterparty;           // kOpen
    common::TradeId trade;                      // kOffer, kAccept, kCancel
    std::vector<common::ItemInstanceId> items;  // kOffer
    std::uint64_t gold = 0;                     // kOffer
    std::uint32_t version = 0;                  // kAccept
};

using HostRequest = std::variant<KillRecord, PickupReservation, ZoneTransferRequest, TradeRequest>;

}  // namespace sa::zone::sim

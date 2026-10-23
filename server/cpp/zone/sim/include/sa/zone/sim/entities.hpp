#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/content.hpp"
#include "sa/zone/sim/map_data.hpp"

#include <cstdint>
#include <variant>
#include <vector>

namespace sa::zone::sim {

enum class EntityKind : std::uint8_t { kPlayer, kMonster, kDroppedItem, kProjectile };

struct Vitals {
    std::int32_t hp = 0;
    std::int32_t max_hp = 0;
    std::int32_t mp = 0;
    std::int32_t max_mp = 0;

    [[nodiscard]] constexpr bool IsAlive() const noexcept { return hp > 0; }
};

// What one observer learns about another entity in a snapshot.
struct EntitySnapshot {
    common::EntityId id;
    EntityKind kind = EntityKind::kPlayer;
    std::uint32_t template_id = 0;  // class / monster / item template from game-data
    common::Vec2 position;
    common::Vec2 velocity;
    std::int32_t hp = 0;
    std::int32_t max_hp = 0;
};

// A client's movement intent for one tick window (see README "네트워크와 입장").
struct MoveCommand {
    common::EntityId player;
    std::uint32_t input_seq = 0;
    common::Vec2 direction;  // any length; normalised by the zone, zero = stop
};

// Skill use (see README "전투"). request_id is the client's idempotency key.
using SkillTarget = std::variant<common::EntityId, common::Vec2>;

struct CastCommand {
    common::EntityId player;
    common::RequestId request_id;
    SkillId skill;
    SkillTarget target;
};

// Reach for an item lying in the zone (see README "자산과 멱등성").
struct PickupCommand {
    common::EntityId player;
    common::RequestId request_id;
    common::EntityId item;
};

// Leave through a portal (ADR-0010). The zone freezes the player and asks the host to move the
// character; the Backend's answer completes or cancels the departure.
struct ChangeZoneCommand {
    common::EntityId player;
    common::RequestId request_id;
    PortalId portal;
};

enum class TradeAction : std::uint8_t { kOpen, kOffer, kAccept, kCancel };

// One step of a trade (ADR-0013). The zone checks what only it knows (who stands where, who is
// alive, who is leaving); everything else is the Backend's answer, relayed as the result.
struct TradeCommand {
    common::EntityId player;
    common::RequestId request_id;
    TradeAction action = TradeAction::kOpen;
    common::EntityId target;                    // kOpen: the counterparty's entity
    common::TradeId trade;                      // kOffer, kAccept, kCancel
    std::vector<common::ItemInstanceId> items;  // kOffer: replaces the player's previous offer
    std::uint64_t gold = 0;                     // kOffer
    std::uint32_t version = 0;                  // kAccept: the version the client saw
};

// The Backend's verdict on a departure, as the host reports it back to the zone.
enum class TransferOutcome : std::uint8_t {
    kTransferred,      // ownership moved: the player leaves this zone
    kZoneUnavailable,  // no live destination: the player stays and may try later
    kNotOwner,         // this zone no longer owns the character; the session ends
    kUnavailable,      // no answer; the player stays and may retry with the same request id
};

// An item the Backend has committed to the ground of this zone; becomes a kDroppedItem entity.
struct GroundItem {
    common::ItemInstanceId instance;
    ItemTemplateId item;
    common::Vec2 position;
};

// The Backend's verdict on a reservation, as the host reports it back to the zone.
enum class PickupOutcome : std::uint8_t {
    kAcquired,
    kAlreadyTaken,     // somebody else committed first: the item leaves the ground
    kInventoryFull,    // the item stays; the reservation is released
    kUnknownItem,      // the Backend never had it: the item leaves the ground
    kUnavailable,      // no answer; the reservation is released and the client may retry
    kNotOwner,         // this zone no longer owns the character; the reservation is released
    kRequestMismatch,  // the Backend holds a different body under this key; a final refusal
};

}  // namespace sa::zone::sim

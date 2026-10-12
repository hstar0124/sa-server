#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/content.hpp"

#include <cstdint>
#include <variant>

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

// Reach for an item lying in the zone (design document 6.3 PickupItem, 8.2).
struct PickupCommand {
    common::EntityId player;
    common::RequestId request_id;
    common::EntityId item;
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
    kAlreadyTaken,   // somebody else committed first: the item leaves the ground
    kInventoryFull,  // the item stays; the reservation is released
    kUnknownItem,    // the Backend never had it: the item leaves the ground
    kUnavailable,    // no answer; the reservation is released and the client may retry
};

}  // namespace sa::zone::sim

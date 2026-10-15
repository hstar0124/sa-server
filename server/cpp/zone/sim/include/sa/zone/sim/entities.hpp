#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <cstdint>

namespace sa::zone::sim {

enum class EntityKind : std::uint8_t { kPlayer, kMonster, kDroppedItem, kProjectile };

// What one observer learns about another entity in a snapshot.
struct EntitySnapshot {
    common::EntityId id;
    EntityKind kind = EntityKind::kPlayer;
    std::uint32_t template_id = 0;  // class / monster / item template from game-data
    common::Vec2 position;
    common::Vec2 velocity;
};

// A client's movement intent for one tick window (design document 6.3 MoveInput).
struct MoveCommand {
    common::EntityId player;
    std::uint32_t input_seq = 0;
    common::Vec2 direction;  // any length; normalised by the zone, zero = stop
};

}  // namespace sa::zone::sim

#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <cstdint>
#include <span>

namespace sa::bot {

// What a bot knows about one nearby entity. Deliberately not the simulation's own type: the
// network bot will fill this from protobuf snapshots.
struct SeenEntity {
    common::EntityId id;
    common::Vec2 position;
};

// Everything a brain is allowed to look at when deciding its next intent.
struct Observation {
    common::Vec2 self_position;
    common::Vec2 map_size;
    std::span<const SeenEntity> visible;
    std::uint64_t tick = 0;
};

}  // namespace sa::bot

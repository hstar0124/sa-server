#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace sa::bot {

enum class SeenKind : std::uint8_t { kPlayer, kMonster, kProjectile, kOther };

// What a bot knows about one nearby entity. Deliberately not the simulation's own type: the
// network bot fills this from protobuf snapshots.
struct SeenEntity {
    common::EntityId id;
    SeenKind kind = SeenKind::kOther;
    common::Vec2 position;
    std::int32_t hp = 0;
};

// Everything a brain is allowed to look at when deciding its next intent.
struct Observation {
    common::Vec2 self_position;
    common::Vec2 map_size;
    std::int32_t self_hp = 0;
    std::int32_t self_max_hp = 0;
    std::span<const SeenEntity> visible;
    std::uint64_t tick = 0;
};

// A skill use: targeted at an entity, or aimed along a direction.
struct CastIntent {
    std::uint32_t skill_id = 0;
    std::variant<common::EntityId, common::Vec2> target;
};

// What the bot wants to do this tick. `direction` is a unit vector or zero (stand still).
struct Intent {
    common::Vec2 direction;
    std::optional<CastIntent> cast;
};

// A decision maker. Two live today: Wanderer and Fighter; the network client picks one per bot
// from its profile, so the abstraction has two real implementations.
class Brain {
public:
    virtual ~Brain() = default;
    [[nodiscard]] virtual Intent Decide(const Observation& observation) = 0;
};

}  // namespace sa::bot

#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace sa::bot {

enum class SeenKind : std::uint8_t { kPlayer, kMonster, kProjectile, kItem, kOther };

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
    common::TickIndex tick;
    std::uint32_t tick_hz = 0;  // rate of `tick`, i.e. the server's simulation rate; 0 until known
};

// A wall-time interval in ticks of the given rate, rounded up so a bot never acts early. Profiles
// give intervals in wall time; brains compare them against the server's ticks.
[[nodiscard]] inline std::uint64_t TicksCovering(std::chrono::milliseconds interval,
                                                 std::uint32_t tick_hz) {
    const std::chrono::nanoseconds tick_period =
        std::chrono::nanoseconds{std::chrono::seconds{1}} / tick_hz;
    return static_cast<std::uint64_t>(
        (std::chrono::nanoseconds{interval} + tick_period - std::chrono::nanoseconds{1}) /
        tick_period);
}

// A skill use: targeted at an entity, or aimed along a direction.
struct CastIntent {
    common::SkillId skill;
    std::variant<common::EntityId, common::Vec2> target;
};

// What the bot wants to do this tick. `direction` is a unit vector or zero (stand still).
struct Intent {
    common::Vec2 direction;
    std::optional<CastIntent> cast;
    std::optional<common::EntityId> pickup;  // a ground item to ask the zone for
};

// A decision maker. Three live today: Wanderer, Fighter and Looter; the network client picks one
// per bot from its profile, so the abstraction has real alternatives.
class Brain {
public:
    virtual ~Brain() = default;
    [[nodiscard]] virtual Intent Decide(const Observation& observation) = 0;
};

}  // namespace sa::bot

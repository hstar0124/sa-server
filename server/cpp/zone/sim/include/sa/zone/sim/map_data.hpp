#pragma once

#include "sa/common/geometry.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace sa::zone::sim {

// Static description of one zone's terrain. Produced by sa::zone::data from game-data/maps;
// the simulation only ever reads it.
struct MapData {
    common::Vec2 size;
    float cell_size = 0.0F;
    std::vector<common::Aabb> obstacles;
    std::vector<common::Vec2> spawn_points;

    [[nodiscard]] common::Aabb Bounds() const noexcept {
        return common::Aabb{.min = {}, .max = size};
    }

    // Structural sanity: positive extents, at least one spawn point, obstacles inside the map.
    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

// Tunables read from game-data/rules/movement.toml.
struct MovementRules {
    float max_speed_mps = 0.0F;
    float aoi_enter_radius_m = 0.0F;
    float aoi_leave_radius_m = 0.0F;  // >= enter radius; the gap is the hysteresis band
    std::uint32_t commands_per_tick_budget = 0;

    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

}  // namespace sa::zone::sim

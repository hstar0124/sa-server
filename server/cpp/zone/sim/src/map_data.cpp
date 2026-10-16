#include "sa/zone/sim/map_data.hpp"

#include <format>

namespace sa::zone::sim {

std::expected<void, std::string> MapData::Validate() const {
    if (size.x <= 0.0F || size.y <= 0.0F) {
        return std::unexpected("map size must be positive");
    }
    if (cell_size <= 0.0F) {
        return std::unexpected("cell_size must be positive");
    }
    if (spawn_points.empty()) {
        return std::unexpected("at least one spawn point is required");
    }
    const common::Aabb bounds = Bounds();
    for (std::size_t i = 0; i < obstacles.size(); ++i) {
        const common::Aabb& box = obstacles[i];
        if (!box.IsValid()) {
            return std::unexpected(std::format("obstacle {} has min >= max", i));
        }
        if (box.min.x < 0.0F || box.min.y < 0.0F || box.max.x > size.x || box.max.y > size.y) {
            return std::unexpected(std::format("obstacle {} lies outside the map", i));
        }
    }
    for (std::size_t i = 0; i < spawn_points.size(); ++i) {
        if (!bounds.Contains(spawn_points[i])) {
            return std::unexpected(std::format("spawn point {} lies outside the map", i));
        }
        for (const common::Aabb& box : obstacles) {
            if (box.Contains(spawn_points[i])) {
                return std::unexpected(std::format("spawn point {} lies inside an obstacle", i));
            }
        }
    }
    return {};
}

std::expected<void, std::string> MovementRules::Validate() const {
    if (max_speed_mps <= 0.0F) {
        return std::unexpected("movement.max_speed_mps must be positive");
    }
    if (aoi_enter_radius_m <= 0.0F) {
        return std::unexpected("aoi.enter_radius_m must be positive");
    }
    if (aoi_leave_radius_m < aoi_enter_radius_m) {
        return std::unexpected("aoi.leave_radius_m must be >= aoi.enter_radius_m");
    }
    if (commands_per_tick_budget == 0) {
        return std::unexpected("input.commands_per_tick_budget must be positive");
    }
    return {};
}

}  // namespace sa::zone::sim

#include "sa/zone/sim/map_data.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace sa::zone::sim {
std::vector<common::Vec2> MapData::HomesOf(const MonsterSpawn& spawn, float ring_radius_m) {
    std::vector<common::Vec2> homes;
    homes.reserve(spawn.count);
    for (std::uint32_t i = 0; i < spawn.count; ++i) {
        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(i) /
                            static_cast<float>(spawn.count);
        const common::Vec2 offset =
            spawn.count == 1 ? common::Vec2{}
                             : common::Vec2{std::cos(angle), std::sin(angle)} * ring_radius_m;
        homes.push_back(spawn.position + offset);
    }
    return homes;
}

bool MapData::IsWalkable(common::Vec2 point, float body_radius_m) const noexcept {
    // Written so that NaN fails: every comparison with it is false.
    const bool inside = point.x >= body_radius_m && point.y >= body_radius_m &&
                        point.x <= size.x - body_radius_m && point.y <= size.y - body_radius_m;
    if (!inside) {
        return false;
    }
    const common::Vec2 footprint{body_radius_m, body_radius_m};
    return std::ranges::none_of(obstacles, [&](const common::Aabb& box) {
        return common::Aabb{.min = box.min - footprint, .max = box.max + footprint}.Contains(point);
    });
}

namespace {

// Each portal: an id, a positive radius, a destination, a position inside the map, a unique id.
std::expected<void, std::string> ValidatePortals(const MapData& map) {
    const common::Aabb bounds = map.Bounds();
    for (std::size_t i = 0; i < map.portals.size(); ++i) {
        const Portal& portal = map.portals[i];
        // Written so that a NaN radius fails too.
        if (portal.id.IsNone() || !(portal.radius_m > 0.0F) || portal.to_zone.IsNone()) {
            return std::unexpected(
                std::format("portal {} needs an id, a positive radius and a destination", i));
        }
        if (!bounds.Contains(portal.position)) {
            return std::unexpected(std::format("portal {} lies outside the map", i));
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (map.portals[j].id == portal.id) {
                return std::unexpected(
                    std::format("portal id {} is used twice", portal.id.value()));
            }
        }
    }
    return {};
}

}  // namespace

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
    for (std::size_t i = 0; i < monster_spawns.size(); ++i) {
        const MonsterSpawn& spawn = monster_spawns[i];
        if (!bounds.Contains(spawn.position)) {
            return std::unexpected(std::format("monster spawn {} lies outside the map", i));
        }
        if (spawn.count == 0) {
            return std::unexpected(std::format("monster spawn {} has count 0", i));
        }
    }
    for (std::size_t i = 0; i < spawn_points.size(); ++i) {
        if (!bounds.Contains(spawn_points[i])) {
            return std::unexpected(std::format("spawn point {} lies outside the map", i));
        }
    }
    return ValidatePortals(*this);
}

std::expected<void, std::string> MapData::ValidateWalkable(float body_radius_m,
                                                           float pack_ring_radius_m) const {
    for (std::size_t i = 0; i < spawn_points.size(); ++i) {
        if (!IsWalkable(spawn_points[i], body_radius_m)) {
            return std::unexpected(std::format(
                "spawn point {} is not walkable: inside an obstacle or too close to one or to "
                "the map edge",
                i));
        }
    }
    for (std::size_t i = 0; i < monster_spawns.size(); ++i) {
        for (const common::Vec2 home : HomesOf(monster_spawns[i], pack_ring_radius_m)) {
            if (!IsWalkable(home, body_radius_m)) {
                return std::unexpected(
                    std::format("monster spawn {} puts a home at ({}, {}) that is not walkable", i,
                                home.x, home.y));
            }
        }
    }
    for (std::size_t i = 0; i < portals.size(); ++i) {
        if (!IsWalkable(portals[i].position, body_radius_m)) {
            return std::unexpected(std::format("portal {} stands where no body can reach it", i));
        }
    }
    return {};
}

const Portal* MapData::FindPortal(PortalId id) const noexcept {
    for (const Portal& portal : portals) {
        if (portal.id == id) {
            return &portal;
        }
    }
    return nullptr;
}

std::expected<void, std::string> MapData::ValidateAgainst(const ContentBundle& content) const {
    for (std::size_t i = 0; i < monster_spawns.size(); ++i) {
        if (!content.monsters.contains(monster_spawns[i].template_id)) {
            return std::unexpected(std::format("monster spawn {} references unknown template {}", i,
                                               monster_spawns[i].template_id.value()));
        }
    }
    return {};
}

std::expected<void, std::string> MovementRules::Validate() const {
    if (max_speed_mps <= 0.0F) {
        return std::unexpected("movement.max_speed_mps must be positive");
    }
    if (body_radius_m <= 0.0F) {
        return std::unexpected("movement.body_radius_m must be positive");
    }
    if (aoi_enter_radius_m <= 0.0F) {
        return std::unexpected("aoi.enter_radius_m must be positive");
    }
    if (aoi_leave_radius_m < aoi_enter_radius_m) {
        return std::unexpected("aoi.leave_radius_m must be >= aoi.enter_radius_m");
    }
    if (aoi_max_entities == 0 || aoi_max_spawns_per_view == 0) {
        return std::unexpected("aoi.max_entities and aoi.max_spawns_per_view must be positive");
    }
    if (commands_per_tick_budget == 0) {
        return std::unexpected("input.commands_per_tick_budget must be positive");
    }
    if (max_queued_per_player == 0) {
        return std::unexpected("input.max_queued_per_player must be positive");
    }
    return {};
}

}  // namespace sa::zone::sim

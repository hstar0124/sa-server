#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/content.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace sa::zone::sim {

// Static description of one zone's terrain. Produced by sa::zone::data from game-data/maps;
// the simulation only ever reads it.
struct MonsterSpawn {
    MonsterTemplateId template_id;
    common::Vec2 position;  // the monsters' home; they leash back to it
    std::uint32_t count = 0;
};

using common::PortalId;

// A way out of this zone (ADR-0010). A character inside `radius_m` of `position` may ask to leave;
// it appears at `arrival` in the map of `to_zone`.
struct Portal {
    PortalId id;
    common::Vec2 position;
    float radius_m = 0.0F;
    common::ZoneId to_zone;
    common::Vec2 arrival;
};

struct MapData {
    common::Vec2 size;
    float cell_size = 0.0F;
    std::vector<common::Aabb> obstacles;
    std::vector<common::Vec2> spawn_points;
    std::vector<MonsterSpawn> monster_spawns;
    std::vector<Portal> portals;

    [[nodiscard]] const Portal* FindPortal(PortalId id) const noexcept;

    [[nodiscard]] common::Aabb Bounds() const noexcept {
        return common::Aabb{.min = {}, .max = size};
    }

    // Where the monsters of one spawn live: a pack stands on a ring of `ring_radius_m` around the
    // spawn position so its members do not stack.
    [[nodiscard]] static std::vector<common::Vec2> HomesOf(const MonsterSpawn& spawn,
                                                           float ring_radius_m);

    // True when a body of radius `body_radius_m` centred at `point` stays inside the map and clear
    // of every obstacle: the collision model the zone moves bodies by. A NaN point is not walkable.
    [[nodiscard]] bool IsWalkable(common::Vec2 point, float body_radius_m) const noexcept;

    // Structural sanity: positive extents, at least one spawn point, obstacles and spawns inside
    // the map, portals with distinct ids, a positive radius and a destination. The zone checks
    // that no portal leads back into itself, since the map does not know which zone serves it.
    [[nodiscard]] std::expected<void, std::string> Validate() const;
    // Every player spawn point, monster home and portal centre is walkable for a body of this
    // radius, so nothing starts inside a wall or pressed against the map edge. A portal's arrival
    // lies in another map; the destination zone checks it when the character arrives.
    [[nodiscard]] std::expected<void, std::string> ValidateWalkable(float body_radius_m,
                                                                    float pack_ring_radius_m) const;
    // Cross-check against content: every monster spawn names a known template.
    [[nodiscard]] std::expected<void, std::string> ValidateAgainst(
        const ContentBundle& content) const;
};

// Tunables read from game-data/rules/movement.toml.
struct MovementRules {
    float max_speed_mps = 0.0F;
    float body_radius_m = 0.0F;  // half-width of a player's or monster's footprint against terrain
    float aoi_enter_radius_m = 0.0F;
    float aoi_leave_radius_m = 0.0F;  // >= enter radius; the gap is the hysteresis band
    // At most this many entities in one player's view: the nearest, with those already visible
    // ranked one hysteresis band closer so the edge of a crowd does not flicker. It bounds a
    // snapshot to N x cap entries instead of N x N when everyone stands in one place.
    std::uint32_t aoi_max_entities = 0;
    // At most this many newly visible entities in one snapshot, nearest first; the rest follow in
    // the next ones. A player arriving in a crowd would otherwise get hundreds of spawns at once.
    std::uint32_t aoi_max_spawns_per_view = 0;
    // Dead reckoning. Observers extrapolate a body from the state last reported for it; the body is
    // reported again only when its velocity moved more than publish_velocity_delta_mps from that
    // state or its position strayed more than publish_position_error_m from the extrapolation.
    // Zero reports every change.
    float publish_velocity_delta_mps = 0.0F;
    float publish_position_error_m = 0.0F;
    std::uint32_t commands_per_tick_budget = 0;  // casts and pickups applied per tick
    std::uint32_t max_queued_per_player = 0;     // casts and pickups one player may have waiting

    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

}  // namespace sa::zone::sim

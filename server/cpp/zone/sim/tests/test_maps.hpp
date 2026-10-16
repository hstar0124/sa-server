#pragma once

#include "sa/zone/sim/map_data.hpp"
#include "sa/zone/sim/zone.hpp"

#include <stdexcept>

// Fixtures shared by the simulation tests. Numbers here are test geometry, not game tuning.
namespace sa::zone::sim::test {

inline MapData OpenField() {
    return MapData{.size = {100.0F, 100.0F},
                   .cell_size = 10.0F,
                   .obstacles = {},
                   .spawn_points = {{10.0F, 10.0F}, {90.0F, 90.0F}}};
}

inline MapData FieldWithWall() {
    MapData map = OpenField();
    map.obstacles.push_back(common::Aabb{.min = {50.0F, 0.0F}, .max = {52.0F, 60.0F}});
    return map;
}

inline MovementRules Rules() {
    return MovementRules{.max_speed_mps = 10.0F,
                         .aoi_enter_radius_m = 20.0F,
                         .aoi_leave_radius_m = 25.0F,
                         .commands_per_tick_budget = 1024};
}

inline ZoneConfig Config(std::uint32_t hz = 10) {
    return ZoneConfig{.world_id = common::WorldId{1},
                      .zone_id = common::ZoneId{1},
                      .simulation_rate = TickRate{.hz = hz}};
}

inline Zone MakeZone(MapData map = OpenField(), MovementRules rules = Rules(),
                     std::uint64_t seed = 1) {
    return Zone{Config(), std::move(map), rules, Rng{seed}};
}

// Position of a player that the test knows to exist; a missing player is a test bug, not UB.
inline common::Vec2 PosOf(const Zone& zone, common::EntityId player) {
    const auto position = zone.PositionOf(player);
    if (!position.has_value()) {
        throw std::logic_error{"player is not in the zone"};
    }
    return *position;
}

inline EntitySnapshot SnapshotOf(const Zone& zone, common::EntityId entity) {
    const auto snapshot = zone.SnapshotOf(entity);
    if (!snapshot.has_value()) {
        throw std::logic_error{"entity is not in the zone"};
    }
    return *snapshot;
}

inline void TickN(Zone& zone, int n) {
    for (int i = 0; i < n; ++i) {
        zone.Tick();
    }
}

}  // namespace sa::zone::sim::test

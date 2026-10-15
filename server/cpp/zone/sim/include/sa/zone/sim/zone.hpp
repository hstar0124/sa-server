#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/duration.hpp"
#include "sa/zone/sim/entities.hpp"
#include "sa/zone/sim/map_data.hpp"
#include "sa/zone/sim/player_view.hpp"
#include "sa/zone/sim/rng.hpp"
#include "sa/zone/sim/spatial_grid.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sa::zone::sim {

struct ZoneConfig {
    common::WorldId world_id;
    common::ZoneId zone_id;
    TickRate simulation_rate{};
};

struct ZoneStats {
    std::uint64_t commands_applied = 0;
    std::uint64_t commands_deferred = 0;  // left queued because the per-tick budget ran out
    std::uint64_t stale_inputs_dropped = 0;
    std::size_t players = 0;
};

// One simulated region. All mutable state is owned here and advanced only by Tick(), which the
// hosting process calls from a single logical thread (design document 7.1). No I/O, no clocks,
// no randomness other than the injected Rng (ADR-0008).
class Zone {
public:
    Zone(ZoneConfig config, MapData map, MovementRules rules, Rng rng);

    // ---- membership -------------------------------------------------------------------------
    // Places the player at `position`, or at a spawn point chosen by the zone's RNG.
    common::EntityId SpawnPlayer(common::CharacterId character, std::uint32_t class_id,
                                 std::optional<common::Vec2> position = std::nullopt);
    void DespawnPlayer(common::EntityId player);

    // ---- input ------------------------------------------------------------------------------
    // Queues a command; applied by the next Tick() within the per-tick budget.
    void Submit(MoveCommand command);

    // ---- simulation -------------------------------------------------------------------------
    void Tick();

    // ---- output -----------------------------------------------------------------------------
    // Interest-set delta for one player since its previous view. Call at snapshot cadence.
    [[nodiscard]] PlayerView CollectView(common::EntityId player);

    // ---- inspection -------------------------------------------------------------------------
    [[nodiscard]] const ZoneConfig& config() const noexcept { return config_; }
    [[nodiscard]] const MapData& map() const noexcept { return map_; }
    [[nodiscard]] common::TickIndex current_tick() const noexcept { return tick_; }
    [[nodiscard]] const ZoneStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::optional<common::Vec2> PositionOf(common::EntityId player) const;
    [[nodiscard]] std::optional<EntitySnapshot> SnapshotOf(common::EntityId entity) const;
    [[nodiscard]] Rng& rng() noexcept { return rng_; }

private:
    struct Player {
        common::CharacterId character;
        std::uint32_t class_id = 0;
        common::Vec2 position;
        common::Vec2 velocity;
        common::Vec2 desired_direction;  // unit or zero
        std::uint32_t last_input_seq = 0;
        bool moved_this_tick = false;
        // Entities this player has been told about, kept sorted for cheap set differences.
        std::vector<common::EntityId> known;
        // Entities currently inside the hysteresis band (visible until they pass leave radius).
        std::vector<common::EntityId> interest;
    };

    void DrainCommands();
    void ApplyMove(const MoveCommand& command);
    void Integrate();
    [[nodiscard]] common::Vec2 ResolveCollisions(common::Vec2 from, common::Vec2 to) const;
    [[nodiscard]] bool Blocked(common::Vec2 point) const;
    void UpdateInterestSets();
    void RecomputeInterest(common::EntityId self, Player& player);
    [[nodiscard]] static EntitySnapshot MakeSnapshot(common::EntityId id, const Player& player);

    ZoneConfig config_;
    MapData map_;
    MovementRules rules_;
    Rng rng_;
    float dt_seconds_;
    SpatialGrid grid_;
    common::TickIndex tick_;
    common::EntityId next_entity_id_{1};
    std::unordered_map<common::EntityId, Player> players_;
    std::deque<MoveCommand> pending_;
    std::vector<common::EntityId> scratch_;  // reused query buffer
    ZoneStats stats_;
};

}  // namespace sa::zone::sim

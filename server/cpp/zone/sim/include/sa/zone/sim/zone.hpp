#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/content.hpp"
#include "sa/zone/sim/duration.hpp"
#include "sa/zone/sim/entities.hpp"
#include "sa/zone/sim/events.hpp"
#include "sa/zone/sim/map_data.hpp"
#include "sa/zone/sim/player_view.hpp"
#include "sa/zone/sim/rng.hpp"
#include "sa/zone/sim/spatial_grid.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
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
    std::uint64_t casts_accepted = 0;
    std::uint64_t casts_rejected = 0;
    std::uint64_t hits = 0;
    std::uint64_t kills = 0;
    std::size_t players = 0;
    std::size_t monsters = 0;
    std::size_t projectiles = 0;
};

// One simulated region. All mutable state is owned here and advanced only by Tick(), which the
// hosting process calls from a single logical thread (see README "Zone의 한 틱"). No I/O, no
// clocks, no randomness other than the injected Rng (ADR-0008).
//
// Implementation is split by concern across zone.cpp (membership, input, interest, views),
// zone_combat.cpp (skills, projectiles, damage, death) and zone_monsters.cpp (spawns, AI).
class Zone {
public:
    using Command = std::variant<MoveCommand, CastCommand>;

    // Fails when the map's spawns name monsters the content does not define.
    [[nodiscard]] static std::expected<Zone, std::string> Create(ZoneConfig config, MapData map,
                                                                 MovementRules rules,
                                                                 CombatRules combat,
                                                                 ContentBundle content, Rng rng);

    // ---- membership -------------------------------------------------------------------------
    // Places the player at `position`, or at a spawn point chosen by the zone's RNG. The class
    // must exist in the content bundle; an unknown class is a caller bug and yields nullopt.
    [[nodiscard]] std::optional<common::EntityId> SpawnPlayer(
        common::CharacterId character, ClassId class_id,
        std::optional<common::Vec2> position = std::nullopt);
    void DespawnPlayer(common::EntityId player);

    // ---- input ------------------------------------------------------------------------------
    // Queues a command; applied by the next Tick() within the per-tick budget.
    void Submit(Command command);

    // ---- simulation -------------------------------------------------------------------------
    void Tick();

    // ---- output -----------------------------------------------------------------------------
    // Interest-set delta for one player since its previous view. Call at snapshot cadence.
    [[nodiscard]] PlayerView CollectView(common::EntityId player);
    // Command results and combat notifications produced since the last drain, with recipients.
    [[nodiscard]] std::vector<DeliveredEvent> DrainEvents();

    // ---- inspection -------------------------------------------------------------------------
    [[nodiscard]] const ZoneConfig& config() const noexcept { return config_; }
    [[nodiscard]] const MapData& map() const noexcept { return map_; }
    [[nodiscard]] const ContentBundle& content() const noexcept { return content_; }
    [[nodiscard]] common::TickIndex current_tick() const noexcept { return tick_; }
    [[nodiscard]] const ZoneStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::optional<common::Vec2> PositionOf(common::EntityId entity) const;
    [[nodiscard]] std::optional<EntitySnapshot> SnapshotOf(common::EntityId entity) const;
    [[nodiscard]] std::optional<Vitals> VitalsOf(common::EntityId entity) const;
    [[nodiscard]] std::optional<EntityKind> KindOf(common::EntityId entity) const;
    [[nodiscard]] std::vector<common::EntityId> LiveMonsters() const;
    [[nodiscard]] Rng& rng() noexcept { return rng_; }

private:
    // ---- state ------------------------------------------------------------------------------
    struct PendingCast {
        common::RequestId request_id;
        const SkillDefinition* skill = nullptr;
        SkillTarget target;
        common::TickIndex resolve_at;
    };

    // Last N request ids with their outcome, so a retried request answers the same way
    // without re-applying (design document 8.1 step 1).
    class RecentRequests {
    public:
        [[nodiscard]] std::optional<RejectReason> Find(common::RequestId id) const;
        void Remember(common::RequestId id, RejectReason reason);

    private:
        static constexpr std::size_t kCapacity = 32;
        std::array<std::pair<common::RequestId, RejectReason>, kCapacity> ring_{};
        std::size_t next_ = 0;
        std::size_t size_ = 0;
    };

    struct Player {
        common::CharacterId character;
        const ClassDefinition* cls = nullptr;
        common::Vec2 position;
        common::Vec2 velocity;
        common::Vec2 desired_direction;  // unit or zero
        std::uint32_t last_input_seq = 0;
        bool moved_this_tick = false;
        bool vitals_changed_this_tick = false;
        Vitals vitals;
        float mp_regen_carry = 0.0F;  // fractional MP accumulated between whole points
        std::unordered_map<SkillId, common::TickIndex> cooldown_ready_at;
        std::optional<PendingCast> cast;
        std::optional<common::TickIndex> respawn_at;  // set while dead
        RecentRequests recent_requests;
        // Entities this player has been told about, kept sorted for cheap set differences.
        std::vector<common::EntityId> known;
        // Entities currently inside the hysteresis band (visible until they pass leave radius).
        std::vector<common::EntityId> interest;
    };

    enum class AiState : std::uint8_t { kIdle, kChase, kAttack, kReturn };

    struct Monster {
        const MonsterDefinition* def = nullptr;
        std::size_t slot = 0;
        common::Vec2 home;
        common::Vec2 position;
        common::Vec2 velocity;
        Vitals vitals;
        bool moved_this_tick = false;
        bool vitals_changed_this_tick = false;
        AiState state = AiState::kIdle;
        common::EntityId target;
        std::optional<common::Vec2> goal;
        common::TickIndex next_think;
        common::TickIndex attack_ready_at;
    };

    // A home position that keeps one monster alive or waits to respawn it.
    struct MonsterSlot {
        const MonsterDefinition* def = nullptr;
        common::Vec2 home;
        std::optional<common::EntityId> live;
        std::optional<common::TickIndex> respawn_at;
    };

    struct Projectile {
        common::EntityId owner;
        const SkillDefinition* skill = nullptr;
        common::Vec2 position;
        common::Vec2 velocity;
        float travelled_m = 0.0F;
    };

    Zone(ZoneConfig config, MapData map, MovementRules rules, CombatRules combat,
         ContentBundle content, Rng rng);

    // ---- zone.cpp ---------------------------------------------------------------------------
    common::EntityId AllocateEntityId();
    void DrainCommands();
    void ApplyMove(const MoveCommand& command);
    void IntegratePlayers();
    [[nodiscard]] common::Vec2 ResolveCollisions(common::Vec2 from, common::Vec2 to) const;
    [[nodiscard]] bool Blocked(common::Vec2 point) const;
    [[nodiscard]] common::Vec2 StepTowards(common::Vec2 from, common::Vec2 goal,
                                           float speed_mps) const;
    void UpdateInterestSets();
    void RecomputeInterest(common::EntityId self, Player& player);
    [[nodiscard]] bool ChangedThisTick(common::EntityId entity) const;
    [[nodiscard]] static bool SortedContainsInterest(const Player& player, common::EntityId entity);
    void ResetTickFlags();
    [[nodiscard]] common::TickIndex TicksFromNow(std::chrono::milliseconds duration) const;
    void Deliver(common::EntityId recipient, ZoneEvent event);
    // Everyone who currently sees `subject`, plus `subject` itself when it is a player.
    void DeliverToObservers(common::EntityId subject, const ZoneEvent& event);

    // ---- zone_combat.cpp --------------------------------------------------------------------
    void ApplyCast(const CastCommand& command);
    [[nodiscard]] RejectReason ValidateCast(const Player& player, const SkillDefinition& skill,
                                            const SkillTarget& target) const;
    void ResolveCasts();
    void Activate(common::EntityId caster, Player& player, const SkillDefinition& skill,
                  const SkillTarget& target);
    void UpdateProjectiles();
    void ApplyDamage(common::EntityId attacker, common::EntityId target, SkillId skill,
                     std::int32_t damage);
    void OnPlayerDeath(common::EntityId id, Player& player);
    void OnMonsterDeath(common::EntityId id);
    void RegenerateMp();
    void RespawnPlayers();
    // First monster whose body comes within `radius` of the segment from -> to (swept test, so a
    // fast projectile cannot tunnel through a small target between two ticks).
    [[nodiscard]] std::optional<common::EntityId> FirstMonsterAlong(common::Vec2 from,
                                                                    common::Vec2 to,
                                                                    float radius) const;

    // ---- zone_monsters.cpp ------------------------------------------------------------------
    void PopulateMonsterSlots();
    void SpawnMonster(std::size_t slot_index);
    void UpdateMonsters();
    void Think(common::EntityId id, Monster& monster);
    void MoveMonster(common::EntityId id, Monster& monster);
    void RespawnMonsters();
    [[nodiscard]] std::optional<common::EntityId> NearestLivePlayerWithin(common::Vec2 center,
                                                                          float radius) const;

    ZoneConfig config_;
    MapData map_;
    MovementRules rules_;
    CombatRules combat_;
    ContentBundle content_;
    Rng rng_;
    float dt_seconds_;
    SpatialGrid grid_;
    common::TickIndex tick_;
    common::EntityId next_entity_id_{1};
    std::unordered_map<common::EntityId, Player> players_;
    std::unordered_map<common::EntityId, Monster> monsters_;
    std::unordered_map<common::EntityId, Projectile> projectiles_;
    std::unordered_map<common::EntityId, EntityKind> kinds_;
    std::vector<MonsterSlot> monster_slots_;
    std::deque<Command> pending_;
    std::vector<DeliveredEvent> outbox_;
    std::vector<common::EntityId> scratch_;  // reused query buffer
    ZoneStats stats_;
};

}  // namespace sa::zone::sim

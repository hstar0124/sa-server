#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/strong_id.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

// Content definitions the simulation consumes (see README "전투").
// Loaded from game-data by sa::zone::data; the simulation only reads them. Durations stay in
// milliseconds here and are converted to ticks by the zone that knows its tick rate.
namespace sa::zone::sim {

using SkillId = common::StrongId<struct SkillIdTag, std::uint32_t>;
using ClassId = common::StrongId<struct ClassIdTag, std::uint32_t>;
using MonsterTemplateId = common::StrongId<struct MonsterTemplateIdTag, std::uint32_t>;
using ItemTemplateId = common::StrongId<struct ItemTemplateIdTag, std::uint32_t>;

enum class SkillKind : std::uint8_t {
    kTargeted,    // needs an entity target within range_m
    kProjectile,  // needs a direction; range_m is the travel limit
};

struct SkillDefinition {
    SkillId id;
    std::string name;
    SkillKind kind = SkillKind::kTargeted;
    std::int32_t cost_mp = 0;
    std::chrono::milliseconds cooldown{};
    std::chrono::milliseconds cast_time{};  // 0 = instant
    float range_m = 0.0F;
    std::int32_t damage = 0;
    float projectile_speed_mps = 0.0F;  // projectile only
    float projectile_radius_m = 0.0F;   // projectile only
};

struct ClassDefinition {
    ClassId id;
    std::string name;
    std::int32_t max_hp = 0;
    std::int32_t max_mp = 0;
    float mp_regen_per_sec = 0.0F;
    std::vector<SkillId> skills;
};

struct MonsterDefinition {
    MonsterTemplateId id;
    std::string name;
    std::int32_t max_hp = 0;
    float speed_mps = 0.0F;
    float aggro_radius_m = 0.0F;
    float leash_radius_m = 0.0F;
    float attack_range_m = 0.0F;
    std::int32_t attack_damage = 0;
    std::chrono::milliseconds attack_cooldown{};
    std::chrono::milliseconds respawn{};
};

struct ItemDefinition {
    ItemTemplateId id;
    std::string name;
    bool stackable = false;
};

struct DropEntry {
    ItemTemplateId item;
    float chance = 0.0F;  // 0..1, rolled independently per entry
};

struct DropTable {
    MonsterTemplateId monster;
    std::int64_t gold = 0;
    std::vector<DropEntry> entries;
};

struct ContentBundle {
    std::string version;
    std::unordered_map<SkillId, SkillDefinition> skills;
    std::unordered_map<ClassId, ClassDefinition> classes;
    std::unordered_map<MonsterTemplateId, MonsterDefinition> monsters;
    std::unordered_map<ItemTemplateId, ItemDefinition> items;
    std::unordered_map<MonsterTemplateId, DropTable> drops;

    // Internal consistency: positive vitals, known skill references, projectile fields present.
    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

struct CombatRules {
    std::uint32_t monster_think_hz = 0;
    std::chrono::milliseconds player_respawn{};
    // A chasing or returning monster that has not moved stall_distance_m within stall_timeout is
    // stuck: there is no pathfinding, so a wall between it and its goal would hold it forever. A
    // stuck chase turns into a return; a stuck return puts the monster back at its home.
    std::chrono::milliseconds monster_stall_timeout{};
    float monster_stall_distance_m = 0.0F;

    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

struct EconomyRules {
    std::uint32_t inventory_capacity = 0;
    float pickup_range_m = 0.0F;
    std::chrono::milliseconds ground_item_despawn{};
    float drop_scatter_radius_m = 0.0F;  // drops land on a ring of this radius around the corpse
    std::string primary_currency;

    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

}  // namespace sa::zone::sim

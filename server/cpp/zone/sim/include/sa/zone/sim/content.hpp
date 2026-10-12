#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/strong_id.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

// Content definitions the simulation consumes (design document 8.1: "스킬 정의는 데이터로").
// Loaded from game-data by sa::zone::data; the simulation only reads them. Durations stay in
// milliseconds here and are converted to ticks by the zone that knows its tick rate.
namespace sa::zone::sim {

using SkillId = common::StrongId<struct SkillIdTag, std::uint32_t>;
using ClassId = common::StrongId<struct ClassIdTag, std::uint32_t>;
using MonsterTemplateId = common::StrongId<struct MonsterTemplateIdTag, std::uint32_t>;

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

struct ContentBundle {
    std::string version;
    std::unordered_map<SkillId, SkillDefinition> skills;
    std::unordered_map<ClassId, ClassDefinition> classes;
    std::unordered_map<MonsterTemplateId, MonsterDefinition> monsters;

    // Internal consistency: positive vitals, known skill references, projectile fields present.
    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

struct CombatRules {
    std::uint32_t monster_think_hz = 0;
    std::chrono::milliseconds player_respawn{};

    [[nodiscard]] std::expected<void, std::string> Validate() const;
};

}  // namespace sa::zone::sim

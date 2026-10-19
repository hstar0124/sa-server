#include "sa/zone/sim/content.hpp"

#include <format>

namespace sa::zone::sim {
namespace {

std::expected<void, std::string> ValidateSkill(const SkillDefinition& skill) {
    if (skill.damage < 0 || skill.cost_mp < 0 || skill.range_m <= 0.0F) {
        return std::unexpected(std::format(
            "skill {} has a non-positive range or negative cost/damage", skill.id.value()));
    }
    if (skill.cooldown.count() < 0 || skill.cast_time.count() < 0) {
        return std::unexpected(std::format("skill {} has a negative duration", skill.id.value()));
    }
    if (skill.kind == SkillKind::kProjectile &&
        (skill.projectile_speed_mps <= 0.0F || skill.projectile_radius_m <= 0.0F)) {
        return std::unexpected(
            std::format("projectile skill {} needs speed and radius", skill.id.value()));
    }
    return {};
}

std::expected<void, std::string> ValidateClass(
    const ClassDefinition& cls, const std::unordered_map<SkillId, SkillDefinition>& skills) {
    if (cls.max_hp <= 0 || cls.max_mp < 0 || cls.mp_regen_per_sec < 0.0F) {
        return std::unexpected(std::format("class {} has invalid vitals", cls.id.value()));
    }
    for (const SkillId skill : cls.skills) {
        if (!skills.contains(skill)) {
            return std::unexpected(
                std::format("class {} references unknown skill {}", cls.id.value(), skill.value()));
        }
    }
    return {};
}

std::expected<void, std::string> ValidateMonster(const MonsterDefinition& monster) {
    if (monster.max_hp <= 0 || monster.speed_mps <= 0.0F || monster.attack_damage < 0) {
        return std::unexpected(
            std::format("monster {} has invalid vitals or speed", monster.id.value()));
    }
    if (monster.aggro_radius_m <= 0.0F || monster.leash_radius_m < monster.aggro_radius_m ||
        monster.attack_range_m <= 0.0F) {
        return std::unexpected(
            std::format("monster {} has inconsistent radii", monster.id.value()));
    }
    if (monster.attack_cooldown.count() <= 0 || monster.respawn.count() < 0) {
        return std::unexpected(std::format("monster {} has invalid durations", monster.id.value()));
    }
    return {};
}

}  // namespace

std::expected<void, std::string> ContentBundle::Validate() const {
    if (version.empty()) {
        return std::unexpected("content version is empty");
    }
    for (const auto& [id, skill] : skills) {
        if (auto result = ValidateSkill(skill); !result) {
            return result;
        }
    }
    for (const auto& [id, cls] : classes) {
        if (auto result = ValidateClass(cls, skills); !result) {
            return result;
        }
    }
    for (const auto& [id, monster] : monsters) {
        if (auto result = ValidateMonster(monster); !result) {
            return result;
        }
    }
    return {};
}

std::expected<void, std::string> CombatRules::Validate() const {
    if (monster_think_hz == 0) {
        return std::unexpected("combat.monster_think_hz must be positive");
    }
    if (player_respawn.count() < 0) {
        return std::unexpected("combat.player_respawn_ms must not be negative");
    }
    return {};
}

}  // namespace sa::zone::sim

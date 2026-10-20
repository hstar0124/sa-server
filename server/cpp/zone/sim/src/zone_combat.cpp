// Skills, projectiles, damage, death and respawn (see README "전투").
#include "sa/zone/sim/zone.hpp"

#include <algorithm>
#include <cmath>

namespace sa::zone::sim {
namespace {

// A monster slightly beyond nominal range still counts for a swing that started in range.
constexpr float kRangeTolerance = 1.1F;

}  // namespace

// ---- casting ------------------------------------------------------------------------------------

void Zone::ApplyCast(const CastCommand& command) {
    const auto it = players_.find(command.player);
    if (it == players_.end()) {
        return;
    }
    Player& player = it->second;
    ++stats_.commands_applied;

    // A request without an id cannot be told apart from the next one; it is never remembered.
    if (command.request_id == common::RequestId{}) {
        ++stats_.casts_rejected;
        Deliver(command.player,
                CommandResultEvent::Decided(command.request_id, RejectReason::kInvalidRequest));
        return;
    }

    // Step 1: a retried request answers exactly as before and changes nothing.
    if (const auto previous = player.recent_requests.Find(command.request_id)) {
        Deliver(command.player, *previous);
        return;
    }

    const auto skill_it = content_.skills.find(command.skill);
    const SkillDefinition* skill = skill_it == content_.skills.end() ? nullptr : &skill_it->second;
    const RejectReason reason = skill == nullptr ? RejectReason::kUnknownSkill
                                                 : ValidateCast(player, *skill, command.target);
    const auto result = CommandResultEvent::Decided(command.request_id, reason);
    player.recent_requests.Remember(result);
    Deliver(command.player, result);
    if (reason != RejectReason::kNone) {
        ++stats_.casts_rejected;
        return;
    }
    ++stats_.casts_accepted;

    // Cost is paid when the cast starts so a second cast cannot spend the same MP; the cooldown
    // starts at activation so an interrupted cast does not lock the skill.
    player.vitals.mp -= skill->cost_mp;
    player.changed_at = tick_;
    if (skill->cast_time.count() == 0) {
        Activate(command.player, player, *skill, command.target);
        return;
    }
    player.cast = PendingCast{.request_id = command.request_id,
                              .skill = skill,
                              .target = command.target,
                              .resolve_at = TicksFromNow(skill->cast_time)};
}

RejectReason Zone::ValidateCast(const Player& player, const SkillDefinition& skill,
                                const SkillTarget& target) const {
    if (!player.vitals.IsAlive()) {
        return RejectReason::kDead;
    }
    if (player.cast) {
        return RejectReason::kAlreadyCasting;
    }
    if (!std::ranges::contains(player.cls->skills, skill.id)) {
        return RejectReason::kNotLearned;
    }
    if (player.vitals.mp < skill.cost_mp) {
        return RejectReason::kNotEnoughMp;
    }
    if (const auto ready = player.cooldown_ready_at.find(skill.id);
        ready != player.cooldown_ready_at.end() && tick_ < ready->second) {
        return RejectReason::kOnCooldown;
    }
    switch (skill.kind) {
        case SkillKind::kTargeted: {
            const auto* target_id = std::get_if<common::EntityId>(&target);
            if (target_id == nullptr) {
                return RejectReason::kInvalidTarget;
            }
            const auto monster = monsters_.find(*target_id);
            if (monster == monsters_.end() || !monster->second.vitals.IsAlive()) {
                return RejectReason::kInvalidTarget;
            }
            if (!common::WithinDistance(player.position, monster->second.position, skill.range_m)) {
                return RejectReason::kOutOfRange;
            }
            return RejectReason::kNone;
        }
        case SkillKind::kProjectile: {
            // A direction without a usable unit vector (zero, NaN, infinite) would launch a
            // projectile that never travels and never ends.
            const auto* direction = std::get_if<common::Vec2>(&target);
            if (direction == nullptr || direction->Normalized().IsZero()) {
                return RejectReason::kInvalidTarget;
            }
            return RejectReason::kNone;
        }
    }
    return RejectReason::kInvalidTarget;
}

void Zone::ResolveCasts() {
    for (auto& [id, player] : players_) {
        if (!player.cast || tick_ < player.cast->resolve_at) {
            continue;
        }
        const PendingCast cast = *player.cast;
        player.cast.reset();
        Activate(id, player, *cast.skill, cast.target);
    }
}

// Activation re-checks what may have changed during the cast: the target may have
// died or walked away. A failed activation still consumed the MP, like a real cast that whiffs.
void Zone::Activate(common::EntityId caster, Player& player, const SkillDefinition& skill,
                    const SkillTarget& target) {
    player.cooldown_ready_at[skill.id] = TicksFromNow(skill.cooldown);
    switch (skill.kind) {
        case SkillKind::kTargeted: {
            const auto target_id = std::get<common::EntityId>(target);
            const auto monster = monsters_.find(target_id);
            if (monster == monsters_.end() ||
                !common::WithinDistance(player.position, monster->second.position,
                                        skill.range_m * kRangeTolerance)) {
                return;
            }
            ApplyDamage(caster, target_id, skill.id, skill.damage);
            return;
        }
        case SkillKind::kProjectile: {
            const common::Vec2 direction = std::get<common::Vec2>(target).Normalized();
            const common::EntityId id = AllocateEntityId();
            projectiles_.emplace(id,
                                 Projectile{.owner = caster,
                                            .skill = &skill,
                                            .position = player.position,
                                            .velocity = direction * skill.projectile_speed_mps});
            kinds_.emplace(id, EntityKind::kProjectile);
            grid_.Insert(id, player.position);
            return;
        }
    }
}

// ---- projectiles
// ------------------------------------------------------------------------------------

namespace {

// Squared distance from point p to the segment ab.
float DistanceSqToSegment(common::Vec2 p, common::Vec2 a, common::Vec2 b) {
    const common::Vec2 ab = b - a;
    const float len_sq = ab.LengthSq();
    if (len_sq == 0.0F) {
        return common::DistanceSq(p, a);
    }
    const common::Vec2 ap = p - a;
    const float t = std::clamp(((ap.x * ab.x) + (ap.y * ab.y)) / len_sq, 0.0F, 1.0F);
    return common::DistanceSq(p, a + ab * t);
}

}  // namespace

std::optional<common::EntityId> Zone::FirstMonsterAlong(common::Vec2 from, common::Vec2 to,
                                                        float radius) const {
    const common::Vec2 mid = (from + to) * 0.5F;
    std::vector<common::EntityId> candidates;
    grid_.QueryCandidates(mid, (common::Distance(from, to) * 0.5F) + radius, candidates);
    std::optional<common::EntityId> best;
    float best_sq = radius * radius;
    for (const common::EntityId id : candidates) {
        const auto monster = monsters_.find(id);
        if (monster == monsters_.end()) {
            continue;
        }
        const float d_sq = DistanceSqToSegment(monster->second.position, from, to);
        if (d_sq < best_sq || (d_sq == best_sq && best && id < *best)) {
            best = id;
            best_sq = d_sq;
        }
    }
    return best;
}

void Zone::UpdateProjectiles() {
    std::vector<common::EntityId> finished;
    for (auto& [id, projectile] : projectiles_) {
        const common::Vec2 from = projectile.position;
        const common::Vec2 next = from + projectile.velocity * dt_seconds_;
        projectile.travelled_m += common::Distance(from, next);
        projectile.position = next;
        grid_.Move(id, next);

        if (const auto hit = FirstMonsterAlong(from, next, projectile.skill->projectile_radius_m)) {
            ApplyDamage(projectile.owner, *hit, projectile.skill->id, projectile.skill->damage);
            finished.push_back(id);
        } else if (projectile.travelled_m >= projectile.skill->range_m || Blocked(next)) {
            finished.push_back(id);
        }
    }
    for (const common::EntityId id : finished) {
        projectiles_.erase(id);
        kinds_.erase(id);
        grid_.Remove(id);
    }
}

// ---- damage and death
// ---------------------------------------------------------------------------------

void Zone::ApplyDamage(common::EntityId attacker, common::EntityId target, SkillId skill,
                       std::int32_t damage) {
    Vitals* vitals = nullptr;
    common::TickIndex* changed_at = nullptr;
    if (const auto m = monsters_.find(target); m != monsters_.end()) {
        vitals = &m->second.vitals;
        changed_at = &m->second.changed_at;
    } else if (const auto p = players_.find(target); p != players_.end()) {
        vitals = &p->second.vitals;
        changed_at = &p->second.changed_at;
    }
    if (vitals == nullptr || !vitals->IsAlive()) {
        return;  // death is processed exactly once per lifecycle
    }
    vitals->hp = std::max(0, vitals->hp - std::max(0, damage));
    *changed_at = tick_;
    ++stats_.hits;
    const bool killed = !vitals->IsAlive();
    const CombatEvent event{.attacker = attacker,
                            .target = target,
                            .skill = skill,
                            .damage = damage,
                            .target_hp = vitals->hp,
                            .killed = killed};
    DeliverToObservers(target, event);
    if (const auto p = players_.find(attacker);
        p != players_.end() && !SortedContainsInterest(p->second, target) && attacker != target) {
        Deliver(attacker, event);  // the attacker always learns the outcome
    }
    if (!killed) {
        return;
    }
    ++stats_.kills;
    if (const auto m = monsters_.find(target); m != monsters_.end()) {
        OnMonsterDeath(target, attacker);
    } else if (const auto p = players_.find(target); p != players_.end()) {
        OnPlayerDeath(target, p->second);
    }
}

void Zone::OnPlayerDeath(common::EntityId /*id*/, Player& player) {
    player.cast.reset();
    SetVelocity(player.velocity, player.changed_at, {});
    player.desired_direction = {};
    player.respawn_at = TicksFromNow(rules_.combat.player_respawn);
}

void Zone::RespawnPlayers() {
    for (auto& [id, player] : players_) {
        if (!player.respawn_at || tick_ < *player.respawn_at) {
            continue;
        }
        player.respawn_at.reset();
        const auto index = rng_.Range<std::size_t>(0, map_.spawn_points.size() - 1);
        player.position = map_.spawn_points[index];
        player.vitals.hp = player.vitals.max_hp;
        player.vitals.mp = player.vitals.max_mp;
        player.changed_at = tick_;
        grid_.Move(id, player.position);
    }
}

void Zone::RegenerateMp() {
    for (auto& [id, player] : players_) {
        if (!player.vitals.IsAlive() || player.vitals.mp >= player.vitals.max_mp) {
            continue;
        }
        player.mp_regen_carry += player.cls->mp_regen_per_sec * dt_seconds_;
        const auto whole = static_cast<std::int32_t>(player.mp_regen_carry);
        if (whole > 0) {
            player.mp_regen_carry -= static_cast<float>(whole);
            player.vitals.mp = std::min(player.vitals.max_mp, player.vitals.mp + whole);
            player.changed_at = tick_;
        }
    }
}

}  // namespace sa::zone::sim

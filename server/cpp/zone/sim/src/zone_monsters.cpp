// Monster spawns and the Idle -> Chase -> Attack -> Return state machine (see README "전투").
#include "sa/zone/sim/zone.hpp"

#include <algorithm>

namespace sa::zone::sim {
namespace {

// Close enough to the home to count as returned.
constexpr float kArriveDistanceM = 0.5F;

}  // namespace

void Zone::PopulateMonsterSlots() {
    for (const MonsterSpawn& spawn : map_.monster_spawns) {
        const MonsterDefinition& def = content_.monsters.at(spawn.template_id);
        for (const common::Vec2 home : MapData::HomesOf(spawn)) {
            monster_slots_.push_back(MonsterSlot{.def = &def, .home = home});
        }
    }
    for (std::size_t i = 0; i < monster_slots_.size(); ++i) {
        SpawnMonster(i);
    }
}

void Zone::SpawnMonster(std::size_t slot_index) {
    MonsterSlot& slot = monster_slots_[slot_index];
    const common::EntityId id = AllocateEntityId();
    const std::uint32_t think_period =
        std::max(1U, config_.simulation_rate.hz / rules_.combat.monster_think_hz);
    Monster monster{.def = slot.def, .slot = slot_index, .home = slot.home, .position = slot.home};
    monster.vitals = Vitals{.hp = slot.def->max_hp, .max_hp = slot.def->max_hp};
    // Stagger decisions across slots so they do not all think on the same tick.
    monster.next_think = common::TickIndex{tick_.value() + 1 + (slot_index % think_period)};
    monster.attack_ready_at = tick_;
    monsters_.emplace(id, monster);
    kinds_.emplace(id, EntityKind::kMonster);
    grid_.Insert(id, slot.home);
    slot.live = id;
    slot.respawn_at.reset();
}

void Zone::OnMonsterDeath(common::EntityId id, common::EntityId killer) {
    const auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return;
    }
    // Only a player earns the kill; a killer that already left the zone earns nothing.
    if (const auto player = players_.find(killer); player != players_.end()) {
        RollDrops(killer, player->second, it->second);
    }
    MonsterSlot& slot = monster_slots_[it->second.slot];
    slot.live.reset();
    slot.respawn_at = TicksFromNow(it->second.def->respawn);
    monsters_.erase(it);
    kinds_.erase(id);
    grid_.Remove(id);
    // Observers see the corpse vanish through their next view; the kill event already told them.
}

void Zone::RespawnMonsters() {
    for (std::size_t i = 0; i < monster_slots_.size(); ++i) {
        const MonsterSlot& slot = monster_slots_[i];
        if (!slot.live && slot.respawn_at && tick_ >= *slot.respawn_at) {
            SpawnMonster(i);
        }
    }
}

// ---- AI -----------------------------------------------------------------------------------------

std::optional<common::EntityId> Zone::NearestLivePlayerWithin(common::Vec2 center,
                                                              float radius) const {
    std::vector<common::EntityId> candidates;
    grid_.QueryCandidates(center, radius, candidates);
    std::optional<common::EntityId> best;
    float best_sq = radius * radius;
    for (const common::EntityId id : candidates) {
        const auto player = players_.find(id);
        if (player == players_.end() || !player->second.vitals.IsAlive()) {
            continue;
        }
        const float d_sq = common::DistanceSq(center, player->second.position);
        if (d_sq < best_sq || (d_sq == best_sq && best && id < *best)) {
            best = id;
            best_sq = d_sq;
        }
    }
    return best;
}

void Zone::UpdateMonsters() {
    const std::uint32_t think_period =
        std::max(1U, config_.simulation_rate.hz / rules_.combat.monster_think_hz);
    IdsInOrder(monsters_, order_);
    for (const common::EntityId id : order_) {
        Monster& monster = monsters_.at(id);  // monsters attack players; none dies in this loop
        if (tick_ >= monster.next_think) {
            Think(id, monster);
            monster.next_think = common::TickIndex{tick_.value() + think_period};
        }
        MoveMonster(id, monster);
    }
}

void Zone::Think(common::EntityId id, Monster& monster) {
    const MonsterDefinition& def = *monster.def;
    const auto target = players_.find(monster.target);
    const bool target_alive = target != players_.end() && target->second.vitals.IsAlive();
    const bool in_attack_range =
        target_alive &&
        common::WithinDistance(monster.position, target->second.position, def.attack_range_m);
    const bool leashed =
        !common::WithinDistance(monster.home, monster.position, def.leash_radius_m);

    switch (monster.state) {
        case AiState::kIdle:
            if (const auto found = NearestLivePlayerWithin(monster.position, def.aggro_radius_m)) {
                monster.target = *found;
                monster.state = AiState::kChase;
                StartHeadway(monster);
            }
            return;
        case AiState::kChase:
            if (!target_alive || leashed || Stalled(monster)) {
                ReturnHome(monster);
                return;
            }
            if (in_attack_range) {
                monster.state = AiState::kAttack;
                monster.goal.reset();
                return;
            }
            monster.goal = target->second.position;
            return;
        case AiState::kAttack:
            if (!target_alive || leashed) {
                ReturnHome(monster);
                return;
            }
            if (!in_attack_range) {
                monster.state = AiState::kChase;
                monster.goal = target->second.position;
                StartHeadway(monster);
                return;
            }
            if (tick_ >= monster.attack_ready_at) {
                monster.attack_ready_at = TicksFromNow(def.attack_cooldown);
                ApplyDamage(id, monster.target, SkillId{}, def.attack_damage);
            }
            return;
        case AiState::kReturn:
            if (common::WithinDistance(monster.position, monster.home, kArriveDistanceM)) {
                SettleAtHome(monster);
            } else if (Stalled(monster)) {
                // Held by a wall on the way home: put it back rather than leave it out of place.
                monster.position = monster.home;
                monster.changed_at = tick_;
                grid_.Move(id, monster.home);
                SettleAtHome(monster);
            }
            return;
    }
}

void Zone::ReturnHome(Monster& monster) {
    monster.state = AiState::kReturn;
    monster.goal = monster.home;
    StartHeadway(monster);
}

void Zone::SettleAtHome(Monster& monster) {
    monster.state = AiState::kIdle;
    monster.goal.reset();
    monster.target = {};
    // Returning restores full health: a leash cannot be used to whittle a monster down.
    if (monster.vitals.hp != monster.vitals.max_hp) {
        monster.vitals.hp = monster.vitals.max_hp;
        monster.changed_at = tick_;
    }
}

void Zone::StartHeadway(Monster& monster) {
    monster.headway_from = monster.position;
    monster.stall_deadline = TicksFromNow(rules_.combat.monster_stall_timeout);
}

// Judged by the monster's own movement, not by the distance to its goal: a monster that keeps
// running after a faster player is chasing, not stuck, and the leash ends that chase.
bool Zone::Stalled(Monster& monster) {
    if (!common::WithinDistance(monster.position, monster.headway_from,
                                rules_.combat.monster_stall_distance_m)) {
        StartHeadway(monster);
        return false;
    }
    return tick_ >= monster.stall_deadline;
}

void Zone::MoveMonster(common::EntityId id, Monster& monster) {
    if (!monster.goal) {
        SetVelocity(monster.velocity, monster.changed_at, {});
        return;
    }
    const common::Vec2 next = StepTowards(monster.position, *monster.goal, monster.def->speed_mps);
    if (next == monster.position) {
        SetVelocity(monster.velocity, monster.changed_at, {});
        return;
    }
    SetVelocity(monster.velocity, monster.changed_at,
                (next - monster.position) * (1.0F / dt_seconds_));
    monster.position = next;
    monster.changed_at = tick_;
    grid_.Move(id, next);
}

}  // namespace sa::zone::sim

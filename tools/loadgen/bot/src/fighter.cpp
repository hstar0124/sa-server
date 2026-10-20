#include "sa/bot/fighter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace sa::bot {
namespace {
constexpr std::int64_t kMillisPerSecond =
    std::chrono::milliseconds{std::chrono::seconds{1}}.count();
}  // namespace

Fighter::Fighter(std::uint64_t seed, FighterParams params)
    : params_(params), roam_(seed, params.roam) {}

const SeenEntity* Fighter::NearestMonster(const Observation& observation, float within_m) {
    const SeenEntity* best = nullptr;
    float best_sq = within_m * within_m;
    for (const SeenEntity& seen : observation.visible) {
        if (seen.kind != SeenKind::kMonster || seen.hp <= 0) {
            continue;
        }
        const float d_sq = common::DistanceSq(observation.self_position, seen.position);
        if (d_sq < best_sq || (d_sq == best_sq && best != nullptr && seen.id < best->id)) {
            best = &seen;
            best_sq = d_sq;
        }
    }
    return best;
}

Intent Fighter::Decide(const Observation& observation) {
    if (observation.self_hp <= 0) {
        target_.reset();
        return Intent{};  // dead: wait for the respawn
    }
    const SeenEntity* monster = NearestMonster(observation, params_.engage_radius_m);
    if (monster == nullptr) {
        target_.reset();
        return roam_.Decide(observation);
    }
    target_ = monster->id;
    const common::Vec2 to_monster = monster->position - observation.self_position;
    if (to_monster.Length() > params_.attack_range_m) {
        return Intent{.direction = to_monster.Normalized()};
    }
    if (observation.tick_hz == 0 || observation.tick < next_cast_tick_) {
        return Intent{};  // in range, waiting for the next swing (or for the server's tick rate)
    }
    // The interval is wall time; convert it with the rate of the tick it is compared against.
    const auto interval_ticks = static_cast<std::uint64_t>(
        (params_.attack_interval.count() * observation.tick_hz + kMillisPerSecond - 1) /
        kMillisPerSecond);
    next_cast_tick_ = observation.tick + std::max<std::uint64_t>(1, interval_ticks);
    ++casts_;
    return Intent{.cast = CastIntent{.skill_id = params_.attack_skill_id, .target = monster->id}};
}

}  // namespace sa::bot

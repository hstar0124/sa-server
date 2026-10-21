#include "sa/bot/looter.hpp"

#include <algorithm>
#include <cstdint>

namespace sa::bot {

Looter::Looter(std::uint64_t seed, LooterParams params)
    : params_(params), fight_(seed, params.fight) {}

const SeenEntity* Looter::NearestItem(const Observation& observation, float within_m) {
    const SeenEntity* best = nullptr;
    float best_sq = within_m * within_m;
    for (const SeenEntity& seen : observation.visible) {
        if (seen.kind != SeenKind::kItem) {
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

Intent Looter::Decide(const Observation& observation) {
    const SeenEntity* item =
        observation.self_hp > 0 ? NearestItem(observation, params_.loot_radius_m) : nullptr;
    if (item == nullptr) {
        target_.reset();
        return fight_.Decide(observation);  // nothing to pick up (or dead): behave as a fighter
    }
    if (target_ != item->id) {
        target_ = item->id;
        next_pickup_tick_ = {};  // a new item is asked for at once
    }
    if (!common::WithinDistance(observation.self_position, item->position,
                                params_.pickup_range_m)) {
        return Intent{.direction = (item->position - observation.self_position).Normalized()};
    }
    if (observation.tick_hz == 0 || observation.tick < next_pickup_tick_) {
        // Asked already (the item vanishes once the zone hands it over), or the server's tick
        // rate is not known yet.
        return Intent{};
    }
    next_pickup_tick_ = common::TickIndex{
        observation.tick.value() +
        std::max<std::uint64_t>(1, TicksCovering(params_.pickup_retry, observation.tick_hz))};
    ++pickups_;
    return Intent{.pickup = item->id};
}

}  // namespace sa::bot

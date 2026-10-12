#include "sa/bot/wanderer.hpp"

#include <algorithm>

namespace sa::bot {

Wanderer::Wanderer(std::uint64_t seed, WandererParams params) : rng_(seed), params_(params) {}

common::Vec2 Wanderer::PickTarget(common::Vec2 map_size) {
    const float m = params_.margin_m;
    std::uniform_real_distribution<float> x{m, std::max(m, map_size.x - m)};
    std::uniform_real_distribution<float> y{m, std::max(m, map_size.y - m)};
    target_ = common::Vec2{x(rng_), y(rng_)};
    ticks_on_target_ = 0;
    return *target_;
}

common::Vec2 Wanderer::Decide(const Observation& observation) {
    if (idle_remaining_ > 0) {
        --idle_remaining_;
        return {};
    }
    const common::Vec2 target = target_.has_value() ? *target_ : PickTarget(observation.map_size);
    const common::Vec2 to_target = target - observation.self_position;
    const bool arrived =
        to_target.LengthSq() <= params_.arrive_distance_m * params_.arrive_distance_m;
    const bool stuck = ++ticks_on_target_ > params_.retarget_after_ticks;
    if (arrived || stuck) {
        if (arrived) {
            ++targets_reached_;
            if (params_.idle_ticks_max > 0) {
                std::uniform_int_distribution<std::uint32_t> idle{params_.idle_ticks_min,
                                                                  params_.idle_ticks_max};
                idle_remaining_ = idle(rng_);
            }
        }
        target_.reset();
        return {};
    }
    return to_target.Normalized();
}

}  // namespace sa::bot

#pragma once

#include "sa/bot/observation.hpp"
#include "sa/common/geometry.hpp"

#include <cstdint>
#include <optional>
#include <random>

namespace sa::bot {

struct WandererParams {
    float arrive_distance_m = 1.0F;            // target counts as reached inside this radius
    std::uint32_t retarget_after_ticks = 200;  // give up on a target that collisions keep blocking
    std::uint32_t idle_ticks_min = 0;  // pause at each target for a random span in [min, max]
    std::uint32_t idle_ticks_max = 0;
    float margin_m = 2.0F;  // keep targets away from the map edge
};

// Picks random points on the map and walks to them. All randomness comes from the seed given at
// construction, so a run is reproducible from its seed (ADR-0006).
class Wanderer final : public Brain {
public:
    Wanderer(std::uint64_t seed, WandererParams params);

    [[nodiscard]] Intent Decide(const Observation& observation) override;

    [[nodiscard]] std::optional<common::Vec2> target() const noexcept { return target_; }
    [[nodiscard]] std::uint32_t targets_reached() const noexcept { return targets_reached_; }

private:
    common::Vec2 PickTarget(common::Vec2 map_size);

    std::mt19937_64 rng_;
    WandererParams params_;
    std::optional<common::Vec2> target_;
    std::uint32_t ticks_on_target_ = 0;
    std::uint32_t idle_remaining_ = 0;
    std::uint32_t targets_reached_ = 0;
};

}  // namespace sa::bot

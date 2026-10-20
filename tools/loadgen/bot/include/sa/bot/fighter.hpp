#pragma once

#include "sa/bot/observation.hpp"
#include "sa/bot/wanderer.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace sa::bot {

struct FighterParams {
    float engage_radius_m = 30.0F;  // notice monsters this far away
    std::uint32_t attack_skill_id = 1;
    float attack_range_m = 2.5F;                  // stop here and swing
    std::chrono::milliseconds attack_interval{};  // between casts; the zone enforces the cooldown
    WandererParams roam;                          // what to do when nothing is in sight
};

// Hunts monsters: walks to the nearest live one it can see, then casts its attack on a timer.
// With nothing in sight it roams like a Wanderer. Deterministic for a seed.
class Fighter final : public Brain {
public:
    Fighter(std::uint64_t seed, FighterParams params);

    [[nodiscard]] Intent Decide(const Observation& observation) override;

    [[nodiscard]] std::optional<common::EntityId> target() const noexcept { return target_; }
    [[nodiscard]] std::uint32_t casts() const noexcept { return casts_; }

private:
    [[nodiscard]] static const SeenEntity* NearestMonster(const Observation& observation,
                                                          float within_m);

    FighterParams params_;
    Wanderer roam_;
    std::optional<common::EntityId> target_;
    std::uint64_t next_cast_tick_ = 0;
    std::uint32_t casts_ = 0;
};

}  // namespace sa::bot
